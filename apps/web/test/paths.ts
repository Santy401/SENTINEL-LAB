import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

export const webRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
export const repoRoot = join(webRoot, "..", "..");
export const examplesDir = join(repoRoot, "packages", "protocol", "examples");
export const firmwareFixturesDir = join(webRoot, "src", "fixtures", "firmware");
