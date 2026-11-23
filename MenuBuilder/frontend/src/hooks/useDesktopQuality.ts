import { onChange, onQuality } from "../utils/portalChanges";
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

// Reports can arrive every second. An acknowledged command needs its own immediate read.
export function qualityReadDue(age: number, hinted: boolean, afterCommand: boolean) {
  return afterCommand || age >= 15000 || (hinted && age >= 5000);
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
    let cached: RuntimeQuality | undefined, readAt = 0, revision = 0, observed = -1;
    let readAfterCommand = false;
    let readRetryAt = 0;
    const off = onChange(device, ["quality"], () => { revision++; });
    let latest = 0;
    const unsubscribe = onQuality((id, epoch, at, value) => {
      if (id !== device || epoch !== stream || at <= latest) return;
      latest = at;
      cached = value;
      observed = revision;
      readAt = performance.now();
      readRetryAt = 0;
      setQuality(value);
    });
    const tick = async () => {
      try {
        const age = performance.now() - readAt;
        if (performance.now() < readRetryAt) return;
        if (!cached || qualityReadDue(age, observed !== revision, readAfterCommand)) {
          const before = revision;
          readRetryAt = performance.now() + 5000;
          const start = latest;
          const snapshot = await getRuntimeQuality(device, stream, abort.signal);
          if (start === latest) {
            cached = snapshot;
            observed = before;
            readAt = performance.now();
          }
          readRetryAt = 0;
          readAfterCommand = false;
        }
        const value = cached;
        if (abort.signal.aborted || !value) return;
        setQuality(value);
        if (writer) {
          // A camera starts read-only. The original initiator explicitly grants
          // source adaptation using the same quality command and native target;
          // no new handshake or mutation is implied by START-only admission.
          if (value.quality_revision && value.available && value.generation === 0
            && value.applied_kbps && value.status !== "pending" && value.status !== "unsupported") {
            readAfterCommand = true;
            await setRuntimeQuality(device, lease!, stream, value.requested_kbps ?? 0,
              writerKey!, abort.signal, undefined, value.quality_revision);
            return;
          }
          const stats = await readStats();
          if (abort.signal.aborted) return;
          const target = policy.observeTarget(value, stats, performance.now());
          if (target !== null) { readAfterCommand = true; await setRuntimeQuality(device, lease!, stream, target.kbps,
            writerKey!, abort.signal, target.rasterStep, value.quality_revision); }
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
    return () => { off(); unsubscribe(); abort.abort(); clearTimeout(timer); };
  }, [device, lease, stream, publisher, writerKey, readStats]);
  return quality;
}
