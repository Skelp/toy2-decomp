#!/usr/bin/env python3
"""Foreground supervisor for fresh-thread decompilation cycles."""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STATE_ROOT = ROOT / "build/decomp-agent"
CURRENT = STATE_ROOT / "current.json"
MCP = ROOT / "tools/decomp_agent_mcp.py"
SKILL = ROOT / ".agents/skills/continue-decomp/SKILL.md"
DEFAULT_PROMPT = (
    "Complete one coherent reconstruction slice. Validate, commit, sync, report, and push it. "
    "Request another fresh run only when supported work remains."
)


def git(*args: str) -> str:
    result = subprocess.run(["git", *args], cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or "git command failed")
    return result.stdout.rstrip("\n")


def startup_repository() -> tuple[str, str, str]:
    if git("branch", "--show-current") != "agent/continuous":
        raise RuntimeError("agent run requires the agent/continuous branch")
    status = git("status", "--porcelain=v1", "--untracked-files=all")
    allowed = " M external/submodules/reccmp"
    if status not in ("", allowed):
        raise RuntimeError("the working tree has unsupported startup changes")
    reccmp_head = git("-C", "external/submodules/reccmp", "rev-parse", "HEAD") if status else ""
    return git("rev-parse", "HEAD"), status, reccmp_head


class RpcClient:
    def __init__(self, process: asyncio.subprocess.Process, log):
        self.process = process
        self.log = log
        self.next_id = 1
        self.pending: dict[int, asyncio.Future] = {}
        self.events: asyncio.Queue = asyncio.Queue()
        self.reader_task = asyncio.create_task(self._read())
        self.stderr_task = asyncio.create_task(self._stderr())

    async def _read(self):
        assert self.process.stdout
        async for raw in self.process.stdout:
            message = json.loads(raw)
            self.log("protocol", direction="in", message=message)
            request_id = message.get("id")
            if request_id in self.pending:
                future = self.pending.pop(request_id)
                if "error" in message:
                    future.set_exception(RuntimeError(str(message["error"])))
                else:
                    future.set_result(message.get("result"))
            elif "method" in message:
                await self.events.put(message)

    async def _stderr(self):
        assert self.process.stderr
        async for raw in self.process.stderr:
            self.log("app_stderr", text=raw.decode(errors="replace").rstrip())

    async def request(self, method: str, params: dict, timeout: float = 30):
        request_id = self.next_id
        self.next_id += 1
        message = {"jsonrpc": "2.0", "id": request_id, "method": method, "params": params}
        self.log("protocol", direction="out", message=message)
        future = asyncio.get_running_loop().create_future()
        self.pending[request_id] = future
        assert self.process.stdin
        self.process.stdin.write((json.dumps(message, separators=(",", ":")) + "\n").encode())
        await self.process.stdin.drain()
        return await asyncio.wait_for(future, timeout)

    async def notify(self, method: str, params: dict):
        message = {"jsonrpc": "2.0", "method": method, "params": params}
        assert self.process.stdin
        self.process.stdin.write((json.dumps(message, separators=(",", ":")) + "\n").encode())
        await self.process.stdin.drain()

    async def event(self, timeout: float = 1):
        return await asyncio.wait_for(self.events.get(), timeout)


class Supervisor:
    def __init__(self, args):
        self.args = args
        self.run_id = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
        self.run_dir = STATE_ROOT / self.run_id
        self.events_path = self.run_dir / "events.jsonl"
        self.context_path = self.run_dir / "rotation-context.json"
        self.pending_path = self.run_dir / "pending-rotation.json"
        self.stop_path = self.run_dir / "stop.json"
        self.previous_head = ""
        self.startup_status = ""
        self.reccmp_head = ""
        self.rpc: RpcClient | None = None
        self.thread_id: str | None = None
        self.turn_id: str | None = None
        self.shutdown_requested = False
        self.forced = False
        self.cycle_started_at = 0.0

    def log(self, event: str, **fields):
        record = {"time": time.time(), "event": event, **fields}
        self.run_dir.mkdir(parents=True, exist_ok=True)
        with self.events_path.open("a", encoding="utf-8") as output:
            output.write(json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n")

    def write_state(self, status: str, cycle: int = 0, failure: str | None = None):
        state = {"run_id": self.run_id, "pid": os.getpid(), "status": status, "cycle": cycle, "max_cycles": self.args.max_cycles, "thread_id": self.thread_id, "turn_id": self.turn_id, "run_dir": str(self.run_dir), "failure": failure}
        CURRENT.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")

    def write_context(self, cycle: int):
        context = {"root": str(ROOT), "previous_head": self.previous_head, "startup_status": self.startup_status, "reccmp_head": self.reccmp_head, "pending_path": str(self.pending_path), "cycle": cycle, "max_cycles": self.args.max_cycles}
        self.context_path.write_text(json.dumps(context, indent=2) + "\n", encoding="utf-8")

    async def start_server(self):
        override = (
            "mcp_servers={session_rotation={"
            f"command={json.dumps(sys.executable)},"
            f"args=[{json.dumps(str(MCP))},{json.dumps(str(self.context_path))}]"
            "}}"
        )
        command = [
            "codex", "app-server", "--stdio",
            "--disable", "apps",
            "-c", 'approval_policy="never"',
            "-c", 'sandbox_mode="danger-full-access"',
            "-c", override,
        ]
        self.log("app_start", command=command)
        process = await asyncio.create_subprocess_exec(
            *command, cwd=ROOT, stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
            limit=16 * 1024 * 1024,
        )
        self.rpc = RpcClient(process, self.log)
        await self.rpc.request("initialize", {"clientInfo": {"name": "toy2-decomp-agent", "version": "1"}, "capabilities": {"experimentalApi": True}})
        await self.rpc.notify("initialized", {})
        servers = await self.rpc.request("mcpServerStatus/list", {}, 30)
        names = [item.get("name") for item in servers.get("data", [])]
        if names != ["session_rotation"]:
            raise RuntimeError(f"unexpected MCP server set: {names}")

    def thread_params(self) -> dict:
        params: dict = {"cwd": str(ROOT), "approvalPolicy": "never", "sandbox": "danger-full-access"}
        for source, target in (("model", "model"), ("service_tier", "serviceTier")):
            value = getattr(self.args, source, None)
            if value:
                params[target] = value
        return params

    def turn_input(self, prompt: str) -> list[dict]:
        return [{"type": "text", "text": prompt}, {"type": "skill", "name": "continue-decomp", "path": str(SKILL)}]

    async def start_cycle(self, prompt: str, cycle: int, predecessor: str | None = None):
        assert self.rpc
        self.write_context(cycle)
        response = await self.rpc.request("thread/start", self.thread_params())
        new_thread = response["thread"]["id"]
        turn_params: dict = {"threadId": new_thread, "input": self.turn_input(prompt), "cwd": str(ROOT), "approvalPolicy": "never"}
        if self.args.effort:
            turn_params["effort"] = self.args.effort
        turn = await self.rpc.request("turn/start", turn_params)
        new_turn = turn["turn"]["id"]
        await self.wait_for("turn/started", new_thread, new_turn, 30)
        self.thread_id, self.turn_id = new_thread, new_turn
        self.cycle_started_at = time.monotonic()
        self.log("cycle_started", cycle=cycle, thread_id=new_thread, turn_id=new_turn, commit=self.previous_head)
        print(f"cycle {cycle}/{self.args.max_cycles}: started thread {new_thread}", flush=True)
        self.write_state("running", cycle)
        if predecessor:
            await self.rpc.request("thread/archive", {"threadId": predecessor})
            await self.rpc.request("thread/unsubscribe", {"threadId": predecessor})
            self.log("predecessor_archived", thread_id=predecessor)

    async def wait_for(self, method: str, thread: str, turn: str | None, timeout: float):
        assert self.rpc
        deadline = time.monotonic() + timeout
        while True:
            event = await self.rpc.event(max(0.1, deadline - time.monotonic()))
            params = event.get("params", {})
            event_thread = params.get("threadId") or params.get("thread", {}).get("id")
            event_turn = params.get("turnId") or params.get("turn", {}).get("id")
            self.handle_progress(event)
            if event.get("method") == method and event_thread == thread and (turn is None or event_turn == turn):
                return event

    def handle_progress(self, event: dict):
        method, params = event.get("method", ""), event.get("params", {})
        if method == "thread/tokenUsage/updated":
            self.log("token_usage", thread_id=params.get("threadId"), turn_id=params.get("turnId"), usage=params.get("tokenUsage"))
        elif method in ("thread/compacted", "context/compacted") or "compact" in method.lower():
            self.log("compaction", method=method, params=params)
            print("context compacted", flush=True)
        elif method == "item/completed":
            item = params.get("item", {})
            if item.get("type") == "agentMessage":
                text = item.get("text", "").strip().replace("\n", " ")
                if text:
                    print(text[:240], flush=True)

    def recheck_rotation(self, handoff: dict) -> str:
        head, status, reccmp_head = startup_repository()
        if status != self.startup_status:
            raise RuntimeError("the repository changed after the rotation request")
        if reccmp_head != self.reccmp_head:
            raise RuntimeError("the reccmp submodule pointer changed after the rotation request")
        if head != handoff.get("commit") or git("rev-parse", "origin/agent/continuous") != head:
            raise RuntimeError("the accepted commit is no longer the pushed HEAD")
        return head

    async def stop_turn(self, force: bool):
        self.pending_path.unlink(missing_ok=True)
        if force:
            self.shutdown_requested = True
            self.forced = True
            if not self.rpc or not self.thread_id or not self.turn_id:
                return
            await self.rpc.request("turn/interrupt", {"threadId": self.thread_id, "turnId": self.turn_id})
            self.log("forced_stop", thread_id=self.thread_id, turn_id=self.turn_id)
        elif not self.shutdown_requested:
            self.shutdown_requested = True
            if not self.rpc or not self.thread_id or not self.turn_id:
                return
            await self.rpc.request("turn/steer", {"threadId": self.thread_id, "expectedTurnId": self.turn_id, "input": [{"type": "text", "text": "Stop at the next safe boundary. Preserve and report the current repository state. Do not request a fresh run."}]})
            self.log("graceful_stop", thread_id=self.thread_id, turn_id=self.turn_id)

    async def watch_turn(self):
        assert self.rpc and self.thread_id and self.turn_id
        while True:
            if self.stop_path.exists():
                request = json.loads(self.stop_path.read_text(encoding="utf-8"))
                self.stop_path.unlink(missing_ok=True)
                await self.stop_turn(bool(request.get("force")))
            try:
                event = await self.rpc.event(0.5)
            except asyncio.TimeoutError:
                continue
            self.handle_progress(event)
            if event.get("method") == "turn/completed" and event.get("params", {}).get("turn", {}).get("id") == self.turn_id:
                self.log("turn_completed", thread_id=self.thread_id, turn_id=self.turn_id, turn=event["params"]["turn"])
                self.log("cycle_duration", thread_id=self.thread_id, turn_id=self.turn_id, seconds=round(time.monotonic() - self.cycle_started_at, 3))
                print(f"turn {self.turn_id}: completed", flush=True)
                return

    @staticmethod
    def handoff_prompt(handoff: dict) -> str:
        return "\n".join(("Continue in a fresh thread from this bounded handoff.", f"Reason: {handoff['reason']}", f"Completed: {handoff['summary']}", f"Next: {handoff['next_prompt']}", "Addresses: " + (", ".join(handoff["addresses"]) or "none"), "Do not repeat: " + ("; ".join(handoff["do_not_repeat"]) or "none")))

    async def run(self) -> int:
        self.run_dir.mkdir(parents=True)
        self.previous_head, self.startup_status, self.reccmp_head = startup_repository()
        self.write_state("starting")
        await self.start_server()
        prompt, predecessor = self.args.prompt, None
        for cycle in range(1, self.args.max_cycles + 1):
            self.pending_path.unlink(missing_ok=True)
            for attempt in range(3):
                try:
                    await self.start_cycle(prompt, cycle, predecessor)
                    break
                except Exception as error:
                    self.log("successor_start_failed", cycle=cycle, attempt=attempt + 1, error=str(error))
                    if attempt == 2:
                        raise
                    await asyncio.sleep(1 << attempt)
            await self.watch_turn()
            if self.shutdown_requested or self.forced or not self.pending_path.exists():
                self.write_state("stopped" if (self.shutdown_requested or self.forced) else "completed", cycle)
                print("agent run stopped" if (self.shutdown_requested or self.forced) else "agent run completed", flush=True)
                return 0
            handoff = json.loads(self.pending_path.read_text(encoding="utf-8"))
            self.previous_head = self.recheck_rotation(handoff)
            self.log("rotation", cycle=cycle, reason=handoff["reason"], commit=self.previous_head, handoff=handoff)
            print(f"rotation accepted: {handoff['reason']} at {self.previous_head[:12]}", flush=True)
            predecessor = self.thread_id
            prompt = self.handoff_prompt(handoff)
        self.write_state("completed", self.args.max_cycles)
        return 0

    async def close(self):
        if self.rpc and self.rpc.process.returncode is None:
            self.rpc.process.terminate()
            await self.rpc.process.wait()


async def run_command(args) -> int:
    supervisor = Supervisor(args)
    loop = asyncio.get_running_loop()
    interrupts = 0

    def interrupt():
        nonlocal interrupts
        interrupts += 1
        asyncio.create_task(supervisor.stop_turn(interrupts > 1))

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, interrupt)
        except (NotImplementedError, RuntimeError):
            signal.signal(sig, lambda *_unused: loop.call_soon_threadsafe(interrupt))
    try:
        return await supervisor.run()
    except Exception as error:
        supervisor.log("failure", error=str(error)) if supervisor.run_dir.exists() else None
        supervisor.write_state("failed", failure=str(error)) if supervisor.run_dir.exists() else None
        print(f"agent: {error}", file=sys.stderr)
        return 1
    finally:
        await supervisor.close()


def read_current() -> dict:
    if not CURRENT.exists():
        raise RuntimeError("no decomp agent run exists")
    return json.loads(CURRENT.read_text(encoding="utf-8"))


def doctor() -> int:
    checks: list[tuple[str, bool, str]] = []
    version = subprocess.run(["codex", "--version"], text=True, capture_output=True)
    checks.append(("Codex 0.146", version.returncode == 0 and "0.146" in version.stdout, version.stdout.strip()))
    try:
        head, status, _ = startup_repository()
        checks.append(("repository", True, f"HEAD {head[:12]}, baseline {status or 'clean'}"))
    except RuntimeError as error:
        checks.append(("repository", False, str(error)))
    context = STATE_ROOT / "doctor-context.json"
    pending = STATE_ROOT / "doctor-pending.json"
    STATE_ROOT.mkdir(parents=True, exist_ok=True)
    context.write_text(json.dumps({"root": str(ROOT), "previous_head": "", "startup_status": "", "pending_path": str(pending), "cycle": 1, "max_cycles": 1}), encoding="utf-8")
    probe = subprocess.run([sys.executable, str(MCP), str(context)], input='{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}\n{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}\n', text=True, capture_output=True)
    checks.append(("rotation MCP", probe.returncode == 0 and "request_fresh_run" in probe.stdout, "local handshake"))

    async def app_probe():
        process = await asyncio.create_subprocess_exec(
            "codex", "app-server", "--stdio", cwd=ROOT,
            stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE, limit=16 * 1024 * 1024,
        )
        rpc = RpcClient(process, lambda *args, **kwargs: None)
        try:
            await rpc.request("initialize", {"clientInfo": {"name": "decomp-doctor", "version": "1"}, "capabilities": {"experimentalApi": True}})
            await rpc.notify("initialized", {})
            account = await rpc.request("account/read", {"refreshToken": False})
            await rpc.request("model/list", {"limit": 1})
            return bool(account)
        finally:
            process.terminate()
            await process.wait()

    try:
        authenticated = asyncio.run(app_probe())
        with tempfile.TemporaryDirectory() as directory:
            schema = subprocess.run(
                ["codex", "app-server", "generate-json-schema", "--experimental", "--out", directory],
                text=True, capture_output=True,
            )
            methods_text = (Path(directory) / "ClientRequest.json").read_text(encoding="utf-8") if schema.returncode == 0 else ""
            required = ("thread/start", "turn/start", "turn/steer", "turn/interrupt", "thread/archive", "thread/unsubscribe")
            methods_available = all(method in methods_text for method in required)
        checks.append(("App Server", methods_available, "initialized with required thread methods"))
        checks.append(("authentication", authenticated, "account/read returned an account"))
    except Exception as error:
        checks.append(("App Server", False, str(error)))
    context.unlink(missing_ok=True)
    for name, passed, detail in checks:
        print(f"{'ok' if passed else 'FAIL':<4} {name}: {detail}")
    return 0 if all(item[1] for item in checks) else 1


def main() -> int:
    parser = argparse.ArgumentParser(prog="tools/decomp agent")
    sub = parser.add_subparsers(dest="command", required=True)
    run = sub.add_parser("run")
    run.add_argument("--prompt", default=DEFAULT_PROMPT)
    run.add_argument("--max-cycles", type=int, default=32)
    run.add_argument("--model")
    run.add_argument("--effort")
    run.add_argument("--service-tier")
    sub.add_parser("status")
    sub.add_parser("log")
    stop = sub.add_parser("stop")
    stop.add_argument("--force", action="store_true")
    sub.add_parser("doctor")
    args = parser.parse_args()
    if args.command == "run":
        if args.max_cycles < 1:
            parser.error("--max-cycles must be positive")
        return asyncio.run(run_command(args))
    if args.command == "doctor":
        return doctor()
    try:
        state = read_current()
    except RuntimeError as error:
        print(f"agent: {error}", file=sys.stderr)
        return 1
    if args.command == "status":
        print(json.dumps(state, indent=2))
    elif args.command == "log":
        path = Path(state["run_dir"]) / "events.jsonl"
        for line in path.read_text(encoding="utf-8").splitlines()[-100:]:
            print(line)
    else:
        if state["status"] not in ("running", "starting"):
            print(f"agent: run is already {state['status']}", file=sys.stderr)
            return 1
        Path(state["run_dir"], "stop.json").write_text(json.dumps({"force": args.force}) + "\n", encoding="utf-8")
        print("Forced stop requested." if args.force else "Safe-boundary stop requested.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
