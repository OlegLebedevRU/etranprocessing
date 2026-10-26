import type { RuntimeQuality } from "../api/video";
import type { VideoReceiveStats } from "../api/janusClient";

/** Only the publisher runs this policy. Transport reports never issue commands.
 * Cut 25% on queue pressure, growing receiver delay or >2% RTP loss; recover 10% of the selected ceiling
 * after five clear seconds. Decoder/compositor drops do not measure network loss.
 */
export class DesktopQualityPolicy {
  private previous: VideoReceiveStats | null = null;
  private clearSince: number | null = null;
  private lastChange = -Infinity;
  private receiverDelayMs = 0;

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
    const lost = stats && previous && stats.packetsLost !== undefined && previous.packetsLost !== undefined
      ? Math.max(0, stats.packetsLost - previous.packetsLost) : 0;
    const validNetwork = !!stats && !!previous && stats.timestamp > previous.timestamp && received >= 0
      && stats.packetsReceived !== undefined && previous.packetsReceived !== undefined
      && stats.packetsLost !== undefined && previous.packetsLost !== undefined;
    const loss = validNetwork && lost > 0 && lost / (received + lost) > 0.02;
    if (!quality.available || (quality.report_age_ms ?? Infinity) >= 3000
      || quality.status === "unsupported"
      || !quality.applied_kbps || !quality.ceiling_kbps) {
      this.clearSince = null;
      return null;
    }
    const pressure = (quality.transport_available && quality.transport_pressure === true)
      || this.receiverDelayMs > 200;
    if (pressure || loss) this.clearSince = null;
    if (quality.status === "pending" || now - this.lastChange < 1000) return null;
    const current = quality.applied_kbps, ceiling = quality.ceiling_kbps;
    let target: number | null = null;
    if (pressure || loss) {
      target = Math.max(100, Math.floor(current * 0.75));
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
