#!/usr/bin/env python3
"""Give fixed route advice after private replay certification."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Mapping, Sequence

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.decomp_mismatch import ROUTE_ORDER  # noqa: E402
from tools.decomp_replay import (  # noqa: E402
    MAX_NON_IMPROVING,
    MAX_TRIALS,
    POLICY_VERSION,
    ReplayError,
    SCHEMA_VERSION,
    _pairs,
    _regular_bytes,
    _reject_constant,
    current_passing_certificate,
)


OBSERVATION_KEYS = {
    "schema_version",
    "kind",
    "routes",
    "tried_routes",
    "frontier",
    "selected_trials",
    "consecutive_non_improving",
    "incumbent_terminal",
}
FRONTIER_KEYS = {"route", "count"}
MAX_OBSERVATION_BYTES = 64 * 1024


class RouteError(ValueError):
    """Report an invalid route-advice operation."""


def _ordered_routes(value: object, description: str) -> list[str]:
    if not isinstance(value, list) or not all(
        isinstance(route, str) and route in ROUTE_ORDER for route in value
    ):
        raise RouteError(f"the {description} is invalid")
    routes = list(value)
    if len(set(routes)) != len(routes):
        raise RouteError(f"the {description} contains a duplicate")
    return routes


def validate_observation(value: object) -> dict[str, object]:
    """Validate one identifier-free route observation."""

    if not isinstance(value, dict) or set(value) != OBSERVATION_KEYS:
        raise RouteError("the route observation has invalid fields")
    if (
        type(value.get("schema_version")) is not int
        or value.get("schema_version") != SCHEMA_VERSION
        or value.get("kind") != "route-observation"
    ):
        raise RouteError("the route observation identity is invalid")
    selected_trials = value.get("selected_trials")
    consecutive_non_improving = value.get("consecutive_non_improving")
    incumbent_terminal = value.get("incumbent_terminal")
    if (
        type(selected_trials) is not int
        or not 0 <= selected_trials <= MAX_TRIALS
        or type(consecutive_non_improving) is not int
        or not 0 <= consecutive_non_improving <= MAX_NON_IMPROVING
        or consecutive_non_improving > selected_trials
        or type(incumbent_terminal) is not bool
    ):
        raise RouteError("the route observation stop state is invalid")
    routes = _ordered_routes(value.get("routes"), "route observation order")
    if routes != [route for route in ROUTE_ORDER if route in set(routes)]:
        raise RouteError("the route observation order is not canonical")
    tried = _ordered_routes(value.get("tried_routes"), "tried route list")
    if tried != [route for route in ROUTE_ORDER if route in set(tried)]:
        raise RouteError("the tried route list is not canonical")
    if any(route not in routes for route in tried):
        raise RouteError("a tried route is absent from the observation order")
    frontier = value.get("frontier")
    if not isinstance(frontier, list):
        raise RouteError("the route observation frontier is invalid")
    counts: dict[str, int] = {}
    for row in frontier:
        if not isinstance(row, Mapping) or set(row) != FRONTIER_KEYS:
            raise RouteError("a route observation frontier row is invalid")
        route = row.get("route")
        count = row.get("count")
        if (
            not isinstance(route, str)
            or route not in ROUTE_ORDER
            or type(count) is not int
            or not 0 < count <= 3
            or route in counts
        ):
            raise RouteError("a route observation frontier row is invalid")
        counts[route] = count
    if list(counts) != [route for route in ROUTE_ORDER if route in counts]:
        raise RouteError("the route observation frontier is not ordered")
    if any(route not in routes or route in tried for route in counts):
        raise RouteError("the route observation frontier is unreachable")
    if len(tried) != consecutive_non_improving:
        raise RouteError("the tried route state is inconsistent")
    return value


def select_route(observation: Mapping[str, object]) -> tuple[str | None, str | None]:
    """Select one route without access to hidden edge outcomes."""

    validate_observation(dict(observation))
    if observation["incumbent_terminal"] is True:
        return None, "terminal-incumbent"
    if int(observation["selected_trials"]) >= MAX_TRIALS:
        return None, "trial-budget-exhausted"
    if int(observation["consecutive_non_improving"]) >= MAX_NON_IMPROVING:
        return None, "non-improvement-budget-exhausted"
    routes = observation["routes"]
    tried = set(observation["tried_routes"])
    frontier = {
        str(row["route"]): int(row["count"])
        for row in observation["frontier"]
        if isinstance(row, Mapping)
    }
    assert isinstance(routes, list)
    for route in routes:
        if route in tried:
            continue
        count = frontier.get(str(route), 0)
        if count > 1:
            return None, "ambiguous-route"
        if count == 1:
            return str(route), None
    return None, "missing-incumbent-route"


def advice_document(
    observation: Mapping[str, object] | None,
    *,
    root: Path = ROOT,
) -> dict[str, object]:
    """Return advice only when the current private certificate passes."""

    certificate = current_passing_certificate(root)
    if certificate is None:
        return {
            "schema_version": SCHEMA_VERSION,
            "kind": "route-advice",
            "policy_version": POLICY_VERSION,
            "certificate_status": "unavailable",
            "status": "withheld",
            "route": None,
            "reason": "private-replay-not-certified",
        }
    if observation is None:
        return {
            "schema_version": SCHEMA_VERSION,
            "kind": "route-advice",
            "policy_version": POLICY_VERSION,
            "certificate_status": "passed",
            "status": "withheld",
            "route": None,
            "reason": "observation-required",
        }
    try:
        route, reason = select_route(observation)
    except RouteError:
        route, reason = None, "invalid-observation"
    if route is None:
        return {
            "schema_version": SCHEMA_VERSION,
            "kind": "route-advice",
            "policy_version": POLICY_VERSION,
            "certificate_status": "passed",
            "status": "withheld",
            "route": None,
            "reason": reason,
        }
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "route-advice",
        "policy_version": POLICY_VERSION,
        "certificate_status": "passed",
        "status": "available",
        "route": route,
        "reason": "certified-fixed-policy",
    }


def _read_observation(path: Path) -> dict[str, object]:
    try:
        content = _regular_bytes(path, MAX_OBSERVATION_BYTES, "route observation file")
    except ReplayError as error:
        raise RouteError("the route observation file is invalid") from error
    try:
        value = json.loads(
            content.decode("utf-8"),
            object_pairs_hook=_pairs,
            parse_constant=_reject_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, ReplayError) as error:
        raise RouteError("the route observation file is invalid") from error
    return validate_observation(value)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    advise = subparsers.add_parser("advise", help="Give certified fixed-policy advice.")
    advise.add_argument("--observation", type=Path)
    advise.add_argument("--json", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    try:
        certificate = current_passing_certificate(ROOT)
        if certificate is None:
            value = {
                "schema_version": SCHEMA_VERSION,
                "kind": "route-advice",
                "policy_version": POLICY_VERSION,
                "certificate_status": "unavailable",
                "status": "withheld",
                "route": None,
                "reason": "private-replay-not-certified",
            }
        else:
            try:
                observation = (
                    _read_observation(arguments.observation)
                    if arguments.observation is not None
                    else None
                )
            except RouteError:
                observation = None
                value = {
                    "schema_version": SCHEMA_VERSION,
                    "kind": "route-advice",
                    "policy_version": POLICY_VERSION,
                    "certificate_status": "passed",
                    "status": "withheld",
                    "route": None,
                    "reason": "invalid-observation",
                }
            else:
                value = None
            if value is not None:
                pass
            elif observation is None:
                value = {
                    "schema_version": SCHEMA_VERSION,
                    "kind": "route-advice",
                    "policy_version": POLICY_VERSION,
                    "certificate_status": "passed",
                    "status": "withheld",
                    "route": None,
                    "reason": "observation-required",
                }
            else:
                route, reason = select_route(observation)
                value = {
                    "schema_version": SCHEMA_VERSION,
                    "kind": "route-advice",
                    "policy_version": POLICY_VERSION,
                    "certificate_status": "passed",
                    "status": "available" if route is not None else "withheld",
                    "route": route,
                    "reason": "certified-fixed-policy" if route is not None else reason,
                }
        if arguments.json:
            print(json.dumps(value, indent=2, sort_keys=True))
        else:
            print(f"Route advice: {value['status']}")
            print(f"Route: {value['route'] or 'none'}")
            print(f"Reason: {value['reason']}")
        return 0
    except Exception:
        print("error: the route advice operation failed", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
