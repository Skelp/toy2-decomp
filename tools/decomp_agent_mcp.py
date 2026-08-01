#!/usr/bin/env python3
"""Run-scoped MCP server that accepts validated fresh-run requests."""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

MAX_HANDOFF_BYTES = 6 * 1024
REASONS = {"completed_slice", "context_pressure_after_bank"}
TOOL = "request_fresh_run"


def git(root: Path, *args: str) -> str:
    result = subprocess.run(["git", *args], cwd=root, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or "git command failed")
    return result.stdout.rstrip("\n")


def validate_handoff(arguments: object) -> dict:
    if not isinstance(arguments, dict):
        raise ValueError("the handoff must be an object")
    required = {"reason", "summary", "next_prompt", "addresses", "do_not_repeat"}
    if set(arguments) != required:
        raise ValueError("the handoff fields do not match the tool schema")
    if arguments["reason"] not in REASONS:
        raise ValueError("the rotation reason is invalid")
    for key in ("summary", "next_prompt"):
        if not isinstance(arguments[key], str) or not arguments[key].strip():
            raise ValueError(f"{key} must be a nonempty string")
    for key, cap in (("addresses", 16), ("do_not_repeat", 8)):
        if not isinstance(arguments[key], list) or len(arguments[key]) > cap:
            raise ValueError(f"{key} must contain at most {cap} strings")
        if not all(isinstance(item, str) and item.strip() for item in arguments[key]):
            raise ValueError(f"{key} must contain nonempty strings")
    if any(not re.fullmatch(r"0x[0-9A-Fa-f]{8}", item) for item in arguments["addresses"]):
        raise ValueError("each address must use the form 0x00401230")
    encoded = json.dumps(arguments, ensure_ascii=False, separators=(",", ":")).encode()
    if len(encoded) > MAX_HANDOFF_BYTES:
        raise ValueError("the complete handoff exceeds 6 KiB")
    return arguments


def validate_repository(context: dict) -> str:
    root = Path(context["root"])
    if git(root, "branch", "--show-current") != "agent/continuous":
        raise RuntimeError("the current branch is not agent/continuous")
    head = git(root, "rev-parse", "HEAD")
    if head == context["previous_head"]:
        raise RuntimeError("HEAD did not advance during this cycle")
    status = git(root, "status", "--porcelain=v1", "--untracked-files=all")
    if status != context["startup_status"]:
        raise RuntimeError("the working-tree state does not match the startup baseline")
    if status and git(root, "-C", "external/submodules/reccmp", "rev-parse", "HEAD") != context["reccmp_head"]:
        raise RuntimeError("the pre-existing reccmp submodule pointer changed")
    remote = git(root, "rev-parse", "origin/agent/continuous")
    if head != remote:
        raise RuntimeError("HEAD is not pushed to origin/agent/continuous")
    return head


def tool_schema() -> dict:
    return {
        "name": TOOL,
        "description": "Request one fresh reconstruction run after a clean pushed slice.",
        "inputSchema": {
            "type": "object",
            "additionalProperties": False,
            "required": ["reason", "summary", "next_prompt", "addresses", "do_not_repeat"],
            "properties": {
                "reason": {"type": "string", "enum": sorted(REASONS)},
                "summary": {"type": "string"},
                "next_prompt": {"type": "string"},
                "addresses": {"type": "array", "maxItems": 16, "items": {"type": "string"}},
                "do_not_repeat": {"type": "array", "maxItems": 8, "items": {"type": "string"}},
            },
        },
    }


def handle(message: dict, context_path: Path) -> dict | None:
    request_id = message.get("id")
    method = message.get("method")
    if request_id is None:
        return None
    if method == "initialize":
        return {"jsonrpc": "2.0", "id": request_id, "result": {"protocolVersion": "2025-06-18", "capabilities": {"tools": {}}, "serverInfo": {"name": "decomp-rotation", "version": "1"}}}
    if method == "tools/list":
        return {"jsonrpc": "2.0", "id": request_id, "result": {"tools": [tool_schema()]}}
    if method != "tools/call":
        return {"jsonrpc": "2.0", "id": request_id, "error": {"code": -32601, "message": "method not found"}}
    params = message.get("params", {})
    if params.get("name") != TOOL:
        return {"jsonrpc": "2.0", "id": request_id, "error": {"code": -32602, "message": "unknown tool"}}
    try:
        context = json.loads(context_path.read_text(encoding="utf-8"))
        if Path(context["pending_path"]).exists():
            raise RuntimeError("a rotation request already exists for this cycle")
        if int(context["cycle"]) >= int(context["max_cycles"]):
            raise RuntimeError("the run reached its cycle limit")
        handoff = validate_handoff(params.get("arguments"))
        handoff["commit"] = validate_repository(context)
        pending = Path(context["pending_path"])
        pending.write_text(json.dumps(handoff, indent=2) + "\n", encoding="utf-8")
        result = {"content": [{"type": "text", "text": "Rotation accepted. Provide the final response now. Do not call more tools."}], "structuredContent": {"accepted": True, "commit": handoff["commit"]}}
    except (ValueError, RuntimeError, KeyError, OSError, json.JSONDecodeError) as error:
        result = {"content": [{"type": "text", "text": f"Rotation rejected: {error}"}], "isError": True, "structuredContent": {"accepted": False, "error": str(error)}}
    return {"jsonrpc": "2.0", "id": request_id, "result": result}


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: decomp_agent_mcp.py CONTEXT.json", file=sys.stderr)
        return 2
    context_path = Path(sys.argv[1])
    for line in sys.stdin:
        try:
            message = json.loads(line)
            response = handle(message, context_path)
        except (json.JSONDecodeError, TypeError) as error:
            response = {"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": str(error)}}
        if response is not None:
            print(json.dumps(response, separators=(",", ":")), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
