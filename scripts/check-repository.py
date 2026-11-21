#!/usr/bin/env python3
"""Check repository hygiene and scan tracked files for obvious secrets."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKIP_SCAN_FILES = {
    "scripts/check-repository.py",
}

REQUIRED_IGNORES = [
    "include/secrets.h",
    "include/rf/connected.h",
    ".env",
    ".pio/",
    "node_modules/",
    "*.pem",
    "*.key",
]

FORBIDDEN_PATHS = [
    (re.compile(r"(^|/)secrets\.h$"), "private Wi-Fi credentials file"),
    (re.compile(r"(^|/)connected\.h$"), "private connectivity file"),
    (re.compile(r"(^|/)\.env$"), "environment secrets file"),
    (re.compile(r"(^|/)\.env\.(?!example$).+"), "private environment variant"),
    (
        re.compile(r"\.(pem|p12|key|mobileprovision|provisionprofile)$"),
        "private key or signing material",
    ),
]

SECRET_PATTERNS = [
    ("private key", re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----")),
    ("AWS access key", re.compile(r"AKIA[0-9A-Z]{16}")),
    ("GitHub token", re.compile(r"gh[pousr]_[A-Za-z0-9]{36,}")),
    ("Slack token", re.compile(r"xox[baprs]-[A-Za-z0-9-]{10,}")),
    ("Telegram bot token", re.compile(r"\b\d{8,12}:[A-Za-z0-9_-]{30,}\b")),
    (
        "assigned credential",
        re.compile(
            r"(?i)\b(password|passwd|secret|api[_-]?key|access[_-]?token|"
            r"bot[_-]?token|chat[_-]?id|webhook[_-]?token|bearer)\b"
            r"\s*[:=]\s*[\"'][^\"'\n]{8,}[\"']"
        ),
    ),
]

MAX_TRACKED_BYTES = 2 * 1024 * 1024


def git_files() -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return [line for line in result.stdout.splitlines() if line]


def is_binary(data: bytes) -> bool:
    return b"\0" in data[:4096]


def check_gitignore(issues: list[str]) -> None:
    gitignore = ROOT / ".gitignore"
    if not gitignore.exists():
        issues.append(".gitignore is missing")
        return
    text = gitignore.read_text(encoding="utf-8")
    for pattern in REQUIRED_IGNORES:
        if pattern not in text:
            issues.append(f".gitignore is missing required pattern: {pattern}")


def check_paths(files: list[str], issues: list[str]) -> None:
    for path in files:
        for pattern, description in FORBIDDEN_PATHS:
            if pattern.search(path):
                issues.append(f"forbidden {description}: {path}")


def check_sizes(files: list[str], issues: list[str]) -> None:
    for path in files:
        full = ROOT / path
        if full.is_file() and full.stat().st_size > MAX_TRACKED_BYTES:
            issues.append(f"tracked file exceeds 2 MiB: {path}")


def check_symlinks(files: list[str], issues: list[str]) -> None:
    for path in files:
        full = ROOT / path
        if full.is_symlink():
            target = full.resolve()
            if ROOT.resolve() not in target.parents:
                issues.append(f"symlink escapes repository: {path}")


def scan_secrets(files: list[str], issues: list[str]) -> None:
    for path in files:
        if path in SKIP_SCAN_FILES:
            continue
        if ".example." in path or path.endswith(".example") or "/example/" in path:
            continue
        full = ROOT / path
        if not full.is_file():
            continue
        data = full.read_bytes()
        if is_binary(data):
            continue
        for number, line in enumerate(data.decode("utf-8", errors="ignore").splitlines(), 1):
            for name, pattern in SECRET_PATTERNS:
                if pattern.search(line):
                    issues.append(f"possible {name} at {path}:{number}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--secrets-only", action="store_true")
    args = parser.parse_args()

    issues: list[str] = []
    files = git_files()

    if not args.secrets_only:
        check_gitignore(issues)
        check_paths(files, issues)
        check_sizes(files, issues)
        check_symlinks(files, issues)

    scan_secrets(files, issues)

    if issues:
        print("Repository check failed:")
        for issue in issues:
            print(f"- {issue}")
        return 1

    print(f"Repository check passed for {len(files)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
