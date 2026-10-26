import type { RuntimeQuality } from "../api/video";
import type { VideoReceiveStats } from "../api/janusClient";

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

  observe(quality: RuntimeQuality, stats: VideoReceiveStats | null, now: number): number | null {
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
    if (!quality.available || (quality.report_age_ms ?? Infinity) >= 3000
      || quality.status === "unsupported"
      || !quality.applied_kbps || !quality.ceiling_kbps) {
      this.clearSince = null;
      return null;
    }
    if (this.appliedKbps !== quality.applied_kbps) {
      // Assess new queue growth after an acknowledged actuator change. A
      // receiver can retain a fixed playout offset after its queue has drained;
      // repeatedly cutting for that old offset would pin quality at the floor.
      this.receiverDelayMs = 0;
      this.appliedKbps = quality.applied_kbps;
    }
    const pressure = (quality.transport_available && quality.transport_pressure === true)
      || this.receiverDelayMs > 200;
    if (pressure) this.clearSince = null;
    if (quality.status === "pending" || now - this.lastChange < 1000) return null;
    const current = quality.applied_kbps, ceiling = quality.ceiling_kbps;
    let target: number | null = null;
    if (pressure) {
      target = Math.max(100, Math.floor(current * 0.5));
      if (target === current) this.receiverDelayMs = 0; // No actuator headroom: avoid retaining an old offset forever.
    } else if (quality.transport_available && validNetwork && received > 0
      && previous && stats && stats.framesDecoded > previous.framesDecoded) {
      this.clearSince ??= now;
      if (now - this.clearSince >= 5000) {
        target = Math.min(ceiling, current + Math.max(1, Math.floor(ceiling * 0.1)));
        this.clearSince = now;
      }
    } else this.clearSince = null;
    if (target === null || target === current) return null;
    this.lastChange = now;
    return target;
  }
}
