#!/usr/bin/env python3
"""Consistency check between the Xcode app inputs and the xtool adapter.

- xtool-config/Info.plist has no Xcode build variables ($(...)), which xtool
  would copy literally into the app.
- Every key of SentinelLab/Resources/Info.plist that is not a build variable
  or a key xtool generates itself has the same value in the overlay.
- xtool.yml names the product of Package.swift and existing PNG/plist paths.
"""
import plistlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GENERATED = {"CFBundleExecutable", "CFBundleIdentifier", "CFBundleName",
             "CFBundlePackageType", "CFBundleInfoDictionaryVersion"}
VERSIONED = {"CFBundleShortVersionString", "CFBundleVersion"}
errors = []

xcode = plistlib.loads((ROOT / "SentinelLab/Resources/Info.plist").read_bytes())
overlay_path = ROOT / "xtool-config/Info.plist"
overlay_text = overlay_path.read_text()
overlay = plistlib.loads(overlay_text.encode())

if "$(" in re.sub(r"<!--.*?-->", "", overlay_text, flags=re.S):
    errors.append("xtool-config/Info.plist contains an Xcode build variable $(...)")
for key, value in xcode.items():
    if key in GENERATED or key in VERSIONED:
        continue
    if isinstance(value, str) and value.startswith("$("):
        continue
    if overlay.get(key) != value:
        errors.append(f"Info.plist key {key} differs between Xcode and xtool overlay")

project = (ROOT / "project.yml").read_text()
marketing = re.search(r'MARKETING_VERSION:\s*"([^"]+)"', project)
if marketing and not overlay.get("CFBundleShortVersionString", "").startswith(marketing.group(1).split(".")[0]):
    errors.append("CFBundleShortVersionString does not follow MARKETING_VERSION")

config = dict(line.split(":", 1) for line in (ROOT / "xtool.yml").read_text().splitlines()
              if ":" in line and not line.lstrip().startswith("#"))
config = {k.strip(): v.strip() for k, v in config.items()}
if config.get("version") != "1":
    errors.append("xtool.yml: version must be 1")
if 'name: "SentinelLab", targets: ["SentinelLab"]' not in (ROOT / "Package.swift").read_text() \
        or config.get("product") != "SentinelLab":
    errors.append("xtool.yml product must match the single library product in Package.swift")
for key, suffix in (("infoPath", ".plist"), ("iconPath", ".png")):
    path = ROOT / config.get(key, "")
    if not path.is_file() or path.suffix != suffix:
        errors.append(f"xtool.yml {key} must point to an existing {suffix} file")
if not re.fullmatch(r"[A-Za-z0-9.-]+", config.get("bundleID", "")):
    errors.append("xtool.yml bundleID is missing or invalid")

if errors:
    print("\n".join("FAIL: " + e for e in errors))
    sys.exit(1)
print("xtool adapter consistent with the Xcode app inputs")
