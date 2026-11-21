#!/usr/bin/env python3
"""Validate Conventional Commit subjects supplied by CI or maintainers."""

from __future__ import annotations

import os
import re
import sys

SUBJECT_RE = re.compile(
    r"^(feat|fix|refactor|perf|test|docs|build|ci|chore|revert)"
    r"(?:\([a-z0-9][a-z0-9._/-]*\))?!?: [a-z][a-z0-9 ,:/_-]*[a-z0-9)]$"
)

SELF_TEST_CASES = {
    "feat(repository): add initial project foundation": True,
    "fix(scope): correct chart labels": True,
    "docs: update audit": True,
    "feat(scope)!: change transport contract": True,
    "Invalid subject.": False,
    "Feat(scope): wrong case": False,
    "feat(scope): trailing dot.": False,
}


def is_valid(subject: str) -> bool:
    return bool(SUBJECT_RE.fullmatch(subject.strip()))


def run_self_test() -> int:
    failures = []
    for subject, expected in SELF_TEST_CASES.items():
        if is_valid(subject) != expected:
            failures.append(subject)
    if failures:
        print("Conventional Commit self-test failed:")
        for subject in failures:
            print(f"- {subject!r}")
        return 1
    print(f"Conventional Commit self-test OK ({len(SELF_TEST_CASES)} cases)")
    return 0


def main() -> int:
    if "--self-test" in sys.argv[1:]:
        return run_self_test()

    subject = (os.environ.get("PR_TITLE") or os.environ.get("COMMIT_SUBJECT") or "").strip()
    if not is_valid(subject):
        print(f"Invalid Conventional Commit subject: {subject!r}")
        print(
            "Expected: type(scope): short imperative description\n"
            "Types: feat, fix, refactor, perf, test, docs, build, ci, chore, revert"
        )
        return 1
    print(f"Conventional Commit subject OK: {subject}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
