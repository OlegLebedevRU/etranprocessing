// Explicitly invoked, owner-visible live study. No credentials are read/exported.
// Example: node e2e/diagnostics/desktop-study.mjs --cdp http://127.0.0.1:9336
//   --url <authenticated portal /video?device_id=...> --display DISPLAY1
//   --phase A1 --seconds 90 --out <private ignored directory>
import { chromium } from "playwright";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { resolve, extname, sep } from "node:path";
import { parseArgs } from "node:util";

const { values } = parseArgs({ options: Object.fromEntries(["cdp", "url", "display", "phase", "seconds", "out"].map(key => [key, { type: "string" }])) });
for (const key of ["cdp", "url", "display", "phase", "out"]) if (!values[key]) throw Error(`Missing --${key}`);
const duration = Number(values.seconds || 90);
if (!Number.isFinite(duration) || duration < 10 || duration > 3600) throw Error("Invalid duration");
if (!/^DISPLAY[12]$/.test(values.display) || !/^[A-Za-z0-9-]+$/.test(values.phase)) throw Error("Invalid display/phase");
const base = new URL(values.url);
if (base.pathname !== "/video" || !base.searchParams.get("device_id")) throw Error("Use explicit desktop device URL");
const out = resolve(values.out);
await mkdir(out, { recursive: true });
const browser = await chromium.connectOverCDP(values.cdp);
const context = browser.contexts()[0];
const page = context.pages().find(p => p.url().startsWith(base.origin) && p.url().includes("study=desktop")) || await context.newPage();
// Refuse before navigation: unloading an owner page can itself stop its stream.
if (await page.getByRole("button", { name: "Остановить трансляцию", exact: true }).count()) {
  await browser.close();
  throw Error("Study page already owns an active stream; stop it normally first");
}
const dist = resolve("dist");
// Only this page gets local production-built static assets. API/auth/Janus stay real.
const serve = async route => {
  const url = new URL(route.request().url());
  if (url.origin !== base.origin || url.pathname.startsWith("/api/")) return route.continue();
  const name = url.pathname === "/video" ? "index.html" : url.pathname.slice(1);
  const file = resolve(dist, name);
  if (!file.startsWith(dist + sep)) return route.continue();
  try {
    const body = await readFile(file);
    await route.fulfill({ body, contentType: ({ ".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".svg": "image/svg+xml", ".json": "application/json" })[extname(file)] || "application/octet-stream" });
  } catch { await route.continue(); }
};
await page.route("**/*", serve);
await page.addInitScript(() => {
  const Original = window.RTCPeerConnection;
  const peers = [];
  window.RTCPeerConnection = new Proxy(Original, { construct(target, args) { const pc = Reflect.construct(target, args); peers.push(pc); return pc; } });
  window.__desktopStudy = { peers, frames: [], samples: [], phase: "warmup", callback: 0, video: null };
});
let started = false, tracing = false;
let cdp;
let sourceRequest = null, sourceDescriptor = null;
const actions = [];
page.on("response", async response => {
  const path = new URL(response.url()).pathname;
  const method = response.request().method();
  if (method !== "GET" && /\/(stream\/(start|stop)|control\/lease(?:\/[^/]+)?)$/.test(path)) {
    const action = path.includes("/stream/") ? path.split("/stream/")[1] : method === "DELETE" ? "release" : "acquire";
    actions.push({ action, method, status: response.status() });
  }
  if (new URL(response.url()).pathname.endsWith("/stream/start") && response.request().method() === "POST") {
    const request = response.request().postDataJSON();
    sourceRequest = { mode: request.mode, source_id: request.source_id, profile: request.profile, codec_policy: request.codec_policy };
    try { const body = await response.json(); sourceDescriptor = body.descriptor ?? null; } catch { /* Failed START is handled by the real UI. */ }
  }
});
const stem = `${values.phase}-${values.display}`;
try {
  await page.goto(values.url);
  await page.bringToFront();
  await page.getByRole("button", { name: "Запустить трансляцию", exact: true }).waitFor({ timeout: 45000 });
  if (await page.getByRole("button", { name: "Остановить трансляцию", exact: true }).count()) throw Error("Existing stream must be stopped by its owner");
  await page.getByRole("combobox", { name: "Источник видео" }).click();
  await page.locator(".ant-select-item-option").filter({ hasText: values.display }).click();
  await page.getByRole("button", { name: "Настройки видео", exact: true }).click();
  await page.getByRole("combobox", { name: "Выбор качества трансляции" }).click();
  await page.locator(".ant-select-item-option").filter({ hasText: /^HD$/ }).click();
  await page.getByRole("combobox", { name: "Кодек общего потока" }).click();
  await page.locator(".ant-select-item-option").filter({ hasText: /^H\.265/ }).click();
  await page.getByRole("button", { name: "Настройки видео", exact: true }).click();
  await page.getByRole("button", { name: "Запустить трансляцию", exact: true }).click();
  started = true;
  await page.getByTestId("video-telemetry").waitFor({ timeout: 120000 });
  await page.waitForFunction(() => document.querySelector("video")?.videoWidth > 0, null, { timeout: 45000 });
  await page.evaluate(({ phase, display }) => {
    const state = window.__desktopStudy;
    const video = document.querySelector("video");
    state.video = video;
    const badge = document.createElement("div");
    badge.id = "desktop-study-phase";
    badge.style.cssText = "position:fixed;right:12px;bottom:12px;z-index:10000;background:#111;color:#7ff;padding:10px;font:14px monospace;pointer-events:none";
    document.body.append(badge);
    state.label = `${phase} · ${display} · H265 HD`;
    badge.textContent = `${state.label} · прогрев`;
    const frame = (now, m) => {
      const f = { now, phase: state.phase, presentedFrames: m.presentedFrames, mediaTime: m.mediaTime, presentationTime: m.presentationTime, expectedDisplayTime: m.expectedDisplayTime, processingDuration: m.processingDuration, captureTime: m.captureTime, receiveTime: m.receiveTime, rtpTimestamp: m.rtpTimestamp };
      if (state.frames.length < 70000) state.frames.push(f);
      state.callback = video.requestVideoFrameCallback(frame);
    };
    state.callback = video.requestVideoFrameCallback(frame);
  }, { phase: values.phase, display: values.display });
  // Warmup is excluded; no jitterBufferTarget/source adaptation is applied.
  await new Promise(r => setTimeout(r, 15000));
  cdp = await browser.newBrowserCDPSession();
  const version = await cdp.send("Browser.getVersion");
  const system = await cdp.send("SystemInfo.getInfo");
  const gpu = system.gpu.devices.map(device => ({ vendorId: device.vendorId, deviceId: device.deviceId, vendorString: device.vendorString, deviceString: device.deviceString, driverVendor: device.driverVendor, driverVersion: device.driverVersion }));
  const categories = "media,webrtc,cc,viz,gpu,blink.user_timing,disabled-by-default-webrtc,disabled-by-default-media";
  await cdp.send("Tracing.start", { categories, transferMode: "ReturnAsStream", options: "record-as-much-as-possible" });
  tracing = true;
  await page.evaluate(() => { const s = window.__desktopStudy; s.frames = []; s.phase = "measure"; performance.mark("desktop-study-start", { detail: { performanceNow: performance.now() } }); });
  const start = Date.now();
  let traceData = null;
  while (Date.now() - start < duration * 1000) {
    const sample = await page.evaluate(async elapsed => {
      const s = window.__desktopStudy;
      const pc = s.peers.find(p => p.connectionState === "connected");
      if (!pc) throw Error("No connected receiver");
      const stats = await pc.getStats();
      const inbound = [...stats.values()].filter(x => x.type === "inbound-rtp" && (x.kind === "video" || x.mediaType === "video") && typeof x.framesDecoded === "number");
      const rows = inbound.map(x => Object.fromEntries(["id", "ssrc", "timestamp", "bytesReceived", "packetsReceived", "packetsLost", "framesReceived", "framesDecoded", "framesDropped", "framesRendered", "freezeCount", "totalFreezesDuration", "totalDecodeTime", "jitter", "jitterBufferDelay", "jitterBufferEmittedCount", "frameWidth", "frameHeight", "framesPerSecond", "decoderImplementation", "powerEfficientDecoder"].filter(k => x[k] !== undefined).map(k => [k, x[k]])));
      const quality = s.video.getVideoPlaybackQuality();
      const result = { elapsed, now: performance.now(), visible: document.visibilityState, videoWidth: s.video.videoWidth, videoHeight: s.video.videoHeight, videoTotal: quality.totalVideoFrames, videoDropped: quality.droppedVideoFrames, receivers: rows, codecs: inbound.map(x => stats.get(x.codecId)?.mimeType) };
      s.samples.push(result);
      document.getElementById("desktop-study-phase").textContent = `${s.label} · измерение ${Math.floor(elapsed)} с`;
      return result;
    }, (Date.now() - start) / 1000);
    if (sample.visible !== "visible") throw Error("Study tab hidden; comparison invalid");
    // Native tracing is a bounded20s window, identical for all study runs.
    if (tracing && Date.now() - start >= Math.min(20, duration) * 1000) {
      const complete = new Promise(resolve => cdp.once("Tracing.tracingComplete", resolve));
      await cdp.send("Tracing.end");
      traceData = await complete;
      tracing = false;
    }
    await new Promise(r => setTimeout(r, 1000));
  }
  const data = await page.evaluate(() => {
    const s = window.__desktopStudy;
    s.phase = "done"; performance.mark("desktop-study-end");
    s.video.cancelVideoFrameCallback(s.callback);
    document.getElementById("desktop-study-phase").textContent = `${s.label} · завершено`;
    return { samples: s.samples, frames: s.frames, userAgent: navigator.userAgent, windowGeometry: { screenX, screenY, innerWidth, innerHeight, devicePixelRatio } };
  });
  const release = page.waitForResponse(response => response.request().method() === "DELETE" && new URL(response.url()).pathname.startsWith(`/api/v1/video/devices/${base.searchParams.get("device_id")}/control/lease/`), { timeout: 30000 });
  await page.getByRole("button", { name: "Остановить трансляцию", exact: true }).click();
  const releaseResponse = await release;
  if (!releaseResponse.ok()) throw Error(`Lease release failed: ${releaseResponse.status()}`);
  await page.getByRole("button", { name: "Запустить трансляцию", exact: true }).waitFor({ timeout: 45000 });
  started = false;
  await writeFile(resolve(out, `${stem}.json`), JSON.stringify({ phase: values.phase, display: values.display, duration, startedAt: new Date(start).toISOString(), sourceRequest, sourceDescriptor, actions, gpu, warmupSeconds: 15, version, traceSeconds: Math.min(20, duration), ...data }));
  if (tracing) { const complete = new Promise(resolve => cdp.once("Tracing.tracingComplete", resolve)); await cdp.send("Tracing.end"); traceData = await complete; tracing = false; }
  if (traceData?.stream) {
    const chunks = [];
    for (;;) { const chunk = await cdp.send("IO.read", { handle: traceData.stream }); chunks.push(chunk.base64Encoded ? Buffer.from(chunk.data, "base64") : Buffer.from(chunk.data)); if (chunk.eof) break; }
    await cdp.send("IO.close", { handle: traceData.stream });
    await writeFile(resolve(out, `${stem}.trace.json`), Buffer.concat(chunks));
  }
  console.log(JSON.stringify({ phase: values.phase, display: values.display, samples: data.samples.length, frames: data.frames.length, stopped: true }));
} finally {
  if (tracing && cdp) await cdp.send("Tracing.end").catch(() => {});
  if (started) {
    const stop = page.getByRole("button", { name: "Остановить трансляцию", exact: true });
    if (await stop.count()) await stop.click().catch(error => console.error("STOP failed", error.message));
  }
  await page.unroute("**/*", serve);
  // Disconnect our client; never close the user's browser from the harness.
  await browser.close();
}
