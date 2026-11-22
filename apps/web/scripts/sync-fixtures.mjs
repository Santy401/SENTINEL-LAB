// Copies firmware-generated Protocol v1 frames (build/firmware-*.json, produced
// by the host test suite and not versioned) into src/fixtures/firmware so the
// web tests and DEMO mode use exactly what the firmware emits.
import { copyFileSync, existsSync, readdirSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, "..", "..", "..");
const source = join(repo, "build");
const target = join(here, "..", "src", "fixtures", "firmware");
mkdirSync(target, { recursive: true });
if (!existsSync(source)) {
  console.error("build/ not found: run python3 tools/check_software.py first");
  process.exit(1);
}
const files = readdirSync(source).filter(name => /^firmware-.*\.json$/.test(name));
if (!files.length) {
  console.error("no build/firmware-*.json fixtures found");
  process.exit(1);
}
for (const name of files) copyFileSync(join(source, name), join(target, name));
console.log(`synced ${files.length} firmware fixtures`);
