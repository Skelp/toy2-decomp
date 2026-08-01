import asyncio
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


mcp = load("decomp_agent_mcp")
agent = load("decomp_agent")


class HandoffTests(unittest.TestCase):
    def valid(self):
        return {"reason": "completed_slice", "summary": "done", "next_prompt": "next", "addresses": ["0x00401230"], "do_not_repeat": []}

    def test_valid_handoff(self):
        self.assertEqual(mcp.validate_handoff(self.valid())["summary"], "done")

    def test_malformed_and_bounded_handoffs(self):
        for change in (
            {"reason": "other"},
            {"addresses": ["bad"]},
            {"addresses": ["0x00401230"] * 17},
            {"do_not_repeat": ["x"] * 9},
            {"summary": "x" * 7000},
        ):
            value = self.valid() | change
            with self.assertRaises(ValueError):
                mcp.validate_handoff(value)

    def test_repository_rejections_and_acceptance(self):
        context = {"root": "/repo", "previous_head": "old", "startup_status": " M external/submodules/reccmp", "reccmp_head": "submodule"}
        good = ["agent/continuous", "new", context["startup_status"], "submodule", "new"]
        with patch.object(mcp, "git", side_effect=good):
            self.assertEqual(mcp.validate_repository(context), "new")
        cases = (
            ["wrong"],
            ["agent/continuous", "old"],
            ["agent/continuous", "new", " M other"],
            ["agent/continuous", "new", context["startup_status"], "changed"],
            ["agent/continuous", "new", context["startup_status"], "submodule", "remote"],
        )
        for values in cases:
            with patch.object(mcp, "git", side_effect=values), self.assertRaises(RuntimeError):
                mcp.validate_repository(context)

    def test_multiple_calls_and_cycle_limit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pending = root / "pending.json"
            context_path = root / "context.json"
            context_path.write_text(json.dumps({"root": directory, "previous_head": "old", "startup_status": "", "pending_path": str(pending), "cycle": 1, "max_cycles": 2}))
            request = {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {"name": mcp.TOOL, "arguments": self.valid()}}
            with patch.object(mcp, "validate_repository", return_value="new"):
                first = mcp.handle(request, context_path)
                second = mcp.handle(request, context_path)
            self.assertTrue(first["result"]["structuredContent"]["accepted"])
            self.assertTrue(second["result"]["isError"])
            pending.unlink()
            data = json.loads(context_path.read_text())
            data["cycle"] = 2
            context_path.write_text(json.dumps(data))
            limited = mcp.handle(request, context_path)
            self.assertTrue(limited["result"]["isError"])


class McpProtocolTests(unittest.TestCase):
    def test_initialize_discovery_success_and_error(self):
        with tempfile.TemporaryDirectory() as directory:
            context = Path(directory) / "context.json"
            pending = Path(directory) / "pending.json"
            context.write_text(json.dumps({"root": directory, "previous_head": "old", "startup_status": "", "pending_path": str(pending), "cycle": 1, "max_cycles": 2}))
            calls = [
                {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
                {"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}},
                {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": mcp.TOOL, "arguments": {}}},
            ]
            result = subprocess.run([sys.executable, str(TOOLS / "decomp_agent_mcp.py"), str(context)], input="".join(json.dumps(item) + "\n" for item in calls), text=True, capture_output=True)
            rows = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertEqual(rows[0]["result"]["serverInfo"]["name"], "decomp-rotation")
            self.assertEqual(rows[1]["result"]["tools"][0]["name"], mcp.TOOL)
            self.assertTrue(rows[2]["result"]["isError"])


class AppServerProtocolTests(unittest.IsolatedAsyncioTestCase):
    async def test_initialize_fresh_thread_first_turn_and_completion(self):
        process = await asyncio.create_subprocess_exec(
            sys.executable,
            str(TOOLS / "tests/fake_app_server.py"),
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        rpc = agent.RpcClient(process, lambda *args, **kwargs: None)
        try:
            await rpc.request("initialize", {"clientInfo": {"name": "test", "version": "1"}})
            thread = await rpc.request("thread/start", {"cwd": "/repo"})
            turn = await rpc.request("turn/start", {"threadId": thread["thread"]["id"], "input": []})
            started = await rpc.event()
            completed = await rpc.event()
            self.assertEqual(turn["turn"]["id"], "turn-1")
            self.assertEqual(started["method"], "turn/started")
            self.assertEqual(completed["method"], "turn/completed")
        finally:
            process.terminate()
            await process.wait()

    async def test_two_cycle_smoke_uses_new_thread_before_archival(self):
        process = await asyncio.create_subprocess_exec(
            sys.executable, str(TOOLS / "tests/fake_app_server.py"),
            stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
        )
        rpc = agent.RpcClient(process, lambda *args, **kwargs: None)
        try:
            await rpc.request("initialize", {"clientInfo": {"name": "test", "version": "1"}})
            first = await rpc.request("thread/start", {"cwd": "/repo"})
            await rpc.request("turn/start", {"threadId": first["thread"]["id"], "input": []})
            await rpc.event()
            await rpc.event()
            second = await rpc.request("thread/start", {"cwd": "/repo"})
            await rpc.request("turn/start", {"threadId": second["thread"]["id"], "input": []})
            started = await rpc.event()
            self.assertNotEqual(first["thread"]["id"], second["thread"]["id"])
            self.assertEqual(started["method"], "turn/started")
            await rpc.request("thread/archive", {"threadId": first["thread"]["id"]})
        finally:
            process.terminate()
            await process.wait()


class SupervisorTests(unittest.IsolatedAsyncioTestCase):
    async def test_no_request_ends_and_rotation_prompt_is_bounded(self):
        handoff = {"reason": "completed_slice", "summary": "done", "next_prompt": "next", "addresses": [], "do_not_repeat": []}
        self.assertIn("Completed: done", agent.Supervisor.handoff_prompt(handoff))
        args = type("Args", (), {"max_cycles": 2, "prompt": "first"})()
        supervisor = agent.Supervisor(args)
        async def start_server(): supervisor.rpc = object()
        async def start_cycle(prompt, cycle, predecessor=None):
            supervisor.thread_id, supervisor.turn_id = f"thread-{cycle}", f"turn-{cycle}"
        supervisor.start_server = start_server
        supervisor.start_cycle = start_cycle
        supervisor.watch_turn = lambda: asyncio.sleep(0)
        with patch.object(agent, "startup_repository", return_value=("head", "", "")), patch.object(supervisor, "write_state") as state:
            self.assertEqual(await supervisor.run(), 0)
        self.assertEqual(state.call_args.args[0], "completed")

    async def test_successor_archives_only_after_turn_started(self):
        class FakeRpc:
            def __init__(self): self.calls = []
            async def request(self, method, params):
                self.calls.append(method)
                if method == "thread/start": return {"thread": {"id": "new-thread"}}
                if method == "turn/start": return {"turn": {"id": "new-turn"}}
                return {}
        args = type("Args", (), {"max_cycles": 2, "model": None, "effort": None, "service_tier": None})()
        supervisor = agent.Supervisor(args)
        supervisor.run_dir.mkdir(parents=True, exist_ok=True)
        supervisor.rpc = FakeRpc()
        supervisor.previous_head = "head"
        supervisor.startup_status = ""
        async def started(*unused): supervisor.rpc.calls.append("observed-turn-started")
        supervisor.wait_for = started
        with patch.object(supervisor, "write_state"):
            await supervisor.start_cycle("prompt", 2, "old-thread")
        self.assertLess(supervisor.rpc.calls.index("observed-turn-started"), supervisor.rpc.calls.index("thread/archive"))

    async def test_successor_failure_does_not_archive_predecessor(self):
        class FakeRpc:
            def __init__(self): self.calls = []
            async def request(self, method, params):
                self.calls.append(method)
                raise RuntimeError("start failed")
        args = type("Args", (), {"max_cycles": 2, "model": None, "effort": None, "service_tier": None})()
        supervisor = agent.Supervisor(args)
        supervisor.run_dir.mkdir(parents=True, exist_ok=True)
        supervisor.rpc = FakeRpc()
        supervisor.previous_head, supervisor.startup_status = "head", ""
        with self.assertRaises(RuntimeError):
            await supervisor.start_cycle("prompt", 2, "old-thread")
        self.assertNotIn("thread/archive", supervisor.rpc.calls)

    async def test_graceful_and_forced_stop(self):
        class FakeRpc:
            def __init__(self): self.methods = []
            async def request(self, method, params): self.methods.append(method); return {}
        args = type("Args", (), {"max_cycles": 2})()
        supervisor = agent.Supervisor(args)
        supervisor.rpc, supervisor.thread_id, supervisor.turn_id = FakeRpc(), "thread", "turn"
        await supervisor.stop_turn(False)
        await supervisor.stop_turn(True)
        self.assertEqual(supervisor.rpc.methods, ["turn/steer", "turn/interrupt"])


if __name__ == "__main__":
    unittest.main()
