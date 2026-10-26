import { useEffect, useRef, useState } from "react";
import type { VideoReceiveStats } from "../../api/janusClient";
import { videoTelemetryDelta, type VideoTelemetryDelta, type VideoTelemetrySample } from "../../utils/videoTelemetry";

export interface VideoTelemetryOverlayProps {
  videoRef: React.RefObject<HTMLVideoElement | null>;
  readStats: () => Promise<VideoReceiveStats | null>;
  sourceLabel?: string;
}

const number = (value: number | null | undefined, digits = 1) => value == null ? "—" : value.toFixed(digits);

/** Receiver observation only. Never changes source policy, jitter target or lease. */
export function VideoTelemetryOverlay({ videoRef, readStats, sourceLabel }: VideoTelemetryOverlayProps) {
  const reader = useRef(readStats);
  reader.current = readStats;
  const [collapsed, setCollapsed] = useState(false);
  const [sample, setSample] = useState<{ raw: VideoReceiveStats; delta: VideoTelemetryDelta | null } | null>(null);
  const [available, setAvailable] = useState(true);

  useEffect(() => {
    const video = videoRef.current;
    if (!video) return;
    let cancelled = false, busy = false, callback = 0;
    let lastCompleted = performance.now();
    let presented: number | undefined;
    let previous: VideoTelemetrySample | null = null;
    const frame: VideoFrameRequestCallback = (_now, metadata) => {
      if (cancelled) return;
      presented = metadata.presentedFrames;
      callback = video.requestVideoFrameCallback(frame);
    };
    if (typeof video.requestVideoFrameCallback === "function") callback = video.requestVideoFrameCallback(frame);
    const visibility = () => { previous = null; setSample(null); };
    document.addEventListener("visibilitychange", visibility);
    const poll = async () => {
      if (cancelled || busy) return;
      busy = true;
      try {
        const raw = await reader.current();
        if (cancelled) return;
        if (!raw) { previous = null; setSample(null); setAvailable(false); return; }
        const quality = video.getVideoPlaybackQuality?.();
        const next = { receive: raw, presentedFrames: presented, elementDroppedFrames: quality?.droppedVideoFrames, visible: document.visibilityState === "visible" };
        const delta = videoTelemetryDelta(previous, next);
        previous = next;
        setSample({ raw, delta });
        setAvailable(true);
      } catch {
        // Stats failures must not interrupt media playback or renewals.
        if (!cancelled) { previous = null; setSample(null); setAvailable(false); }
      } finally { busy = false; lastCompleted = performance.now(); }
    };
    void poll();
    const timer = window.setInterval(() => {
      if (performance.now() - lastCompleted > 5000) { previous = null; setSample(null); setAvailable(false); }
      void poll();
    }, 1000);
    return () => {
      cancelled = true;
      window.clearInterval(timer);
      if (callback) video.cancelVideoFrameCallback(callback);
      document.removeEventListener("visibilitychange", visibility);
    };
  }, [videoRef]);

  const d = sample?.delta;
  return <aside data-testid="video-telemetry" aria-label="Параметры трансляции" style={{ position: "absolute", top: 8, left: 8, zIndex: 21, maxWidth: "calc(100% - 16px)", color: "#fff", background: "rgba(0,0,0,.78)", borderRadius: 6, font: "12px/1.6 monospace", pointerEvents: "none" }}>
    <button type="button" aria-expanded={!collapsed} onClick={() => setCollapsed(value => !value)} style={{ pointerEvents: "auto", cursor: "pointer", border: 0, background: "transparent", color: "inherit", padding: "4px 8px", font: "inherit" }}>Параметры трансляции {collapsed ? "+" : "−"}</button>
    {!collapsed && <div style={{ padding: "0 8px 6px", whiteSpace: "pre-wrap" }}>
      <div>{sourceLabel || "Источник"} · {sample?.raw.codec || "—"} · {videoRef.current?.videoWidth || "—"}×{videoRef.current?.videoHeight || "—"}</div>
      <div>FPS decode / показ: {number(d?.decodedFps)} / {number(d?.presentedFps)} · {number(d?.mbps, 2)} Mbps</div>
      <div title="RTP lost — оценка потерь пакетов; может уменьшаться после поздних пакетов. Receive drop — отдельный счётчик входящего видео.">Δ RTP lost: {number(d?.packetsLost, 0)} · Δ receive drop: {number(d?.receiveDropped, 0)}</div>
      <div>Δ video drop: {number(d?.elementDropped, 0)} · Δ freeze: {number(d?.freezes, 0)}</div>
      <div>Decode: {number(d?.decodeMs, 2)} ms · Jitter buffer: {number(d?.jitterBufferMs, 1)} ms</div>
      <div>{!available ? "Статистика недоступна" : !d ? "Ожидание интервала измерения" : `Интервал ${number(d.seconds)} с · показ = rVFC`}</div>
    </div>}
  </aside>;
}
