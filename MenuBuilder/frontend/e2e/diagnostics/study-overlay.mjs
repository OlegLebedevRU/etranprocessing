/** Injected by the explicit diagnostic harness only; never imported by src. */
export function installStudyOverlay({ display }) {
  window.__desktopStudy?.disposeOverlay?.();
  const video = document.querySelector("video");
  if (!video) throw Error("Study video missing");
  const panel = document.createElement("aside");
  panel.dataset.testid = "video-telemetry";
  panel.setAttribute("aria-label", "Параметры тестовой трансляции");
  panel.style.cssText = "position:fixed;left:12px;top:120px;z-index:10000;max-width:calc(100vw - 24px);color:white;background:rgba(0,0,0,.85);padding:8px;border-radius:6px;font:12px/1.6 monospace;pointer-events:none";
  const toggle = document.createElement("button");
  toggle.textContent = "Параметры тестовой трансляции";
  toggle.style.cssText = "color:inherit;background:transparent;border:0;cursor:pointer;pointer-events:auto;font:inherit";
  toggle.setAttribute("aria-expanded", "true");
  const text = document.createElement("div");
  text.style.whiteSpace = "pre-wrap";
  toggle.onclick = () => { text.hidden = !text.hidden; toggle.setAttribute("aria-expanded", String(!text.hidden)); };
  panel.append(toggle, text);
  document.body.append(panel);
  let previous = null, presented, callback = 0, busy = false, disposed = false;
  let lastCompleted = performance.now();
  const delta = (a, b, signed = false) => typeof a === "number" && typeof b === "number" && Number.isFinite(a) && Number.isFinite(b) && (signed || a >= b) ? a - b : null;
  const format = (value, digits = 1) => value == null ? "—" : value.toFixed(digits);
  const frame = (_now, metadata) => { if (!disposed) { presented = metadata.presentedFrames; callback = video.requestVideoFrameCallback(frame); } };
  if (typeof video.requestVideoFrameCallback === "function") callback = video.requestVideoFrameCallback(frame);
  const visibility = () => { previous = null; };
  document.addEventListener("visibilitychange", visibility);
  const poll = async () => {
    if (disposed || busy) return;
    busy = true;
    try {
      const pc = window.__desktopStudy.peers.find(p => p.connectionState === "connected");
      const reports = pc ? await pc.getStats() : null;
      if (disposed) return;
      const raw = reports && [...reports.values()].find(r => r.type === "inbound-rtp" && (r.kind === "video" || r.mediaType === "video") && typeof r.framesDecoded === "number");
      if (!raw) { previous = null; text.textContent = "Статистика недоступна\nΔ RTP lost: —"; return; }
      const next = { raw, presented, dropped: video.getVideoPlaybackQuality?.().droppedVideoFrames, visible: document.visibilityState === "visible" };
      const before = previous;
      previous = next;
      const seconds = before ? (raw.timestamp - before.raw.timestamp) / 1000 : 0;
      const valid = before && before.visible && next.visible && before.raw.id === raw.id && before.raw.ssrc === raw.ssrc && raw.framesDecoded >= before.raw.framesDecoded && seconds > 0;
      const d = key => valid ? delta(raw[key], before.raw[key], key === "packetsLost") : null;
      const rate = value => value == null ? null : value / seconds;
      const decoded = d("framesDecoded"), emitted = d("jitterBufferEmittedCount");
      const decodeTime = d("totalDecodeTime"), bufferTime = d("jitterBufferDelay"), bytes = d("bytesReceived");
      const codec = reports.get(raw.codecId)?.mimeType || "—";
      text.textContent = `${display} · ${codec} · ${video.videoWidth || "—"}×${video.videoHeight || "—"}\n`
        + `FPS decode / показ: ${format(rate(decoded))} / ${format(valid ? rate(delta(presented, before.presented)) : null)} · ${format(bytes == null ? null : bytes * 8 / seconds / 1e6, 2)} Mbps\n`
        + `Δ RTP lost: ${format(d("packetsLost"), 0)} · Δ receive drop: ${format(d("framesDropped"), 0)}\n`
        + `Δ video drop: ${format(valid ? delta(next.dropped, before.dropped) : null, 0)} · Δ freeze: ${format(d("freezeCount"), 0)}\n`
        + `Decode: ${format(decoded > 0 && decodeTime != null ? decodeTime * 1000 / decoded : null, 2)} ms · Jitter buffer: ${format(emitted > 0 && bufferTime != null ? bufferTime * 1000 / emitted : null)} ms\n`
        + (valid ? `Интервал ${format(seconds)} с · показ = rVFC` : "Ожидание интервала измерения");
    } catch {
      if (!disposed) { previous = null; text.textContent = "Статистика недоступна\nΔ RTP lost: —"; }
    } finally { busy = false; lastCompleted = performance.now(); }
  };
  void poll();
  const timer = setInterval(() => {
    if (performance.now() - lastCompleted > 5000) { previous = null; text.textContent = "Статистика недоступна"; }
    void poll();
  }, 1000);
  window.__desktopStudy.disposeOverlay = () => {
    disposed = true; clearInterval(timer);
    if (callback) video.cancelVideoFrameCallback(callback);
    document.removeEventListener("visibilitychange", visibility);
    panel.remove();
  };
}
