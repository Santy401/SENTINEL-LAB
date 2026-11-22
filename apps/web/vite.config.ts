import { fileURLToPath, URL } from "node:url";
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

// Dev server proxies the firmware HTTP API so the dashboard can run without the
// gateway. SENTINEL_DEVICE overrides the default laboratory AP address.
const device = process.env.SENTINEL_DEVICE ?? "http://192.168.4.1";

export default defineConfig({
  base: "./",
  plugins: [react()],
  resolve: { alias: { "@": fileURLToPath(new URL("./src", import.meta.url)) } },
  server: {
    proxy: {
      "/api": { target: device, changeOrigin: true },
    },
  },
  // The ESP32 serves exactly one script and one stylesheet (tools/embed_web.py,
  // firmware routes /assets/app.{js,css}), so dynamic imports (the three.js
  // board scene) are inlined into the single bundle instead of a second chunk.
  build: {
    outDir: "dist",
    sourcemap: false,
    chunkSizeWarningLimit: 1400,
    rolldownOptions: { output: { inlineDynamicImports: true } },
  },
});
