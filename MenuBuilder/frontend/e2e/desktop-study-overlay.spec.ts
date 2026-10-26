import { expect, test } from "@playwright/test";
import { installStudyOverlay } from "./diagnostics/study-overlay.mjs";

test("test overlay is injected explicitly, separates counters and disposes", async ({ page }) => {
  await page.setContent("<video muted></video>");
  await page.clock.install();
  await expect(page.getByTestId("video-telemetry")).toHaveCount(0);
  await page.evaluate(() => {
    let calls = 0;
    (window as any).__desktopStudy = { peers: [{ connectionState: "connected", getStats: async () => {
      const n = calls++;
      return new Map([
        ["codec", { mimeType: "video/H265" }],
        ["video", { type: "inbound-rtp", kind: "video", id: "video", ssrc: 1, codecId: "codec", timestamp: 1000 + n * 1000, framesDecoded: n * 10, bytesReceived: n * 250000, packetsLost: 10 - n, framesDropped: n * 2, freezeCount: n, totalDecodeTime: n * .02, jitterBufferDelay: n * .5, jitterBufferEmittedCount: n * 10 }],
      ]);
    } }] };
  });
  await page.evaluate(installStudyOverlay, { display: "DISPLAY2" });
  const overlay = page.getByTestId("video-telemetry");
  await expect(overlay).toContainText("Ожидание интервала измерения");
  await page.clock.fastForward(1000);
  await expect(overlay).toContainText("Δ RTP lost: -1 · Δ receive drop: 2");
  await expect(overlay).toContainText("2.00 Mbps");
  await expect(overlay).toContainText("Decode: 2.00 ms · Jitter buffer: 50.0 ms");
  await overlay.getByRole("button").click();
  await expect(overlay.getByRole("button")).toHaveAttribute("aria-expanded", "false");
  await page.evaluate(() => (window as any).__desktopStudy.disposeOverlay());
  await page.clock.fastForward(5000);
  await expect(overlay).toHaveCount(0);
});

test("test overlay keeps unavailable counters missing", async ({ page }) => {
  await page.setContent("<video muted></video>");
  await page.evaluate(() => { (window as any).__desktopStudy = { peers: [] }; });
  await page.evaluate(installStudyOverlay, { display: "DISPLAY1" });
  await expect(page.getByTestId("video-telemetry")).toContainText("Статистика недоступна\nΔ RTP lost: —");
});
