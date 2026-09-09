#!/usr/bin/env python3
"""Provide the shared integer quality rule for source experiments and replay."""

from __future__ import annotations

import math


STATUS_LEVELS = {"provisional": 1, "effective": 2, "exact": 3}
QUALITY_STATUS_STRIDE = 1_000_001
QUALITY_SCORE_SCALE = 1_000_000


def quality(status_level: object, score_millionths: object) -> int:
    """Return integer quality for one normalized source result."""

    if type(status_level) is not int or status_level not in STATUS_LEVELS.values():
        raise ValueError("the source status level is invalid")
    if (
        type(score_millionths) is not int
        or not 0 <= score_millionths <= QUALITY_SCORE_SCALE
    ):
        raise ValueError("the source score is invalid")
    return status_level * QUALITY_STATUS_STRIDE + score_millionths


def quality_from_score(status: object, score: object) -> tuple[int, int]:
    """Normalize one status and finite score to the shared integer scale."""

    if not isinstance(status, str) or status not in STATUS_LEVELS:
        raise ValueError("the source status is invalid")
    if isinstance(score, bool) or not isinstance(score, (int, float)):
        raise ValueError("the source score is invalid")
    numeric = float(score)
    if not math.isfinite(numeric) or not 0.0 <= numeric <= 1.0:
        raise ValueError("the source score is invalid")
    score_millionths = round(numeric * QUALITY_SCORE_SCALE)
    return STATUS_LEVELS[status], score_millionths


def candidate_quality(status: object, score: object) -> int:
    """Return shared integer quality from an unnormalized source result."""

    level, millionths = quality_from_score(status, score)
    return quality(level, millionths)
