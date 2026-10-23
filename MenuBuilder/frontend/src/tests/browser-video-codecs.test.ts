import { afterEach, expect, it, vi } from "vitest";
import { browserVideoCodecs } from "../utils/browserVideoCodecs";

afterEach(() => vi.unstubAllGlobals());

it("reports actual browser codec support with a bounded observation lifetime", () => {
  vi.stubGlobal("RTCRtpReceiver", { getCapabilities: () => ({ codecs: [{ mimeType: "video/H264" }, { mimeType: "video/H265" }] }) });
  const value = browserVideoCodecs();
  expect(value.h264).toBe(true); expect(value.h265).toBe(true);
  expect(value.valid_until_ms - value.checked_at_ms).toBe(60000);
});

it("keeps missing capability APIs unknown rather than authorizing fallback", () => {
  vi.stubGlobal("RTCRtpReceiver", undefined);
  expect(browserVideoCodecs()).toMatchObject({ h264: null, h265: null });
});

it("distinguishes confirmed absence of HEVC from an unknown capability", () => {
  vi.stubGlobal("RTCRtpReceiver", { getCapabilities: () => ({ codecs: [{ mimeType: "video/H264" }] }) });
  expect(browserVideoCodecs()).toMatchObject({ h264: true, h265: false });
});
