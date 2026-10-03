import { defineConfig } from "@playwright/test";

export default defineConfig({
  testDir: "./e2e",
  fullyParallel: false,
  use: {
    baseURL: "http://127.0.0.1:5182",
    channel: process.platform === "win32" ? "msedge" : undefined,
  },
  webServer: {
    command: "npm run preview -- --host 127.0.0.1 --port 5182",
    url: "http://127.0.0.1:5182",
    reuseExistingServer: false,
  },
});
