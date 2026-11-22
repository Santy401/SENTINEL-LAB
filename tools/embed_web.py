#!/usr/bin/env python3
"""Package apps/web/dist for the ESP32 realtime image.

Writes web_assets/{index.html,app.js,app.css,manifest.webmanifest,icons}.gz
and manifest.json.
Asset names are fixed (so platformio.ini never changes); index.html is
rewritten to reference ./assets/app.{js,css}?v=<sha8> for cache busting.
gzip output is deterministic (mtime 0, no file name), so --check can prove
that the committed bundle matches a fresh `npm run build`.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "apps" / "web" / "dist"
OUT = ROOT / "web_assets"
# Flash budget for the embedded web (app partition is 6.5 MiB; keep headroom).
MAX_TOTAL_GZ = 600 * 1024
# Static files Vite copies from apps/web/public to the dist root (home-screen
# install: manifest and icons). Fixed names so firmware routes stay stable.
ROOT_FILES = ("manifest.webmanifest", "apple-touch-icon.png", "icon-192.png", "icon-512.png")
BUNDLES = ("index.html.gz", "app.js.gz", "app.css.gz") + tuple(f"{n}.gz" for n in ROOT_FILES)


def gz(data: bytes) -> bytes:
    buffer = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=buffer, compresslevel=9, mtime=0) as handle:
        handle.write(data)
    return buffer.getvalue()


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def package(dist: Path) -> dict[str, bytes]:
    index = (dist / "index.html").read_text(encoding="utf-8")
    scripts = re.findall(r'src="\./assets/([^"]+\.js)"', index)
    styles = re.findall(r'href="\./assets/([^"]+\.css)"', index)
    if len(scripts) != 1 or len(styles) != 1:
        raise SystemExit(f"FAIL: expected one script and one stylesheet, got {scripts} {styles}")
    js = (dist / "assets" / scripts[0]).read_bytes()
    css = (dist / "assets" / styles[0]).read_bytes()
    extra = sorted(p.name for p in (dist / "assets").iterdir()) if (dist / "assets").is_dir() else []
    if set(extra) != {scripts[0], styles[0]}:
        raise SystemExit(f"FAIL: unexpected extra assets {sorted(set(extra) - {scripts[0], styles[0]})}")
    index = index.replace(f"./assets/{scripts[0]}", f"./assets/app.js?v={sha(js)[:8]}")
    index = index.replace(f"./assets/{styles[0]}", f"./assets/app.css?v={sha(css)[:8]}")
    root_extra = sorted(p.name for p in dist.iterdir() if p.is_file() and p.name != "index.html")
    if set(root_extra) != set(ROOT_FILES):
        raise SystemExit(f"FAIL: dist root files {root_extra} != expected {sorted(ROOT_FILES)}")
    files = {
        "index.html.gz": gz(index.encode("utf-8")),
        "app.js.gz": gz(js),
        "app.css.gz": gz(css),
    }
    for name in ROOT_FILES:
        files[f"{name}.gz"] = gz((dist / name).read_bytes())
    manifest = {
        "source": "apps/web/dist",
        "files": {name: {"sha256": sha(data), "bytes": len(data)} for name, data in files.items()},
        "uncompressed": {"index.html": len(index.encode()), "app.js": len(js), "app.css": len(css)},
    }
    files["manifest.json"] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    total = sum(len(files[n]) for n in BUNDLES)
    if total > MAX_TOTAL_GZ:
        raise SystemExit(f"FAIL: embedded web is {total} B, budget {MAX_TOTAL_GZ} B")
    return files


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dist", type=Path, default=DIST)
    parser.add_argument("--out", type=Path, default=OUT)
    parser.add_argument("--check", action="store_true",
                        help="fail if the committed bundle differs from --dist")
    args = parser.parse_args(argv)
    if not (args.dist / "index.html").is_file():
        print("FAIL: run `npm run build` in apps/web first", file=sys.stderr)
        return 2
    files = package(args.dist)
    if args.check:
        stale = [n for n, data in files.items()
                 if not (args.out / n).is_file() or (args.out / n).read_bytes() != data]
        if stale:
            print(f"FAIL: web_assets is stale ({', '.join(stale)}); run tools/embed_web.py",
                  file=sys.stderr)
            return 1
        print("OK   web_assets matches apps/web/dist")
        return 0
    args.out.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        (args.out / name).write_bytes(data)
    total = sum(len(files[n]) for n in BUNDLES)
    print(f"OK   wrote {args.out} ({total} B gzip)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
