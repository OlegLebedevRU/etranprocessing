import type { VideoReceiveStats } from "../api/janusClient";

export interface VideoTelemetrySample {
  receive: VideoReceiveStats;
  presentedFrames?: number;
  elementDroppedFrames?: number;
  visible: boolean;
}

export interface VideoTelemetryDelta {
  seconds: number;
  decodedFps: number | null;
  presentedFps: number | null;
  mbps: number | null;
  packetsLost: number | null;
  receiveDropped: number | null;
  elementDropped: number | null;
  freezes: number | null;
  decodeMs: number | null;
  jitterBufferMs: number | null;
}

function difference(after: number | undefined, before: number | undefined, signed = false): number | null {
  if (after === undefined || before === undefined || !Number.isFinite(after) || !Number.isFinite(before)) return null;
  const delta = after - before;
  return signed || delta >= 0 ? delta : null;
}

export function videoTelemetryDelta(before: VideoTelemetrySample | null, after: VideoTelemetrySample): VideoTelemetryDelta | null {
  if (!before || !before.visible || !after.visible || before.receive.id !== after.receive.id || before.receive.ssrc !== after.receive.ssrc) return null;
  const a = after.receive, b = before.receive;
  const seconds = (a.timestamp - b.timestamp) / 1000;
  if (!Number.isFinite(seconds) || seconds <= 0 || a.framesDecoded < b.framesDecoded) return null;
  const decoded = difference(a.framesDecoded, b.framesDecoded);
  const presented = difference(after.presentedFrames, before.presentedFrames);
  const bytes = difference(a.bytesReceived, b.bytesReceived);
  const decodeTime = difference(a.totalDecodeTime, b.totalDecodeTime);
  const bufferTime = difference(a.jitterBufferDelay, b.jitterBufferDelay);
  const emitted = difference(a.jitterBufferEmittedCount, b.jitterBufferEmittedCount);
  return {
    seconds,
    decodedFps: decoded === null ? null : decoded / seconds,
    presentedFps: presented === null ? null : presented / seconds,
    mbps: bytes === null ? null : bytes * 8 / seconds / 1e6,
    // RFC3550's loss estimate may decrease after late/duplicate packets.
    packetsLost: difference(a.packetsLost, b.packetsLost, true),
    receiveDropped: difference(a.framesDropped, b.framesDropped),
    elementDropped: difference(after.elementDroppedFrames, before.elementDroppedFrames),
    freezes: difference(a.freezeCount, b.freezeCount),
    decodeMs: decodeTime !== null && decoded !== null && decoded > 0 ? decodeTime * 1000 / decoded : null,
    jitterBufferMs: bufferTime !== null && emitted !== null && emitted > 0 ? bufferTime * 1000 / emitted : null,
  };
}
