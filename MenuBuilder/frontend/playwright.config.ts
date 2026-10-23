import { defineConfig } from "@playwright/test";

const port = Number(process.env.MB_PLAYWRIGHT_PORT ?? "5182");
if (!Number.isInteger(port) || port < 1024 || port > 65535) throw new Error("Invalid MB_PLAYWRIGHT_PORT");
const baseURL = `http://127.0.0.1:${port}`;

export default defineConfig({
  testDir: "./e2e",
  fullyParallel: false,
  use: {
    baseURL,
    channel: process.platform === "win32" ? "msedge" : undefined,
  },
  webServer: {
    command: `npm run preview -- --host 127.0.0.1 --port ${port} --strictPort`,
    url: baseURL,
    reuseExistingServer: false,
  },
});
