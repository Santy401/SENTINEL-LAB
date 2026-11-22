// Realtime token for the browser. Browsers cannot send Authorization on a
// WebSocket, so the token travels as the "sentinel.auth.<token>" subprotocol
// offer next to "sentinel.v1" (the firmware only ever selects sentinel.v1).
// Stored only in this browser; never part of settings, evidence or logs.

export const TOKEN_KEY = "sentinel.realtimeToken";
export const AUTH_PROTOCOL_PREFIX = "sentinel.auth.";

/** RFC 7230 token characters accepted in a subprotocol, bounded length. */
export function validToken(token: string): boolean {
  return /^[A-Za-z0-9._~-]{8,128}$/.test(token);
}

export function loadToken(storage: Pick<Storage, "getItem"> | null = safeStorage()): string | null {
  try {
    const value = storage?.getItem(TOKEN_KEY) ?? null;
    return value && validToken(value) ? value : null;
  } catch {
    return null;
  }
}

export function saveToken(token: string | null, storage: Pick<Storage, "setItem" | "removeItem"> | null = safeStorage()): void {
  try {
    if (token && validToken(token)) storage?.setItem(TOKEN_KEY, token);
    else storage?.removeItem(TOKEN_KEY);
  } catch {
    // Private mode or blocked storage: the token simply is not remembered.
  }
}

export function subprotocols(base: string, token: string | null): string[] {
  return token && validToken(token) ? [base, AUTH_PROTOCOL_PREFIX + token] : [base];
}

/**
 * WebSocket URL when no gateway answered /gateway/config: the host that served
 * this page (the ESP32 itself at 192.168.4.1), or the AP default for local dev.
 */
export function defaultWsUrl(location: { protocol: string; hostname: string } | null, fallback: string): string {
  if (!location || location.protocol !== "http:") return fallback;
  const host = location.hostname;
  if (!host || host === "localhost" || host === "127.0.0.1" || host === "::1") return fallback;
  return `ws://${host.includes(":") ? `[${host}]` : host}:81/realtime`;
}

function safeStorage(): Storage | null {
  try {
    return typeof localStorage === "undefined" ? null : localStorage;
  } catch {
    return null;
  }
}
