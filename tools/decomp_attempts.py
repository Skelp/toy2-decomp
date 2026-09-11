#!/usr/bin/env python3
"""Log reconstruction attempts per target and hint when to stop iterating."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tomllib
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.decomp_diff import read_similarity  # noqa: E402


def attempts_directory(root: Path = ROOT) -> Path:
    return root / "build" / "decomp-attempts"


ATTEMPTS_DIR = attempts_directory()
BEST_DIR = ROOT / "build" / "decomp-cache" / "best"
SOURCE_PATHS = ("src", "tools/Resources/functions_map.txt")
CAMPAIGN_STATE = Path("build") / "decomp-campaign-state.json"
DEFAULT_BUDGET = 12
STALL_ATTEMPTS = 3
STALL_MIN_GAIN_POINTS = 0.5
EXTEND_GAIN_POINTS = 1.0


def canonical_address(value: str) -> str:
    try:
        return f"0x{int(value, 16):08X}"
    except (TypeError, ValueError) as error:
        raise argparse.ArgumentTypeError("use an address such as 0x00401000") from error


def read_rows(address: str, directory: Path = ATTEMPTS_DIR) -> list[dict[str, object]]:
    """Return the logged attempt rows of a target in attempt order."""
    path = directory / f"{canonical_address(address)}.jsonl"
    text = path.read_text(encoding="utf-8") if path.exists() else ""
    try:
        rows = [json.loads(line) for line in text.splitlines() if line.strip()]
    except json.JSONDecodeError:
        return []
    return [
        row for row in rows
        if isinstance(row, dict) and isinstance(row.get("raw"), (int, float))
    ]


def read_raws(address: str, directory: Path = ATTEMPTS_DIR) -> list[float]:
    """Return the logged similarity percentages of a target in attempt order."""
    return [float(row["raw"]) for row in read_rows(address, directory)]


def parse_diff(text: str) -> tuple[float, bool, bool] | None:
    """Return (percent, exact, effective) from a reccmp verbose diff, else None."""
    similarity = read_similarity(text)
    if similarity is None:
        return None
    effective = re.search(r"effective\s+match|\(effective\)", text, re.I) is not None
    return round(similarity * 100.0, 2), similarity >= 1.0 and not effective, effective


def base_budget() -> int:
    """Return the base attempt budget. `campaigns run --budget N` exports
    DECOMP_BUDGET, so the writer's bc counts against the budget the driver enforces."""
    value = os.environ.get("DECOMP_BUDGET", "")
    return int(value) if value.isdigit() and int(value) >= 2 else DEFAULT_BUDGET


def budget(raws: list[float]) -> tuple[int, bool]:
    """Return (attempt budget, stalled) from the gains of every attempt after the first."""
    gains = [raw - max(raws[:index]) for index, raw in enumerate(raws) if index]
    extended = any(gain >= EXTEND_GAIN_POINTS for gain in gains[-4:])
    recent = gains[-STALL_ATTEMPTS:]
    stalled = len(recent) >= STALL_ATTEMPTS and all(g < STALL_MIN_GAIN_POINTS for g in recent)
    return 2 * base_budget() if extended else base_budget(), stalled


def read_stats(address: str, directory: Path = ATTEMPTS_DIR) -> dict[str, object]:
    raws = read_raws(address, directory)
    best = max(raws, default=None)
    limit, stalled = budget(raws)
    return {
        "attempts": len(raws), "best_attempt": raws.index(best) + 1 if raws else None,
        "best_raw": best, "last_raw": raws[-1] if raws else None,
        "stalled": stalled, "limit": limit,
    }


# Helpers of `tools/decomp campaigns run`, the scripted loop: they turn tool JSON
# and text into the tab-separated fields the bash driver reads.
def subsystem_of(row: dict[str, object]) -> str:
    """Name a campaign subsystem: the source file stem, else the class of the name."""
    source = str(row.get("source") or "")
    if source:
        return Path(source).stem
    parts = str(row.get("name") or "").split("::")
    return parts[-2] if len(parts) >= 2 else (parts[0] or "Unknown")


def pick_row(
    rows: list[dict[str, object]], address: str | None = None,
    coverage: bool = False, skip: tuple[str, ...] = (),
) -> dict[str, object] | None:
    """Return the forced target's row, else the first workable row not in skip."""
    for row in rows:
        here = canonical_address(str(row.get("address")))
        if address is not None:
            if here == address:
                return row
        elif (row.get("work_target") and not row.get("map_defect") and here not in skip
              and (row.get("dependency_ready") or not coverage)):
            return row
    return None


def mapped_row(address: str, root: Path) -> dict[str, object] | None:
    """Describe a forced target that no queue lists, from the map and the source."""
    name = None
    map_path = root / "tools" / "Resources" / "functions_map.txt"
    for line in map_path.read_text(encoding="utf-8").splitlines():
        parts = line.split(None, 1)
        if len(parts) == 2 and parts[0].upper() == address.upper():
            name = parts[1].strip()
    if name is None:
        return None
    state, source = "NOT_STARTED", ""
    pattern = re.compile(rf"// (FUNCTION|STUB): TOY2 {address}\b", re.I)
    for path in sorted((root / "src").rglob("*.cpp")):
        found = pattern.search(path.read_text(encoding="utf-8", errors="ignore"))
        if found:
            state, source = found.group(1).upper(), path.relative_to(root / "src").as_posix()
            break
    size = 0
    try:
        sizes = json.loads((root / "build" / "decomp-function-sizes.json").read_text())
        size = next(int(i["size"]) for i in sizes if int(i["address"], 16) == int(address, 16))
    except (OSError, ValueError, StopIteration, KeyError, TypeError):
        pass
    return {"address": address, "name": name, "state": state, "size": size, "source": source}


# The writer harnesses of `campaigns run`. This is the one place that defines
# their commands, so a run, --check and --dry-run all print and run the same one.
HARNESSES = ("claude", "codex")
CODEX_MARKERS = ("CODEX_THREAD_ID", "CODEX_SESSION_ID")
SKILL_PATH = ".agents/skills/decomp-expert/SKILL.md"
DEFAULT_EFFORT = {"claude": "xhigh", "codex": "high"}
HARNESS_FORMAT = {"claude": "claude-json", "codex": "codex-jsonl"}
FORMAT_SUFFIX = {"claude-json": ".json", "codex-jsonl": ".jsonl", "text": ".txt"}
LOGIN = {"claude": "claude auth login", "codex": "codex login"}
NETWORK_SETTING = "sandbox_workspace_write.network_access = true"
EXIT_CODES = {"done": 0, "review": 1, "usage": 2, "check": 2, "writer": 3, "interrupted": 130}
AUTH_ERROR = re.compile(
    r"\b40[13]\b|unauthori[sz]ed|invalid api key|not logged in|log ?in\b|authenticat|oauth"
    r"|token (?:has )?expired", re.I)
QUOTA_ERROR = re.compile(r"\b429\b|rate.?limit|quota|usage limit|limit reached|credits|billing", re.I)
NETWORK_ERROR = re.compile(
    r"network|could not resolve|dns|connection (?:refused|reset|error|failed)|unreachable|econn"
    r"|enotfound|timed out|stream disconnected|error sending request|offline", re.I)
SANDBOX_ERROR = re.compile(r"Read-only file system|Operation not permitted")


def ancestor_names(pid: int | None = None) -> list[str]:
    """Return the command names of the parent processes, nearest first (Linux /proc)."""
    names: list[str] = []
    pid = os.getppid() if pid is None else pid
    while pid > 1 and len(names) < 64:
        try:
            stat = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8", errors="replace")
            names.append(stat[stat.find("(") + 1 : stat.rfind(")")])
            pid = int(stat[stat.rfind(")") + 2 :].split()[1])
        except (OSError, ValueError, IndexError):
            break
    return names


def detect_harness(env, on_path, ancestors=()) -> tuple[str, str]:
    """Choose the writer harness for --harness auto: the session the driver runs in,
    else the first harness on PATH. Return (harness or "", reason)."""
    marker = next((name for name in CODEX_MARKERS if env.get(name)), "")
    in_claude = bool(env.get("CLAUDECODE"))
    if in_claude and marker:
        # One harness started the other; the nearer parent process is the session.
        nearest = next((name for name in ancestors if name in HARNESSES), "")
        if nearest:
            return nearest, f"CLAUDECODE and {marker} are set; the nearest parent is {nearest}"
    if in_claude:
        return "claude", "a Claude Code shell: CLAUDECODE is set"
    if marker:
        return "codex", f"a Codex shell: {marker} is set"
    for name in HARNESSES:
        if on_path(name):
            return name, f"no harness session; {name} is the first harness on PATH"
    return "", "no harness session, and neither claude nor codex is on PATH"


def codex_extra_dirs(root: Path) -> list[Path]:
    """Return the directories a codex writer writes outside its workspace-write root.

    Codex keeps .git read only, and `git checkout -- src` writes the index there (a
    worktree also keeps its git directory outside the tree). Wine writes its prefix,
    which .tooling can link to outside the tree."""
    found: list[Path] = []
    try:
        gitdir = subprocess.run(["git", "rev-parse", "--absolute-git-dir"], cwd=root,
                                capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        gitdir = ""
    if gitdir:
        found.append(Path(gitdir))
    prefix = (root / ".tooling" / "wineprefix").resolve()
    if not prefix.is_relative_to(root.resolve()):
        found.append(prefix)
    return found


def codex_user_model(env=os.environ) -> str:
    """Return the model of the user's codex config, which the codex writer ignores."""
    home = Path(env.get("CODEX_HOME") or Path.home() / ".codex")
    try:
        with (home / "config.toml").open("rb") as handle:
            model = tomllib.load(handle).get("model")
    except (OSError, tomllib.TOMLDecodeError):
        return ""
    return model if isinstance(model, str) else ""


def writer_command(harness: str, root: Path, effort: str = "", model: str = "") -> str:
    """Return the built-in writer command of a harness; the assignment arrives on stdin."""
    effort = shlex.quote(effort or DEFAULT_EFFORT[harness])
    if harness == "claude":
        # The skill is the whole system prompt; the project allow list is the only grant.
        words = ["claude -p --tools Bash --system-prompt-file", SKILL_PATH,
                 "--strict-mcp-config --setting-sources project --permission-mode dontAsk",
                 "--no-session-persistence --output-format json --effort", effort]
        return " ".join(words + (["--model", shlex.quote(model)] if model else []))
    # Codex has no system-prompt flag; developer_instructions adds the skill and keeps
    # the base instructions. A value that is not TOML (the skill starts with ---) is
    # taken as a literal string. Network stays on because the network filter also
    # blocks the local socket wineserver binds; approval_policy=never forbids escalation.
    # --ignore-user-config drops the user's MCP servers, skills and settings, and the
    # disabled apps and plugins drop their catalogues: all cost tokens on every turn.
    # Auth still comes from CODEX_HOME; the model comes from --model or the user config.
    words = ["codex exec --ephemeral --ignore-user-config --disable apps --disable plugins",
             "-s workspace-write -C", shlex.quote(str(root))]
    for directory in codex_extra_dirs(root):
        words += ["--add-dir", shlex.quote(str(directory))]
    words += ["-c sandbox_workspace_write.network_access=true -c approval_policy=never",
              f"-c tool_output_token_limit=12000 -c model_reasoning_effort={effort}"]
    model = model or codex_user_model()
    words += ["-m", shlex.quote(model)] if model else []
    words += [f'-c "developer_instructions=$(cat {SKILL_PATH})"',
              '--json -o "$DECOMP_WRITER_LAST" -']
    return " ".join(words)


def resolve_writer(choice: str, source: str, override: str, fmt: str, effort: str,
                   model: str, root: Path, env=os.environ) -> tuple[str, str, str, str, str]:
    """Return (harness, reason, format, binary, command) of a run's writer."""
    if override:
        binary = (override.split() or [""])[0]
        harness = choice if choice in HARNESSES else (binary if binary in HARNESSES else "custom")
        fmt = fmt or HARNESS_FORMAT.get(binary, "text")
        return harness, f"{source} sets the writer command", fmt, binary, override
    if choice in HARNESSES:
        harness, reason = choice, f"{source} {choice}" if source == "--harness" else f"{source}={choice}"
    else:
        harness, reason = detect_harness(env, lambda name: shutil.which(name) is not None,
                                         ancestor_names())
    if not harness:
        return "", reason, fmt or "text", "", ""
    return (harness, reason, fmt or HARNESS_FORMAT[harness], harness,
            writer_command(harness, root, effort, model))


def _number(value: object) -> int:
    try:
        return int(value or 0)
    except (TypeError, ValueError):
        return 0


def empty_usage() -> dict[str, object]:
    return {"cost": None, "input": 0, "cache_write": 0, "cache_read": 0, "output": 0,
            "reasoning": 0, "turns": 0, "api_ms": None, "denials": 0, "error": 0,
            "message": "", "result": ""}


def first_line(text: str) -> str:
    return (str(text).strip().splitlines() or [""])[0].strip()


def claude_usage(text: str) -> dict[str, object]:
    """Read a `claude -p --output-format json` result. input excludes the cache."""
    try:
        data = json.loads(text)
    except json.JSONDecodeError:
        data = {}
    data = data if isinstance(data, dict) else {}
    usage = data.get("usage") if isinstance(data.get("usage"), dict) else {}
    details = usage.get("output_tokens_details")
    details = details if isinstance(details, dict) else {}
    fields = empty_usage()
    fields.update(
        cost=float(data["total_cost_usd"]) if data.get("total_cost_usd") is not None else None,
        input=_number(usage.get("input_tokens")),
        cache_write=_number(usage.get("cache_creation_input_tokens")),
        cache_read=_number(usage.get("cache_read_input_tokens")),
        output=_number(usage.get("output_tokens")),
        reasoning=_number(details.get("thinking_tokens")),
        turns=_number(data.get("num_turns")),
        api_ms=_number(data["duration_api_ms"]) if "duration_api_ms" in data else None,
        denials=len(data.get("permission_denials") or []),
        error=int(bool(data.get("is_error"))), result=str(data.get("result") or ""))
    if fields["error"]:
        fields["message"] = (first_line(fields["result"])
                             or f"API error status {data.get('api_error_status')}")
    return fields


def provider_message(text: object) -> str:
    """Return a codex error message; a provider error arrives as a JSON body."""
    text = str(text or "").strip()
    try:
        body = json.loads(text)
    except json.JSONDecodeError:
        return text
    if not isinstance(body, dict):
        return text
    error = body.get("error") if isinstance(body.get("error"), dict) else body
    message = str(error.get("message") or text)
    return f"{body['status']}: {message}" if body.get("status") else message


def codex_usage(text: str, last: str = "") -> dict[str, object]:
    """Read `codex exec --json` events: usage summed over turn.completed events (input
    excludes the cached part, as for claude), the error of error or turn.failed events,
    and the final message from the -o file. A turn is one command or the last reply."""
    fields = empty_usage()
    commands, completed, failed, errors, replies = 0, False, "", [], []
    for line in text.splitlines():
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        kind = event.get("type") if isinstance(event, dict) else None
        if kind == "turn.completed":
            completed = True
            usage = event.get("usage") if isinstance(event.get("usage"), dict) else {}
            cached = _number(usage.get("cached_input_tokens"))
            fields["input"] += _number(usage.get("input_tokens")) - cached
            fields["cache_read"] += cached
            fields["cache_write"] += _number(usage.get("cache_write_input_tokens"))
            fields["output"] += _number(usage.get("output_tokens"))
            fields["reasoning"] += _number(usage.get("reasoning_output_tokens"))
        elif kind == "turn.failed":
            error = event.get("error") if isinstance(event.get("error"), dict) else {}
            failed = provider_message(error.get("message"))
        elif kind == "error":
            errors.append(provider_message(event.get("message")))
        elif kind == "item.completed" and isinstance(event.get("item"), dict):
            item = event["item"]
            if item.get("type") == "command_execution":
                commands += 1
                fields["denials"] += bool(SANDBOX_ERROR.search(str(item.get("aggregated_output"))))
            elif item.get("type") == "agent_message":
                replies.append(str(item.get("text") or ""))
    message = failed or (errors[-1] if errors and not completed else "")
    fields.update(turns=commands + int(completed), error=int(bool(message)), message=message,
                  result=last.strip() or (replies[-1] if replies else ""))
    return fields


def writer_usage(text: str, fmt: str = "claude-json", last: str = "") -> dict[str, object]:
    """Read the usage, error and final message of one writer run in its output format."""
    if fmt == "codex-jsonl":
        return codex_usage(text, last)
    if fmt == "text":
        fields = empty_usage()
        fields.update(turns=int(bool(text.strip())), result=text)
        return fields
    return claude_usage(text)


def writer_failure(fields: dict[str, object], status: int, stderr: str = "") -> str:
    """Say why a writer run did no work, or return "" when it ran and returned."""
    if status == 124:
        return "the writer timed out"
    if status == 127:
        return "the writer command was not found"
    detail = str(fields["message"]) or (stderr.strip().splitlines() or [""])[-1].strip()
    if status:
        return f"the writer exited with status {status}" + (f": {detail[:300]}" if detail else "")
    if fields["error"]:
        return f"the writer reported an error: {str(fields['message'])[:300]}"
    return "" if fields["turns"] else "the writer ran no turn"


def failure_fix(failure: str, harness: str, env=os.environ) -> tuple[str, str]:
    """Return (fix hint, next command) for a writer failure."""
    other = "codex" if harness == "claude" else "claude"
    check = "tools/decomp campaigns run --check --live" + (
        f" --harness {harness}" if harness in HARNESSES else "")
    if failure == "the writer timed out":
        return "raise DECOMP_WRITER_TIMEOUT (seconds; default 2400) or lower --batch", check
    if failure == "the writer command was not found":
        return f"install {harness} or pass --harness {other}", f"command -v {harness}"
    if NETWORK_ERROR.search(failure) and env.get("CODEX_SANDBOX_NETWORK_DISABLED"):
        return (f"this Codex sandbox blocks network; set {NETWORK_SETTING} in "
                "~/.codex/config.toml, or run outside the sandbox", check)
    if AUTH_ERROR.search(failure):
        return f"log in: {LOGIN.get(harness, 'log the harness in')}", LOGIN.get(harness, check)
    if QUOTA_ERROR.search(failure):
        return f"wait for the usage limit to reset, or pass --harness {other}", check
    if NETWORK_ERROR.search(failure):
        return f"check the network; inside a Codex sandbox set {NETWORK_SETTING}", check
    return f"read the writer log, then {check}", check


def live_verdict(fields: dict[str, object], status: int, stderr: str, head: str,
                 harness: str, seconds: int) -> tuple[str, str, str, str]:
    """Judge the --check --live writer call: (ok|FAIL, detail, fix, next command)."""
    failure = writer_failure(fields, status, stderr)
    if failure:
        return ("FAIL", failure, *failure_fix(failure, harness))
    prompt = fields["input"] + fields["cache_write"] + fields["cache_read"]
    turns = max(int(fields["turns"]), 1)
    cost = f"${fields['cost']:.2f}" if fields["cost"] is not None else f"{prompt // 1000}k prompt tokens"
    skill = "skill seen" if "# Decomp expert" in str(fields["result"]) else "skill NOT seen"
    detail = (f"{fields['turns']} turns, context about {prompt / turns / 1000:.1f}k tokens per turn, "
              f"{fields['denials']} denials, {cost}, {seconds} s, {skill}")
    if fields["denials"]:
        return "FAIL", detail, ("the project allow list (.claude/settings.json) denied the command"
                                if harness == "claude" else "the sandbox denied a write"), ""
    if head and head not in str(fields["result"]):
        return "FAIL", detail + "; the command output is missing", "read the check-live log", ""
    return "ok", detail, "", ""


def handoff_text(fields: dict[str, object]) -> str:
    """Return a batch writer's final message as handoff text, from its title line on."""
    if fields["error"]:
        return ""
    lines = [line for line in str(fields["result"]).strip().splitlines()
             if not line.lstrip().startswith("```")]
    titles = [index for index, line in enumerate(lines) if line.startswith("# ")]
    return "\n".join(lines[titles[0] if titles else 0:]).strip()


def usage_row(fields: dict[str, object]) -> str:
    """Return the usage.tsv fields of a writer run; an unknown value stays empty."""
    names = ("cost", "input", "cache_write", "cache_read", "output", "reasoning", "turns",
             "api_ms", "denials", "error")
    return "\t".join("" if fields[name] is None else
                     f"{fields[name]:.4f}" if name == "cost" else str(fields[name])
                     for name in names)


# The run log of `campaigns run`: every operator line is one JSON event in
# events.jsonl, and state.json holds the run's current phase for --status.
def utc_stamp() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def runs_directory(root: Path = ROOT) -> Path:
    return root / "build" / "decomp-runs"


def field_value(text: str) -> object:
    """Keep whole and decimal numbers as numbers, everything else as text."""
    if re.fullmatch(r"[-+]?\d+", text):
        return int(text)
    if re.fullmatch(r"[-+]?\d+\.\d+", text):
        return float(text)
    return text


def parse_pairs(pairs: list[str]) -> dict[str, object]:
    return {key: field_value(value) for key, _, value in (pair.partition("=") for pair in pairs)}


def read_json_file(path: Path) -> dict[str, object]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return data if isinstance(data, dict) else {}


def update_state(path: Path, values: dict[str, object], reset: bool = False) -> dict[str, object]:
    """Merge values into the run state and replace the file in one rename."""
    state = {} if reset else read_json_file(path)
    for key in ("pid", "writer_pid"):
        if key in values:
            values[f"{key}_start"] = process_start(values[key]) if values[key] else ""
    state.update(values, updated=utc_stamp())
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}")
    temporary.write_text(json.dumps(state, indent=1) + "\n", encoding="utf-8")
    os.replace(temporary, path)
    return state


def append_event(path: Path | None, event: str, text: str,
                 fields: dict[str, object]) -> dict[str, object]:
    """Return one event record and append it to path; None appends nothing."""
    record = {"ts": utc_stamp(), "event": event, "text": text, "fields": fields}
    if path is None:
        return record
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(record) + "\n")
    return record


def final_line(kind: str, reason: str, next_step: str) -> tuple[int, str]:
    code = EXIT_CODES[kind]
    return code, f"run: exit {code} ({reason}); next: {next_step}"


def process_start(pid: object) -> str:
    """Return the start time of a process (field 22 of /proc/PID/stat), or ""."""
    try:
        stat = Path(f"/proc/{int(pid)}/stat").read_text(encoding="utf-8", errors="replace")
        return stat[stat.rfind(")") + 2 :].split()[19]
    except (OSError, ValueError, IndexError, TypeError):
        return ""


def pid_alive(pid: object, start: object = "") -> bool:
    """Return whether pid is still the process the run state recorded: the same start
    time when one was recorded, else a process whose command line names decomp."""
    now = process_start(pid)
    if not now or int(pid) <= 0:
        return False
    if start:
        return now == str(start)
    try:
        return b"decomp" in Path(f"/proc/{int(pid)}/cmdline").read_bytes()
    except OSError:
        return False


def _age(seconds: float) -> str:
    return f"{seconds:.0f} s" if seconds < 120 else f"{seconds / 60:.1f} min"


def _seconds_since(stamp: object, now: datetime) -> float:
    try:
        then = datetime.fromisoformat(str(stamp).replace("Z", "+00:00"))
    except ValueError:
        return 0.0
    return max((now - then).total_seconds(), 0.0)


def run_status(root: Path = ROOT, now: datetime | None = None) -> dict[str, object]:
    """Describe the run in build/decomp-runs/state.json for --status."""
    now = now or datetime.now(timezone.utc)
    runs = runs_directory(root)
    state = read_json_file(runs / "state.json")
    report: dict[str, object] = {"state": state, "alive": False, "stale": False,
                                 "writer_alive": False, "reason": "", "next": "",
                                 "attempts": [], "logs": [], "lines": []}
    lines: list[str] = report["lines"]  # type: ignore[assignment]
    if not state:
        report.update(reason="no run state", next="tools/decomp campaigns run --check")
        lines.append(f"run: no run state ({runs.relative_to(root)}/state.json);"
                     f" next: {report['next']}")
        return report
    get = state.get
    alive = pid_alive(get("pid"), get("pid_start"))
    finished = get("exit_code") is not None
    report.update(alive=alive, stale=not alive and not finished)
    if finished:
        report.update(reason=get("reason"), next=get("next"))
        lines.append(f"run: finished, exit {get('exit_code')} ({get('reason')}); next: {get('next')}")
    elif alive:
        report.update(reason=f"phase {get('phase')}", next="tools/decomp campaigns run --status",
                      stop=f"kill -TERM {get('pid')}")
        lines.append(f"run: pid {get('pid')} alive, {get('harness')}, phase {get('phase')},"
                     f" {_age(_seconds_since(get('started'), now))} since start;"
                     f" stop: kill -TERM {get('pid')}")
    else:
        lines += stale_lines(report, root, now)
    if get("target"):
        batch = f", batch {get('batch')}" if get("batch") else ""
        lines.append(f"campaign {get('campaign')}/{get('count')}: {get('target')} {get('name')}"
                     f" {get('mode')}{batch}")
    if isinstance(get("baseline"), (int, float)):
        best = get("best") if isinstance(get("best"), (int, float)) else get("baseline")
        lines.append(f"score: baseline {float(get('baseline')):.2f}%, best {float(best):.2f}%,"
                     f" attempts {get('attempts')}")
    if get("last_event") and not finished:
        lines.append(f"last: {str(get('last_event')).strip()}")
    if get("target") and get("phase") in ("batch", "repair"):
        rows = read_rows(str(get("target")), attempts_directory(root))[-4:]
        report["attempts"] = rows
        if rows:
            lines.append("attempts: " + ", ".join(f"{row.get('n')} {float(row['raw']):.2f}%"
                                                  for row in rows))
    stamp = now.timestamp()
    logs = sorted((path for path in runs.glob("*") if path.is_file() and is_run_log(path.name)),
                  key=lambda path: path.stat().st_mtime, reverse=True)[:3]
    report["logs"] = [path.relative_to(root).as_posix() for path in logs]
    log = root / str(get("log") or "-")
    if get("log") and not finished and log.is_file():
        lines.append(f"log: {get('log')} ({log.stat().st_size // 1024} KB,"
                     f" updated {_age(stamp - log.stat().st_mtime)} ago)")
    elif logs:
        lines.append("newest logs: " + ", ".join(
            f"{path.name} ({_age(stamp - path.stat().st_mtime)})" for path in logs))
    return report


def is_run_log(name: str) -> bool:
    """Name the files of build/decomp-runs a --status reader may open: not the state,
    the event log, prompts or the queue files that --check and --dry-run also write."""
    return (name not in ("state.json", "events.jsonl") and not name.endswith((".prompt", ".msg"))
            and not name.startswith(("candidates", "coverage", "check-")))


def changed_sources(root: Path) -> int:
    """Return the number of changed paths under src and the map, 0 when git fails."""
    try:
        return len(subprocess.run(["git", "status", "--porcelain", "--", *SOURCE_PATHS],
                                  cwd=root, capture_output=True, text=True, check=True)
                   .stdout.splitlines())
    except (OSError, subprocess.CalledProcessError):
        return 0


def stale_lines(report: dict[str, object], root: Path, now: datetime) -> list[str]:
    """Describe what a run that died without a final line left, and choose one next
    step: stop its writer, abort its campaign, review src, or check the setup."""
    get = report["state"].get  # type: ignore[union-attr]
    writer = get("writer_pid")
    report["writer_alive"] = bool(writer) and pid_alive(writer, get("writer_pid_start"))
    campaign = (root / CAMPAIGN_STATE).is_file()
    changed = changed_sources(root)
    patch = f"build/decomp-cache/best/{get('target')}.patch"
    if report["writer_alive"]:
        next_step = f"kill -TERM {writer}"
    elif campaign:
        next_step = 'tools/decomp campaigns abort --reason "campaigns run died"'
    elif changed:
        next_step = "git status --short -- " + " ".join(SOURCE_PATHS)
    else:
        next_step = "tools/decomp campaigns run --check"
    report.update(reason=f"stale state, phase {get('phase')}", next=next_step)
    lines = [f"run: pid {get('pid')} not alive; stale state (phase {get('phase')}, updated"
             f" {_age(_seconds_since(get('updated'), now))} ago); next: {next_step}"]
    if report["writer_alive"]:
        lines.append(f"writer: pid {writer} still running; it can still edit src")
    if campaign:
        lines.append(f"campaign: still active ({CAMPAIGN_STATE.as_posix()})")
    if changed:
        lines.append(f"src: {changed} changed paths")
    if get("target") and (root / patch).is_file():
        lines.append(f"best patch: {patch}")
    return lines


def busy_run(report: dict[str, object], caller: int) -> tuple[str, str]:
    """Return (what still runs, next command) when a run or its writer blocks a new run."""
    get = report["state"].get  # type: ignore[union-attr]
    if report["alive"] and get("exit_code") is None and int(get("pid")) != caller:
        return f"run pid {get('pid')} is still going", "tools/decomp campaigns run --status"
    writer = get("writer_pid")
    if report["stale"] and writer and pid_alive(writer, get("writer_pid_start")):
        return f"writer pid {writer} of a dead run is still going", f"kill -TERM {writer}"
    return "", ""


def tried_models(handoff: str) -> list[str]:
    """Return the TRIED lines of a handoff, without the baseline line."""
    models: list[str] = []
    inside = False
    for line in handoff.splitlines():
        if line.startswith("TRIED"):
            inside = True
        elif inside and line[:1] not in (" ", "\t"):
            break
        elif inside and line.strip() and "baseline" not in line.lower():
            models.append(" ".join(line.split()))
    return models


def failure_lines(log: str) -> list[str]:
    """Return the validate problems and lint findings of a validate log."""
    found: list[str] = []
    inside = False
    for line in log.splitlines():
        if line.strip() == "validation failed:":
            inside = True
            continue
        inside = inside and line.startswith("- ")
        if inside or re.search(r": (error|warning): |^lint: failed", line):
            if line not in found:
                found.append(line)
    return found[:40]


def clear_attempts(addresses: list[str], directory: Path = ATTEMPTS_DIR) -> None:
    for address in addresses:
        (directory / f"{canonical_address(address)}.jsonl").unlink(missing_ok=True)


def source_diff(root: Path) -> bytes | None:
    """Return the diff of the source paths against HEAD, or None when git fails."""
    command = ["git", "diff", "HEAD", "--", *SOURCE_PATHS]
    try:
        return subprocess.run(command, cwd=root, capture_output=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None


def source_tree(diff: bytes | None) -> str:
    """Return the sha256 of a source diff, or an empty string when git failed."""
    return "" if diff is None else hashlib.sha256(diff).hexdigest()


def save_best_diffs(address: str, diff_path: Path, root: Path) -> list[Path]:
    """Copy the verbose and compact diffs of a new best next to its patch."""
    name = canonical_address(address)
    compact_path = diff_path.with_name(f"{diff_path.stem}.compact.txt")
    copied: list[Path] = []
    for source, suffix in ((diff_path, ".txt"), (compact_path, ".compact.txt")):
        if not source.is_file():
            continue
        target = root / "build" / "decomp-cache" / "best" / f"{name}{suffix}"
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        copied.append(target)
    return copied


def save_best_patch(
    address: str, root: Path, diff_path: Path | None = None, diff: bytes | None = None
) -> Path | None:
    """Save the source diff (and the attempt's diffs) as the best of a target."""
    if diff_path is not None:
        save_best_diffs(address, diff_path, root)
    diff = source_diff(root) if diff is None else diff
    if diff is None:
        return None
    path = root / "build" / "decomp-cache" / "best" / f"{canonical_address(address)}.patch"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(diff)
    return path


def unchanged_line(last: dict[str, object], count: int) -> str:
    """Describe the last attempt that already measured the current source tree."""
    return (
        f"source unchanged since attempt {last.get('n', count)} "
        f"(score {float(last['raw']):.2f}%); not logged"
    )


def log_attempt(
    address: str, diff_path: Path, root: Path = ROOT, quiet: bool = False
) -> list[str]:
    """Append one attempt for a target under root and return the lines to print."""
    try:
        parsed = parse_diff(diff_path.read_text(encoding="utf-8", errors="replace"))
    except OSError:
        parsed = None
    if parsed is None:
        return [f"warning: no similarity verdict in {diff_path}; attempt not logged"]
    raw, exact, effective = parsed
    rows = read_rows(address, attempts_directory(root))
    diff = source_diff(root)
    tree = source_tree(diff)
    if rows and tree and rows[-1].get("tree") == tree:
        return [unchanged_line(rows[-1], len(rows))]
    raws = [float(row["raw"]) for row in rows]
    previous_best = max(raws, default=None)
    raws.append(raw)
    at = datetime.now(timezone.utc).replace(microsecond=0).isoformat()
    row = {
        "n": len(raws), "at": at, "raw": raw, "exact": exact, "effective": effective,
        "tree": tree,
    }
    path = attempts_directory(root) / f"{canonical_address(address)}.jsonl"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(row) + "\n")
    count, best, (limit, stalled) = len(raws), max(raws), budget(raws)
    lines = [] if quiet else [
        f"attempt {count}/{limit}  raw {raw:.2f}% ({raw - (previous_best or 0.0):+.2f})"
        f"  best {best:.2f}% (attempt {raws.index(best) + 1})"
    ]
    if previous_best is None or raw > previous_best:
        patch = save_best_patch(address, root, diff_path, diff)
        if patch is not None and not quiet:
            lines.append(f"best patch: {patch}")
    if stalled:
        lines.append(f"stall: {STALL_ATTEMPTS} attempts without a {STALL_MIN_GAIN_POINTS:g}-point "
                     "gain; stop this batch")
    if count >= limit:
        lines.append(f"budget: {count} attempts used; finish this campaign")
    return lines


def run_subcommands(subparsers) -> None:
    """Add the subcommands the `campaigns run` driver calls."""
    writer = subparsers.add_parser("writer", help="print harness, reason, format, binary, command")
    for name in ("--harness", "--from", "--writer", "--format", "--effort", "--model"):
        writer.add_argument(name, default="")
    usage = subparsers.add_parser("writer-usage", help="print the usage fields of a writer run")
    usage.add_argument("file", type=Path)
    usage.add_argument("--format", default="claude-json", choices=tuple(FORMAT_SUFFIX))
    usage.add_argument("--harness", default="")
    usage.add_argument("--result", type=int, metavar="LINES", help="print the result tail")
    usage.add_argument("--failure", type=int, metavar="STATUS",
                       help="print why it did no work, the fix and the next command")
    usage.add_argument("--handoff", action="store_true", help="print the result as a handoff")
    usage.add_argument("--live", metavar="HEAD", help="judge a --check --live call")
    usage.add_argument("--status", type=int, default=0)
    usage.add_argument("--seconds", type=int, default=0)
    event = subparsers.add_parser("event", help="print and log one run event")
    event.add_argument("name")
    event.add_argument("text", nargs="?")
    event.add_argument("--field", action="append", default=[])
    event.add_argument("--set", action="append", default=[])
    for flag in ("--state", "--reset", "--json", "--stderr"):
        event.add_argument(flag, action="store_true")
    final = subparsers.add_parser("final", help="print the final line; exit with its code")
    final.add_argument("--kind", required=True, choices=tuple(EXIT_CODES))
    final.add_argument("--reason", required=True)
    final.add_argument("--next", required=True)
    for flag in ("--state", "--json"):
        final.add_argument(flag, action="store_true")
    status = subparsers.add_parser("run-status", help="show the run of state.json")
    status.add_argument("--json", action="store_true")
    status.add_argument("--busy", action="store_true",
                        help="print what blocks a new run and the next command, tab separated")
    status.add_argument("--self", type=int, default=0, help="the pid of the asking run")


def run_command(args) -> int:
    """Run one driver subcommand and return its exit status."""
    runs = runs_directory(args.root)
    if args.command == "writer":
        print("\n".join(resolve_writer(args.harness, getattr(args, "from"), args.writer,
                                       args.format, args.effort, args.model, args.root)))
        return 0
    if args.command == "writer-usage":
        path: Path = args.file
        read = lambda file: file.read_text(errors="replace") if file.is_file() else ""  # noqa: E731
        fields = writer_usage(read(path), args.format, read(path.with_suffix(".last.txt")))
        stderr = read(path.with_suffix(".stderr"))
        if args.live is not None:
            output = "\t".join(live_verdict(fields, args.status, stderr, args.live,
                                            args.harness, args.seconds))
        elif args.failure is not None:
            failure = writer_failure(fields, args.failure, stderr)
            output = "\n".join((failure, *failure_fix(failure, args.harness))) if failure else ""
        elif args.handoff:
            output = handoff_text(fields)
        elif args.result is not None:
            output = "\n".join(str(fields["result"]).strip().splitlines()[-args.result:])
        else:
            output = usage_row(fields)
        print(output) if output else None
        return 0
    if args.command == "event":
        state = parse_pairs(args.set)
        if args.text is not None:
            state["last_event"] = args.text.strip()
            record = append_event(runs / "events.jsonl" if args.state else None, args.name,
                                  args.text, parse_pairs(args.field))
            line = json.dumps(record) if args.json else args.text
            print(line, file=sys.stderr if args.stderr and not args.json else sys.stdout)
        if args.state:
            update_state(runs / "state.json", state, args.reset)
        return 0
    if args.command == "final":
        code, text = final_line(args.kind, args.reason, args.next)
        record = append_event(runs / "events.jsonl" if args.state else None, "final", text,
                              {"exit_code": code, "reason": args.reason, "next": args.next})
        if args.state:
            update_state(runs / "state.json", {
                "phase": "done" if code == 0 else "stopped", "exit_code": code, "writer_pid": "",
                "reason": args.reason, "next": args.next, "last_event": text})
        print(json.dumps(record) if args.json else text)
        return code
    report = run_status(args.root)
    if args.busy:
        busy = busy_run(report, args.self)
        print("\t".join(busy)) if busy[0] else None
    elif args.json:
        print(json.dumps({key: value for key, value in report.items() if key != "lines"}))
    else:
        print("\n".join(report["lines"]))  # type: ignore[arg-type]
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help=argparse.SUPPRESS)
    subparsers = parser.add_subparsers(dest="command", required=True)
    attempt = subparsers.add_parser("attempt", help="log one comparison of a target")
    attempt.add_argument("--address", required=True, type=canonical_address)
    attempt.add_argument("--diff", required=True, type=Path)
    attempt.add_argument("--quiet", action="store_true")
    clear = subparsers.add_parser("clear", help="forget the attempts of the given targets")
    clear.add_argument("--address", action="append", required=True, type=canonical_address)
    stats = subparsers.add_parser("stats", help="show the attempt counts of a target")
    stats.add_argument("--address", required=True, type=canonical_address)
    stats.add_argument("--json", action="store_true")
    pick = subparsers.add_parser("pick", help="print the next target of candidates JSON")
    pick.add_argument("files", nargs="+", type=Path)
    pick.add_argument("--address", type=canonical_address)
    pick.add_argument("--coverage", action="store_true")
    pick.add_argument("--skip", action="append", default=[], type=canonical_address)
    run_subcommands(subparsers)
    for name in ("tried", "failures"):
        subparsers.add_parser(name, help=f"print the {name} lines of a file").add_argument(
            "file", type=Path)
    args = parser.parse_args(argv)
    if args.command in ("writer", "writer-usage", "event", "final", "run-status"):
        return run_command(args)
    if args.command == "pick":
        rows = [row for path in args.files for row in json.loads(path.read_text() or "[]")]
        row = pick_row(rows, args.address, args.coverage, tuple(args.skip))
        if row is None and args.address is not None:
            row = mapped_row(args.address, args.root)
        if row is None:
            return 1
        fields = (canonical_address(str(row["address"])), row.get("name"), row.get("state"),
                  row.get("size") or 0, row.get("source") or "", subsystem_of(row))
        print("\n".join(str(field) for field in fields))
        return 0
    if args.command in ("tried", "failures"):
        text = args.file.read_text(errors="replace") if args.file.is_file() else ""
        lines = tried_models(text) if args.command == "tried" else failure_lines(text)
        print("\n".join(lines)) if lines else None
        return 0
    if args.command == "attempt":
        for line in log_attempt(args.address, args.diff, args.root, args.quiet):
            print(line)
    elif args.command == "clear":
        clear_attempts(args.address, attempts_directory(args.root))
        print(f"Cleared the attempts of {len(args.address)} target(s).")
    else:
        result = read_stats(args.address, attempts_directory(args.root))
        print(json.dumps(result) if args.json else " ".join(f"{k}={v}" for k, v in result.items()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
