import { useEffect, useState } from "react";
import { getRuntimeQuality, setRuntimeQuality } from "../api/video";
import type { RuntimeQuality } from "../api/video";
import type { VideoReceiveStats } from "../api/janusClient";
import { DesktopQualityPolicy } from "../utils/desktopQuality";

export interface DesktopQualitySession {
  device: number;
  lease: string;
  stream: string;
  publisher: boolean;
  writerKey?: string;
}

export function useDesktopQuality(session: DesktopQualitySession | null,
  readStats: () => Promise<VideoReceiveStats | null>) {
  const [quality, setQuality] = useState<RuntimeQuality | null>(null);
  const device = session?.device, lease = session?.lease, stream = session?.stream,
    publisher = session?.publisher, writerKey = session?.writerKey;
  useEffect(() => {
    setQuality(null);
    if (device === undefined || !stream) return;
    const abort = new AbortController();
    const policy = new DesktopQualityPolicy();
    let timer: ReturnType<typeof setTimeout> | undefined;
    let writer = !!publisher && !!lease && !!writerKey;
    const tick = async () => {
      try {
        const value = await getRuntimeQuality(device, stream, abort.signal);
        if (abort.signal.aborted) return;
        setQuality(value);
        if (writer) {
          const stats = await readStats();
          if (abort.signal.aborted) return;
          const target = policy.observeTarget(value, stats, performance.now());
          if (target !== null) await setRuntimeQuality(device, lease!, stream, target.kbps,
            writerKey!, abort.signal, target.rasterStep);
        }
      } catch (error) {
        const status = (error as { response?: { status?: number } })?.response?.status;
        // Lost authority cannot turn a viewing tab into a quality writer.
        if (status === 403 || status === 409) writer = false;
        if (status === 404) abort.abort(); // Older server: ordinary streaming continues.
      } finally {
        if (!abort.signal.aborted) timer = setTimeout(() => void tick(), 1000);
      }
    };
    void tick();
    return () => { abort.abort(); clearTimeout(timer); };
  }, [device, lease, stream, publisher, writerKey, readStats]);
  return quality;
}
