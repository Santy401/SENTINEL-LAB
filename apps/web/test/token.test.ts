import { describe, expect, it } from "vitest";
import { defaultWsUrl, loadToken, saveToken, subprotocols, TOKEN_KEY, validToken } from "@/lib/token";
import { RealtimeClient } from "@/lib/realtime-client";

function memoryStorage() {
  const map = new Map<string, string>();
  return {
    getItem: (k: string) => map.get(k) ?? null,
    setItem: (k: string, v: string) => void map.set(k, v),
    removeItem: (k: string) => void map.delete(k),
    map,
  };
}

describe("realtime token", () => {
  it("accepts only bounded subprotocol-safe tokens", () => {
    expect(validToken("a1b2c3d4e5f6")).toBe(true);
    expect(validToken("short")).toBe(false);
    expect(validToken("has space inside")).toBe(false);
    expect(validToken("comma,separated")).toBe(false);
    expect(validToken("x".repeat(129))).toBe(false);
  });

  it("offers the token as a second subprotocol only when valid", () => {
    expect(subprotocols("sentinel.v1", null)).toEqual(["sentinel.v1"]);
    expect(subprotocols("sentinel.v1", "bad token")).toEqual(["sentinel.v1"]);
    expect(subprotocols("sentinel.v1", "abcdef123456")).toEqual(["sentinel.v1", "sentinel.auth.abcdef123456"]);
  });

  it("persists and clears the token in storage", () => {
    const storage = memoryStorage();
    saveToken("abcdef123456", storage);
    expect(storage.map.get(TOKEN_KEY)).toBe("abcdef123456");
    expect(loadToken(storage)).toBe("abcdef123456");
    saveToken(null, storage);
    expect(loadToken(storage)).toBeNull();
  });

  it("derives the WebSocket URL from the page host", () => {
    const fallback = "ws://192.168.4.1:81/realtime";
    expect(defaultWsUrl({ protocol: "http:", hostname: "192.168.4.1" }, fallback)).toBe(fallback);
    expect(defaultWsUrl({ protocol: "http:", hostname: "10.0.0.7" }, fallback)).toBe("ws://10.0.0.7:81/realtime");
    expect(defaultWsUrl({ protocol: "http:", hostname: "localhost" }, fallback)).toBe(fallback);
    expect(defaultWsUrl(null, fallback)).toBe(fallback);
  });

  it("the client passes the token subprotocol when opening a socket", () => {
    const opened: string[][] = [];
    const client = new RealtimeClient({
      url: "ws://192.168.4.1:81/realtime",
      token: () => "abcdef123456",
      createSocket: (_url, protocols) => {
        opened.push(protocols);
        throw new Error("no network in tests");
      },
      scheduler: {
        setTimeout: () => 0,
        clearTimeout: () => undefined,
        setInterval: () => 0,
        clearInterval: () => undefined,
        now: () => 0,
      },
    });
    client.start();
    client.stop();
    expect(opened[0]).toEqual(["sentinel.v1", "sentinel.auth.abcdef123456"]);
  });
});
