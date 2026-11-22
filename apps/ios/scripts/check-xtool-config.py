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

# Top-level keys only (extension entries are indented list items).
config = {}
for line in (ROOT / "xtool.yml").read_text().splitlines():
    if line[:1].isspace() or line.lstrip().startswith("#") or ":" not in line:
        continue
    key, value = line.split(":", 1)
    config[key.strip()] = value.strip()
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

# Widget extension: xtool overlay must be variable-free and declare the same
# extension point as the Xcode plist; Package.swift must expose the product.
xtool_yml = (ROOT / "xtool.yml").read_text()
if "SentinelLabWidget" in xtool_yml:
    widget_xcode = plistlib.loads((ROOT / "SentinelLabWidget/Resources/Info.plist").read_bytes())
    widget_xtool_text = (ROOT / "xtool-config/SentinelLabWidget-Info.plist").read_text()
    widget_xtool = plistlib.loads(widget_xtool_text.encode())
    if "$(" in re.sub(r"<!--.*?-->", "", widget_xtool_text, flags=re.S):
        errors.append("xtool-config/SentinelLabWidget-Info.plist contains $(...)")
    if widget_xcode.get("NSExtension") != widget_xtool.get("NSExtension"):
        errors.append("widget NSExtension differs between Xcode and xtool plists")
    for key in VERSIONED:
        if widget_xtool.get(key) != overlay.get(key):
            errors.append(f"widget {key} must match the app ({overlay.get(key)})")
    if 'name: "SentinelLabWidget", targets: ["SentinelLabWidget"]' not in (ROOT / "Package.swift").read_text():
        errors.append("Package.swift must declare the SentinelLabWidget library product")
if overlay.get("NSSupportsLiveActivities") is not True:
    errors.append("NSSupportsLiveActivities must be true in the app Info.plist")

if errors:
    print("\n".join("FAIL: " + e for e in errors))
    sys.exit(1)
print("xtool adapter consistent with the Xcode app inputs")
