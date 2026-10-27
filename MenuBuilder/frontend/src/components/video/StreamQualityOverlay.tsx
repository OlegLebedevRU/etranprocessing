import { useEffect, useState } from "react";
import type { RefObject } from "react";
import type { VideoReceiveStats } from "../../api/janusClient";
import type { RuntimeQuality } from "../../api/video";

interface Props {
  stream: string;
  profile?: string;
  codec?: string;
  videoRef: RefObject<HTMLVideoElement | null>;
  readStats: () => Promise<VideoReceiveStats | null>;
  quality?: RuntimeQuality | null;
}

/** Receiver observations only; no quality command, lease timer or HTTP polling. */
export function StreamQualityOverlay({ stream, profile, codec, videoRef, readStats, quality }: Props) {
  const [text, setText] = useState("Измерение потока…");
  useEffect(() => {
    let cancelled = false;
    let timer: ReturnType<typeof setTimeout> | undefined;
    let previous: { stats: VideoReceiveStats; total?: number; dropped?: number } | undefined;
    setText("Измерение потока…");
    const sample = async () => {
      try {
        const stats = await readStats();
        if (cancelled) return;
        const video = videoRef.current;
        const quality = video?.getVideoPlaybackQuality?.();
        const seconds = previous && stats && previous.stats.ssrc === stats.ssrc
          ? (stats.timestamp - previous.stats.timestamp) / 1000 : 0;
        const delta = (current?: number, before?: number) =>
          seconds > 0 && current !== undefined && before !== undefined && current >= before
            ? current - before : undefined;
        const bytes = delta(stats?.bytesReceived, previous?.stats.bytesReceived);
        const frames = delta(stats?.framesDecoded, previous?.stats.framesDecoded);
        const received = delta(stats?.packetsReceived, previous?.stats.packetsReceived);
        const lost = delta(stats?.packetsLost, previous?.stats.packetsLost);
        const total = delta(quality?.totalVideoFrames, previous?.total);
        const dropped = delta(quality?.droppedVideoFrames, previous?.dropped);
        const mbps = bytes !== undefined ? `${(bytes * 8 / seconds / 1e6).toFixed(2)} Мбит/с` : "— Мбит/с";
        const decode = frames !== undefined ? (frames / seconds).toFixed(1) : "—";
        const display = total !== undefined && dropped !== undefined ? ((total - dropped) / seconds).toFixed(1) : "—";
        const loss = received !== undefined && lost !== undefined && received + lost > 0
          ? `${(100 * lost / (received + lost)).toFixed(2)}%` : "—";
        const format = video?.videoWidth ? `${video.videoWidth}×${video.videoHeight}` : "Размер —";
        const activeCodec = (stats?.codec?.replace(/^video\//i, "") || codec || "Кодек —").toUpperCase();
        setText(`${profile === "default" ? "HD" : profile === "low" ? "Medium" : ""} · ${activeCodec} · ${format}\nПриём ${mbps} · потери ${loss}\nДекод ${decode} · показ ${display} кадр/с`);
        if (stats) previous = { stats, total: quality?.totalVideoFrames, dropped: quality?.droppedVideoFrames };
      } catch {
        if (!cancelled) setText("Метрики потока временно недоступны");
      } finally {
        if (!cancelled) timer = setTimeout(() => void sample(), 1000);
      }
    };
    void sample();
    return () => { cancelled = true; clearTimeout(timer); };
  }, [stream, profile, codec, videoRef, readStats]);
  return <div data-testid="stream-quality-overlay" title="Измерения браузера за последний интервал; приём RTP не равен целевому битрейту камеры"
    style={{ position: "absolute", top: 10, left: 10, zIndex: 5, pointerEvents: "none", whiteSpace: "pre-line", color: "#fff", background: "rgba(0,0,0,.65)", borderRadius: 5, padding: "5px 8px", fontSize: 11, lineHeight: 1.5, fontVariantNumeric: "tabular-nums" }}>{text}
    {quality?.available && `\nЦель ${quality.requested_kbps === 0 ? "Auto" : quality.requested_kbps ?? "—"} · кодировщик ${quality.applied_kbps ?? "—"} кбит/с${(quality.report_age_ms ?? Infinity) >= 3000 ? " · данные устарели" : ""}`}</div>;
}
