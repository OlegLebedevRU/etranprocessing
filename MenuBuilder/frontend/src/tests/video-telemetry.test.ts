import { describe, expect, it } from "vitest";
import { videoTelemetryDelta, type VideoTelemetrySample } from "../utils/videoTelemetry";

const sample = (timestamp: number, framesDecoded: number, extra = {}): VideoTelemetrySample => ({
  receive: { id: "video", ssrc: 42, timestamp, framesDecoded, ...extra }, visible: true,
});

describe("video interval telemetry", () => {
  it("separates packet loss, decoder drops and video element drops", () => {
    const before = { ...sample(1000, 20, { bytesReceived: 1000, packetsLost: 8, framesDropped: 2, freezeCount: 1, totalDecodeTime: .2, jitterBufferDelay: 1, jitterBufferEmittedCount: 20 }), presentedFrames: 18, elementDroppedFrames: 2 };
    const after = { ...sample(3000, 40, { bytesReceived: 501000, packetsLost: 6, framesDropped: 5, freezeCount: 2, totalDecodeTime: .3, jitterBufferDelay: 1.8, jitterBufferEmittedCount: 40 }), presentedFrames: 33, elementDroppedFrames: 7 };
    expect(videoTelemetryDelta(before, after)).toEqual({ seconds: 2, decodedFps: 10, presentedFps: 7.5, mbps: 2, packetsLost: -2, receiveDropped: 3, elementDropped: 5, freezes: 1, decodeMs: expect.closeTo(5), jitterBufferMs: 40 });
  });
  it("does not manufacture missing counters or zero-divide stalled decode", () => {
    const delta = videoTelemetryDelta(sample(1000, 0), sample(2000, 0));
    expect(delta?.decodedFps).toBe(0);
    for (const key of ["presentedFps", "mbps", "packetsLost", "decodeMs", "jitterBufferMs"] as const) expect(delta?.[key]).toBeNull();
  });
  it("rebaselines on stream reset, identity, clock and visibility changes", () => {
    const before = sample(2000, 20);
    for (const after of [sample(3000, 1), sample(2000, 30), sample(1000, 30), sample(3000, 30, { id: "other" }), sample(3000, 30, { ssrc: 43 }), { ...sample(3000, 30), visible: false }]) expect(videoTelemetryDelta(before, after)).toBeNull();
    expect(videoTelemetryDelta({ ...before, visible: false }, sample(3000, 30))).toBeNull();
  });
});
