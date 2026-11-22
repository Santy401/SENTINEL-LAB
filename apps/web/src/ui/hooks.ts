import { useEffect, useState } from "react";

function query(media: string): boolean {
  try {
    return typeof window !== "undefined" && typeof window.matchMedia === "function" && window.matchMedia(media).matches;
  } catch {
    return false;
  }
}

/** Live prefers-reduced-motion flag. */
export function useReducedMotion(): boolean {
  const media = "(prefers-reduced-motion: reduce)";
  const [reduced, setReduced] = useState(() => query(media));
  useEffect(() => {
    if (typeof window === "undefined" || typeof window.matchMedia !== "function") return;
    const list = window.matchMedia(media);
    const onChange = () => setReduced(list.matches);
    list.addEventListener?.("change", onChange);
    return () => list.removeEventListener?.("change", onChange);
  }, []);
  return reduced;
}

/** Whether a WebGL context can be created; false on the server and in tests. */
export function detectWebGL(): boolean {
  try {
    if (typeof document === "undefined") return false;
    const canvas = document.createElement("canvas");
    return !!(canvas.getContext("webgl2") || canvas.getContext("webgl"));
  } catch {
    return false;
  }
}

/** Current theme from <html data-theme>. */
export function useDarkTheme(): boolean {
  const read = () => typeof document !== "undefined" && document.documentElement.dataset.theme === "dark";
  const [dark, setDark] = useState(read);
  useEffect(() => {
    if (typeof MutationObserver === "undefined") return;
    const observer = new MutationObserver(() => setDark(read()));
    observer.observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme"] });
    return () => observer.disconnect();
  }, []);
  return dark;
}

/** Re-renders every `ms` so "age" readouts stay current. */
export function useNow(ms: number): number {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    const id = setInterval(() => setNow(Date.now()), ms);
    return () => clearInterval(id);
  }, [ms]);
  return now;
}
