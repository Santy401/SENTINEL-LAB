import { existsSync, readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { firmwareFixturesDir, repoRoot } from "./paths";

// build/ is not versioned. When the host suite has produced it locally, the
// copies used by the web app must match what the firmware generated.
const buildDir = join(repoRoot, "build");
const generated = existsSync(buildDir) ? readdirSync(buildDir).filter(n => /^firmware-.*\.json$/.test(n)) : [];

describe("firmware fixture copies", () => {
  if (generated.length === 0) {
    // CI checkouts have no build/: the committed copies are still validated by
    // protocol.test.ts; only the freshness check needs the host suite output.
    it.skip("build/firmware-*.json not generated on this host", () => {});
    return;
  }
  it.each(generated)("%s matches build/", name => {
    const copy = join(firmwareFixturesDir, name);
    expect(existsSync(copy), `run npm run sync-fixtures (${name} missing)`).toBe(true);
    expect(readFileSync(copy, "utf8")).toBe(readFileSync(join(buildDir, name), "utf8"));
  });
});
