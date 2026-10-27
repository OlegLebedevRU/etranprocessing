import type { RuntimeQuality } from "../api/video";
import type { VideoReceiveStats } from "../api/janusClient";

export interface DesktopQualityTarget { kbps: number; rasterStep?: number }

/** Only the publisher runs this policy. Transport reports never issue commands.
 * Halve on queue pressure/delay; recover 10% of the selected ceiling
 * after five clear seconds. Decoder/compositor drops do not measure network loss.
 */
export class DesktopQualityPolicy {
  private previous: VideoReceiveStats | null = null;
  private clearSince: number | null = null;
  private lastChange = -Infinity;
  private receiverDelayMs = 0;
  private appliedKbps: number | null = null;
  private appliedStep: number | null = null;
  private raisedStep: number | null = null;
  private rasterRetryMs = 5000;
  private rasterFailedAt = -Infinity;
  private incompleteSince: number | null = null;

  observe(quality: RuntimeQuality, stats: VideoReceiveStats | null, now: number): number | null {
    return this.observeTarget(quality, stats, now)?.kbps ?? null;
  }

  observeTarget(quality: RuntimeQuality, stats: VideoReceiveStats | null, now: number): DesktopQualityTarget | null {
    const previous = this.previous;
    this.previous = stats;
    // Compare actual track delivery with the 90 kHz source clock. Video element
    // mediaTime can be rebased by Chrome and conceal seconds of queued video.
    // This measures accumulated delay, independently of packet loss or drops.
    if (!stats || !previous || stats.ssrc !== previous.ssrc) this.receiverDelayMs = 0;
    else if (stats.sourceTimestamp !== undefined && previous.sourceTimestamp !== undefined
      && stats.sourceRtpTimestamp !== undefined && previous.sourceRtpTimestamp !== undefined) {
      const elapsed = stats.sourceTimestamp - previous.sourceTimestamp;
      const media = ((stats.sourceRtpTimestamp - previous.sourceRtpTimestamp) | 0) / 90;
      if (elapsed < 0 || media < 0) this.receiverDelayMs = 0;
      else if (elapsed > 0 && media > 0) this.receiverDelayMs = Math.max(0, this.receiverDelayMs + elapsed - media);
    } else this.receiverDelayMs = 0;
    const received = stats && previous && stats.packetsReceived !== undefined && previous.packetsReceived !== undefined
      ? stats.packetsReceived - previous.packetsReceived : 0;
    const validNetwork = !!stats && !!previous && stats.timestamp > previous.timestamp && received >= 0
      && stats.packetsReceived !== undefined && previous.packetsReceived !== undefined
      && stats.packetsLost !== undefined && previous.packetsLost !== undefined;
    // framesReceived counts complete assembled frames before decoding (W3C).
    // Continued RTP arrival without a complete frame can expose a large IDR
    // before the first decode. Missing data, idle sources, new loss and decoder
    // stalls alone do not establish this assembly-delay measurement.
    if (!stats || !previous || stats.ssrc !== previous.ssrc || stats.timestamp <= previous.timestamp) {
      this.incompleteSince = stats?.framesReceived === 0 && (stats.packetsReceived ?? 0) > 0 ? now : null;
    } else if (validNetwork && received > 0 && stats.framesReceived !== undefined
      && stats.framesReceived === previous.framesReceived && stats.packetsLost! <= previous.packetsLost!) {
      this.incompleteSince ??= now;
    } else this.incompleteSince = null;
    if (!quality.available || (quality.report_age_ms ?? Infinity) >= 3000
      || quality.status === "unsupported"
      || !quality.applied_kbps || !quality.ceiling_kbps) {
      this.clearSince = null;
      return null;
    }
    const step = quality.raster_available ? quality.raster_step ?? 0 : 0;
    if (this.appliedKbps !== quality.applied_kbps || this.appliedStep !== step) {
      // Assess new queue growth after an acknowledged actuator change. A
      // receiver can retain a fixed playout offset after its queue has drained;
      // repeatedly cutting for that old offset would pin quality at the floor.
      this.receiverDelayMs = 0;
      this.appliedKbps = quality.applied_kbps;
      this.appliedStep = step;
      this.clearSince = null;
    }
    const pressure = (quality.transport_available && quality.transport_pressure === true)
      || this.receiverDelayMs > 200 || (this.incompleteSince !== null && now - this.incompleteSince > 200);
    if (pressure) this.clearSince = null;
    if (quality.status === "pending" || now - this.lastChange < 1000) return null;
    const current = quality.applied_kbps, ceiling = quality.ceiling_kbps;
    let target: number | null = null;
    let targetStep = step;
    if (pressure) {
      if (this.raisedStep !== null && step <= this.raisedStep) {
        this.rasterFailedAt = now;
        // Operator-approved compromise: repeated failures reduce probe frequency,
        // but never impose minutes of extra cooldown after the link recovers.
        this.rasterRetryMs = Math.min(30000, this.rasterRetryMs * 2);
        this.raisedStep = null;
      }
      target = Math.max(100, Math.floor(current * 0.5));
      // One half-size raster has one quarter of the pixels. A known large IDR
      // needs that reduction immediately: lowering mean bitrate cannot remove
      // a keyframe already queued on the link. Use the same 200 ms queue budget,
      // not a second arbitrary threshold or a hardware-specific alignment.
      const oversizedIdr = (quality.last_idr_bytes ?? 0) * 8 > target * 200;
      if (quality.raster_available && quality.initial_width && quality.initial_height
        && (oversizedIdr || target <= ceiling / 4 ** (step + 1)) && ceiling / 4 ** (step + 1) >= 100
        && Math.floor(quality.initial_width / 2 ** (step + 1) / 2) >= 1
        && Math.floor(quality.initial_height / 2 ** (step + 1) / 2) >= 1) targetStep++;
      if (target === current) this.receiverDelayMs = 0; // No actuator headroom: avoid retaining an old offset forever.
    } else if (quality.transport_available && validNetwork && received > 0
      && previous && stats && stats.framesDecoded > previous.framesDecoded) {
      this.clearSince ??= now;
      if (now - this.clearSince >= 5000) {
        // Restore the rate budget for the NEXT larger raster before probing it.
        // Four times as many pixels at the old quarter-rate budget can starve
        // an encoder and produce a large IDR even after the network recovers.
        const nextRasterBudget = Math.max(100, Math.floor(ceiling / 4 ** Math.max(0, step - 1)));
        if (step > 0 && current >= nextRasterBudget) {
          if (now - this.rasterFailedAt >= this.rasterRetryMs) {
            target = current; targetStep--;
            this.raisedStep = targetStep;
          }
        } else target = Math.min(nextRasterBudget, current + Math.max(1, Math.floor(ceiling * 0.1)));
        this.clearSince = now;
      }
    } else this.clearSince = null;
    if (target === null || (target === current && targetStep === step)) return null;
    this.lastChange = now;
    return { kbps: target, ...(targetStep === step ? {} : { rasterStep: targetStep }) };
  }
}
