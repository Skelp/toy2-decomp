#!/usr/bin/env python3
"""Minimal JSON-RPC App Server fixture for supervisor transport tests."""

import json
import sys

thread_count = 0
turn_count = 0


def send(message):
    print(json.dumps(message, separators=(",", ":")), flush=True)


for line in sys.stdin:
    message = json.loads(line)
    if "id" not in message:
        continue
    request_id, method = message["id"], message["method"]
    if method == "initialize":
        result = {"userAgent": "fake"}
    elif method == "thread/start":
        thread_count += 1
        result = {"thread": {"id": f"thread-{thread_count}"}}
    elif method == "turn/start":
        turn_count += 1
        result = {"turn": {"id": f"turn-{turn_count}"}}
    elif method in ("thread/archive", "thread/unsubscribe", "turn/steer", "turn/interrupt"):
        result = {}
    else:
        send({"jsonrpc": "2.0", "id": request_id, "error": {"code": -32601, "message": "method not found"}})
        continue
    send({"jsonrpc": "2.0", "id": request_id, "result": result})
    if method == "turn/start":
        thread_id = message["params"]["threadId"]
        turn_id = f"turn-{turn_count}"
        send({"jsonrpc": "2.0", "method": "turn/started", "params": {"threadId": thread_id, "turn": {"id": turn_id}}})
        send({"jsonrpc": "2.0", "method": "turn/completed", "params": {"threadId": thread_id, "turn": {"id": turn_id, "status": "completed"}}})
