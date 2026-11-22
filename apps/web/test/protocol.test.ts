import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { MAX_FRAME_BYTES, OutboundSession, findDuplicateKey, parseServerFrame, validateEnvelope } from "@/lib/protocol";
import { examplesDir, firmwareFixturesDir } from "./paths";

const read = (dir: string) => readdirSync(dir).filter(f => f.endsWith(".json")).map(name => ({ name, text: readFileSync(join(dir, name), "utf8") }));

describe("Protocol v1 contract examples", () => {
  const valid = read(join(examplesDir, "valid"));
  const invalid = read(join(examplesDir, "invalid"));

  it("finds the shared examples", () => {
    expect(valid.length).toBeGreaterThanOrEqual(9);
    expect(invalid.length).toBeGreaterThanOrEqual(16);
  });

  // Examples are pretty-printed and exceed the 1024 B firmware frame cap, which
  // is a transport limit, so size is not enforced for contract files.
  it.each(valid)("accepts valid/$name", ({ text }) => {
    const result = parseServerFrame(text, Number.MAX_SAFE_INTEGER);
    expect(result.ok ? "ok" : result.reason).toBe("ok");
  });

  it.each(invalid)("rejects invalid/$name", ({ text }) => {
    const result = parseServerFrame(text, Number.MAX_SAFE_INTEGER);
    expect(result.ok).toBe(false);
  });
});

describe("firmware-generated frames", () => {
  const frames = read(firmwareFixturesDir);
  it("has all eight firmware fixtures", () => {
    expect(frames.map(f => f.name).sort()).toEqual([
      "firmware-error.json",
      "firmware-event.json",
      "firmware-goodbye.json",
      "firmware-heartbeat.json",
      "firmware-hello.json",
      "firmware-motion.json",
      "firmware-rf.json",
      "firmware-system.json",
    ]);
  });
  it.each(frames)("parses $name within the 1024 B frame limit", ({ text }) => {
    const result = parseServerFrame(text.trim());
    expect(result.ok ? "ok" : result.reason).toBe("ok");
  });
});

describe("parseServerFrame guards", () => {
  const base = JSON.parse(readFileSync(join(firmwareFixturesDir, "firmware-heartbeat.json"), "utf8")) as Record<string, unknown>;

  it("reports unknown types separately", () => {
    const result = parseServerFrame(JSON.stringify({ ...base, type: "rf.scan" }));
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.unknownType).toBe("rf.scan");
  });

  it("rejects oversized frames before parsing", () => {
    const result = parseServerFrame(JSON.stringify({ ...base, pad: "x".repeat(MAX_FRAME_BYTES) }));
    expect(result).toEqual({ ok: false, reason: "frame exceeds size limit" });
  });

  it("rejects non-JSON, non-objects and other protocol versions", () => {
    expect(parseServerFrame("not json").ok).toBe(false);
    expect(parseServerFrame("[]").ok).toBe(false);
    expect(parseServerFrame(JSON.stringify({ ...base, protocolVersion: "1.1.0" })).ok).toBe(false);
  });

  it("rejects calendar-invalid timestamps", () => {
    expect(parseServerFrame(JSON.stringify({ ...base, timestamp: "2026-02-30T10:00:00.000Z" })).ok).toBe(false);
  });

  it("rejects simulated payloads", () => {
    const rf = JSON.parse(readFileSync(join(firmwareFixturesDir, "firmware-rf.json"), "utf8")) as { payload: Record<string, unknown> };
    rf.payload.simulated = true;
    expect(parseServerFrame(JSON.stringify(rf)).ok).toBe(false);
  });

  it("detects duplicate keys at any depth but not across objects", () => {
    expect(findDuplicateKey('{"a":1,"a":2}')).toBe("a");
    expect(findDuplicateKey('{"a":{"b":1,"b":2}}')).toBe("b");
    expect(findDuplicateKey('{"a":[{"b":1},{"b":2}],"c":"a"}')).toBeNull();
    expect(findDuplicateKey('{"k":"x\\"y","z":"k"}')).toBeNull();
  });
});

describe("OutboundSession", () => {
  it("builds valid, strictly increasing client envelopes", () => {
    const session = new OutboundSession("web-dashboard", "web-session-abc");
    const now = new Date("2026-11-21T16:10:02.100Z");
    const hello = session.hello("web-01", now);
    const heartbeat = session.heartbeat("web-01", 1500.7, now);
    const goodbye = session.goodbye("client-request", now);
    expect([hello.sequence, heartbeat.sequence, goodbye.sequence]).toEqual([1, 2, 3]);
    expect(hello.timestamp).toBe("2026-11-21T16:10:02.100Z");
    expect(hello.payload).toEqual({ deviceId: "web-01", role: "web", supportedVersion: "1.0.0", capabilities: ["telemetry.view"] });
    expect(heartbeat.payload.uptimeMs).toBe(1500);
    for (const envelope of [hello, heartbeat, goodbye]) {
      expect(() => validateEnvelope(JSON.parse(JSON.stringify(envelope)))).not.toThrow();
      expect(new TextEncoder().encode(JSON.stringify(envelope)).length).toBeLessThanOrEqual(MAX_FRAME_BYTES);
    }
  });

  it("refuses identifiers outside the contract patterns", () => {
    expect(() => new OutboundSession("Web Dashboard", "s")).toThrow();
    expect(() => new OutboundSession("web", "-bad")).toThrow();
  });
});
