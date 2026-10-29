// Raw traces stay private. This emits counter summaries, never credentials/URLs.
import { readFile, readdir, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
const directory = resolve(process.argv[2] || ".pytest_tmp/desktop-performance");
const quantiles = values => {
  const v = values.filter(Number.isFinite).sort((a, b) => a - b);
  return v.length ? { n: v.length, mean: v.reduce((a, b) => a + b, 0) / v.length, p50: v[Math.floor((v.length - 1) * .5)], p95: v[Math.floor((v.length - 1) * .95)], max: v.at(-1) } : null;
};
const intervals = events => quantiles(events.slice(1).map((e, i) => (e.ts - events[i].ts) / 1000));
const summaries = [];
for (const name of (await readdir(directory)).filter(n => /^(A1|B|A2|long|late)-DISPLAY[12]\.json$/.test(n)).sort()) {
  const run = JSON.parse(await readFile(resolve(directory, name), "utf8"));
  const first = run.samples[0], last = run.samples.at(-1);
  const a = first.receivers[0], b = last.receivers[0];
  if (!a || !b || a.id !== b.id || a.ssrc !== b.ssrc) throw Error(`Receiver changed: ${name}`);
  const seconds = (b.timestamp - a.timestamp) / 1000;
  const diff = key => typeof a[key] === "number" && typeof b[key] === "number" ? b[key] - a[key] : null;
  const decoded = diff("framesDecoded"), emitted = diff("jitterBufferEmittedCount");
  const frames = run.frames;
  const frameSeconds = (frames.at(-1).now - frames[0].now) / 1000;
  const trace = JSON.parse(await readFile(resolve(directory, name.replace(".json", ".trace.json")), "utf8"));
  const all = trace.traceEvents;
  const byName = (name, pid) => all.filter(e => e.name === name && (pid === undefined || e.pid === pid)).sort((a, b) => a.ts - b.ts);
  const enqueues = byName("WebMediaPlayerMSCompositor::EnqueueFrame");
  const pids = [...new Set(enqueues.map(e => e.pid))];
  const native = pids.map(pid => {
    const enqueue = enqueues.filter(e => e.pid === pid);
    const chosen = byName("WebMediaPlayerMSCompositor::SetCurrentFrame", pid);
    const chosenTimes = new Set(chosen.map(e => e.args.Timestamp));
    const interior = enqueue.filter(e => e.ts > enqueue[0].ts + 500000 && e.ts < enqueue.at(-1).ts - 500000);
    const selectionDelays = interior.map(e => {
      const chosenFrame = chosen.find(c => c.args.Timestamp === e.args.Timestamp && c.ts >= e.ts);
      return chosenFrame ? (chosenFrame.ts - e.ts) / 1000 : NaN;
    });
    const decode = byName("RTCVideoDecoderAdapter::Decode", pid).filter(e => e.ph === "X");
    const matches = frames.map(f => {
      const d = decode.find(e => e.args.timestamp === f.rtpTimestamp);
      const q = enqueue.find(e => Math.abs(e.args.Timestamp - f.mediaTime * 1e6) < 2);
      const c = chosen.find(e => Math.abs(e.args.Timestamp - f.mediaTime * 1e6) < 2);
      return { f, d, q, c };
    }).filter(x => x.d && x.q && x.c);
    const clock = all.find(e => e.pid === pid && /^desktop-study-(start|late)$/.test(e.name) && typeof e.args?.data?.startTime === "number");
    const clockOffsetMs = clock ? clock.ts / 1000 - clock.args.data.startTime : null;
    const render = byName("RemoteVideoSourceDelegate::RenderFrame", pid);
    const unselected = interior.filter(e => !chosenTimes.has(e.args.Timestamp)).map(e => {
      const r = render.find(r => r.args.Timestamp === e.args.Timestamp);
      const index = enqueue.indexOf(e);
      return { queueGapMs: index > 0 ? (e.ts - enqueue[index - 1].ts) / 1000 : null, sourceGapMs: index > 0 ? (e.args.Timestamp - enqueue[index - 1].args.Timestamp) / 1000 : null,
        enqueueAfterIdealMs: typeof r?.args["Ideal Render Instant"] === "number" ? (e.ts - r.args["Ideal Render Instant"]) / 1000 : null };
    });
    return { pid, enqueued: enqueue.length, chosen: chosen.length, interiorEnqueued: interior.length, interiorNotChosen: interior.filter(e => !chosenTimes.has(e.args.Timestamp)).length,
      beginFrameMs: intervals(byName("VideoFrameSubmitter::OnBeginFrame", pid).filter(e => e.ph === "X")),
      sourceMediaIntervalMs: quantiles(enqueue.slice(1).map((e, i) => (e.args.Timestamp - enqueue[i].args.Timestamp) / 1000)),
      enqueueToSelectionMs: quantiles(selectionDelays), matchedRtpDecodeMediaEnqueueSelection: matches.length,
      decodeToEnqueueMs: quantiles(matches.map(m => (m.q.ts - m.d.ts) / 1000)),
      receiveToDecodeMs: clockOffsetMs === null ? null : quantiles(matches.map(m => typeof m.f.receiveTime === "number" ? m.d.ts / 1000 - clockOffsetMs - m.f.receiveTime : NaN)),
      receiveToSelectionMs: clockOffsetMs === null ? null : quantiles(matches.map(m => typeof m.f.receiveTime === "number" ? m.c.ts / 1000 - clockOffsetMs - m.f.receiveTime : NaN)),
      unselected,
      hardwareDecodeEvents: byName("D3DVideoDecoder::OutputResult").length,
    };
  });
  let host = [];
  try {
    const start = Date.parse(run.startedAt), end = start + run.duration * 1000;
    host = (await readFile(resolve(directory, "host.jsonl"), "utf8")).trim().split(/\r?\n/).map(s => JSON.parse(s.replace(/^\uFEFF/, ""))).filter(h => Date.parse(h.utc) >= start && Date.parse(h.utc) <= end);
  } catch { /* Host data is optional, never fabricate it. */ }
  const gpu = {};
  for (const type of ["VideoEncode", "VideoDecode", "3D"]) gpu[type] = quantiles(host.map(h => {
    const engines = (h.gpuEngines || []).filter(e => e.engine.endsWith(`engtype_${type}`));
    return engines.length ? Math.max(...engines.map(e => e.utilization)) : NaN;
  }));
  const processCpu = {};
  for (const name of ["l4capture", "chrome"]) {
    const samples = host.slice(1).map((h, i) => {
      const previous = host[i];
      let delta = 0;
      for (const p of h.processes.filter(p => p.name === name)) { const before = previous.processes.find(b => b.pid === p.pid); if (before && p.cpuSeconds >= before.cpuSeconds) delta += p.cpuSeconds - before.cpuSeconds; }
      return delta / ((Date.parse(h.utc) - Date.parse(previous.utc)) / 1000) / h.logicalProcessors * 100;
    });
    processCpu[name] = quantiles(samples);
  }
  summaries.push({ phase: run.phase, display: run.display, startedAt: run.startedAt, version: run.version.product, sourceRequest: run.sourceRequest, sourceDescriptor: run.sourceDescriptor, gpuDevices: run.gpu,
    seconds, raster: [last.videoWidth, last.videoHeight], codec: last.codecs, decodedFps: decoded / seconds,
    presentedFps: (frames.at(-1).presentedFrames - frames[0].presentedFrames) / frameSeconds,
    mbps: diff("bytesReceived") * 8 / seconds / 1e6, packetsLost: diff("packetsLost"), received: diff("framesReceived"), decoded,
    receiveDropped: diff("framesDropped"), videoDropped: last.videoDropped - first.videoDropped, freezes: diff("freezeCount"),
    decodeMs: decoded > 0 ? diff("totalDecodeTime") * 1000 / decoded : null,
    jitterBufferMs: emitted > 0 ? diff("jitterBufferDelay") * 1000 / emitted : null,
    visibleSamples: run.samples.filter(s => s.visible === "visible").length, totalSamples: run.samples.length,
    rVFCintervalMs: quantiles(frames.slice(1).map((f, i) => f.now - frames[i].now)),
    rVFCreceiveToPresentationMs: quantiles(frames.map(f => f.presentationTime - f.receiveTime)),
    rVFCprocessingMs: quantiles(frames.map(f => f.processingDuration * 1000)),
    actions: run.actions, windowGeometry: run.windowGeometry, cpuPercent: quantiles(host.map(h => h.cpuPercent)), processCpuPercent: processCpu, gpuMaxEnginePercent: gpu, native,
  });
}
await writeFile(resolve(directory, "summary.json"), JSON.stringify(summaries, null, 2));
console.log(JSON.stringify(process.argv.includes("--brief") ? summaries.map(s => ({ phase: s.phase, display: s.display, decodedFps: s.decodedFps, presentedFps: s.presentedFps, mbps: s.mbps, packetsLost: s.packetsLost, videoDropped: s.videoDropped, freezes: s.freezes, cpu: s.cpuPercent?.mean, beginP50: s.native[0]?.beginFrameMs?.p50, notChosen: s.native[0]?.interiorNotChosen })) : summaries, null, 2));
