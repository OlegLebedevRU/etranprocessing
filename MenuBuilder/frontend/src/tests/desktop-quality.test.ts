import { expect, it } from "vitest";
import { DesktopQualityPolicy } from "../utils/desktopQuality";
import type { RuntimeQuality } from "../api/video";

const nominal: RuntimeQuality = { available: true, ceiling_kbps: 2000, applied_kbps: 2000,
  generation: 0, status: "nominal", report_age_ms: 0, transport_available: true, transport_pressure: false };
const stat = (n: number, lost = 0, decoded = n * 10) => ({ timestamp: n * 1000,
  packetsLost: lost, packetsReceived: n * 100, framesDecoded: decoded });

const raster = { ...nominal, raster_available: true, raster_step: 0,
  initial_width: 3000, initial_height: 2000 };

it("detects a frame still assembling before the first decode without blaming decoder stalls", () => {
  const policy = new DesktopQualityPolicy();
  expect(policy.observeTarget(raster, { ...stat(1), framesReceived: 0, framesDecoded: 0 }, 1000)).toBeNull();
  expect(policy.observeTarget(raster, { ...stat(2), framesReceived: 0, framesDecoded: 0 }, 2000))
    .toEqual({ kbps: 1000 });
  const decoder = new DesktopQualityPolicy();
  for (let n = 1; n < 8; n++) expect(decoder.observeTarget(raster,
    { ...stat(n), framesReceived: n * 10, framesDecoded: 0 }, n * 1000)).toBeNull();
  const loss = new DesktopQualityPolicy();
  for (let n = 1; n < 8; n++) expect(loss.observeTarget(raster,
    { ...stat(n, n * 10), framesReceived: 0, framesDecoded: 0 }, n * 1000)).toBeNull();
});

it("reduces raster by half per side only when the bitrate reaches the quarter-pixel budget", () => {
  expect(new DesktopQualityPolicy().observeTarget({ ...raster, transport_pressure: true }, stat(1), 1000))
    .toEqual({ kbps: 1000 });
  expect(new DesktopQualityPolicy().observeTarget({ ...raster, applied_kbps: 1000, transport_pressure: true }, stat(1), 1000))
    .toEqual({ kbps: 500, rasterStep: 1 });
  expect(new DesktopQualityPolicy().observeTarget({ ...raster, raster_step: 1, applied_kbps: 250, transport_pressure: true }, stat(1), 1000))
    .toEqual({ kbps: 125, rasterStep: 2 });
});

it("does not shrink raster beyond the supported bitrate floor or without native opt-in", () => {
  expect(new DesktopQualityPolicy().observeTarget({ ...raster, raster_step: 2, applied_kbps: 125, transport_pressure: true }, stat(1), 1000))
    .toEqual({ kbps: 100 });
  expect(new DesktopQualityPolicy().observeTarget({ ...raster, raster_available: false, applied_kbps: 1000, transport_pressure: true }, stat(1), 1000))
    .toEqual({ kbps: 500 });
});

it("probes one sharper raster at unchanged bitrate after clear measured delivery", () => {
  const policy = new DesktopQualityPolicy();
  const reduced = { ...raster, raster_step: 1, applied_kbps: 500, status: "limited" as const };
  for (let n = 0; n <= 5; n++) expect(policy.observeTarget(reduced, stat(n), n * 1000)).toBeNull();
  expect(policy.observeTarget(reduced, stat(6), 6000)).toEqual({ kbps: 500, rasterStep: 0 });
  expect(policy.observeTarget({ ...raster, applied_kbps: 500, transport_pressure: true }, stat(7), 7000))
    .toEqual({ kbps: 250, rasterStep: 1 });
  for (let n = 8; n <= 17; n++) expect(policy.observeTarget(reduced, stat(n), n * 1000)).toBeNull();
  expect(policy.observeTarget(reduced, stat(18), 18000)).toEqual({ kbps: 500, rasterStep: 0 });
});

it("cuts on measured upstream queue pressure, independent of browser frame drops", () => {
  const policy = new DesktopQualityPolicy();
  expect(policy.observe({ ...nominal, transport_pressure: true }, stat(1), 1000)).toBe(1000);
  expect(policy.observe({ ...nominal, status: "pending", transport_pressure: true }, stat(2), 2000)).toBeNull();
});

it("does not mistake stalled decoding/compositing or an idle low bitrate desktop for network pressure", () => {
  const policy = new DesktopQualityPolicy();
  for (let i = 0; i < 10; i++) expect(policy.observe({ ...nominal, delivery_kbps: 50 }, stat(i, 0, 0), i * 1000)).toBeNull();
});

it("does not mistake RTP loss alone for measured congestion", () => {
  const policy = new DesktopQualityPolicy();
  expect(policy.observe(nominal, stat(1, 100), 1000)).toBeNull();
  expect(policy.observe(nominal, stat(2, 100), 2000)).toBeNull();
  expect(policy.observe(nominal, stat(3, 105), 3000)).toBeNull();
});

it("can recover despite isolated RTP losses when no queue is growing", () => {
  const policy = new DesktopQualityPolicy();
  const limited = { ...nominal, applied_kbps: 1000, status: "limited" as const };
  for (let i = 0; i <= 5; i++) expect(policy.observe(limited, stat(i, i * 20), i * 1000)).toBeNull();
  expect(policy.observe(limited, stat(6, 120), 6000)).toBe(1200);
});

it("does not pin the actuator floor for a fixed offset, but cannot recover while delay keeps growing", () => {
  const floor = { ...nominal, applied_kbps: 100, status: "limited" as const };
  const sample = (n: number, media: number) => ({ ...stat(n), ssrc: 7,
    sourceTimestamp: n * 1000, sourceRtpTimestamp: media * 90 });
  const policy = new DesktopQualityPolicy();
  expect(policy.observe(floor, sample(1, 1000), 1000)).toBeNull();
  expect(policy.observe(floor, sample(2, 1500), 2000)).toBeNull();
  for (let n = 3; n < 8; n++) expect(policy.observe(floor, sample(n, n * 1000 - 500), n * 1000)).toBeNull();
  expect(policy.observe(floor, sample(8, 7500), 8000)).toBe(300);
  const growing = new DesktopQualityPolicy();
  for (let n = 1; n < 20; n++) expect(growing.observe(floor, sample(n, n * 500), n * 1000)).toBeNull();
});

it("recovers gradually only with fresh source feedback and decoded frames, never above the selected ceiling", () => {
  const policy = new DesktopQualityPolicy();
  const limited = { ...nominal, applied_kbps: 1900, status: "limited" as const };
  for (let i = 0; i <= 5; i++) expect(policy.observe(limited, stat(i), i * 1000)).toBeNull();
  expect(policy.observe(limited, stat(6), 6000)).toBe(2000);
  expect(policy.observe(nominal, stat(7), 7000)).toBeNull();
});

it("does not make quality changes without support, with stale reports or after a driver refusal", () => {
  for (const q of [{ ...nominal, available: false }, { ...nominal, report_age_ms: 3000 },
    { ...nominal, status: "unsupported" as const }]) {
    const policy = new DesktopQualityPolicy();
    expect(policy.observe({ ...q, transport_pressure: true }, stat(1), 1000)).toBeNull();
  }
});

it("never infers spare bandwidth from absent measurements", () => {
  const policy = new DesktopQualityPolicy();
  for (let i = 0; i < 20; i++) expect(policy.observe({ ...nominal, applied_kbps: 1000,
    transport_available: false }, stat(i), i * 1000)).toBeNull();
});

it("retries after an expired/lost control request using fresh actual source reports", () => {
  const policy = new DesktopQualityPolicy();
  expect(policy.observe({ ...nominal, status: "unconfirmed", transport_pressure: true }, stat(1), 1000)).toBe(1000);
});

it("cuts on accumulated receiver delay even when an ordered weak link loses no RTP packets", () => {
  const policy = new DesktopQualityPolicy();
  expect(policy.observe(nominal, { ...stat(1), ssrc: 7, sourceTimestamp: 1000, sourceRtpTimestamp: 90000 }, 1000)).toBeNull();
  expect(policy.observe(nominal, { ...stat(2), ssrc: 7, sourceTimestamp: 2000, sourceRtpTimestamp: 135000 }, 2000)).toBe(1000);
});

it("keeps idle source gaps and RTP timestamp wraps from manufacturing delay", () => {
  const policy = new DesktopQualityPolicy();
  const first = { ...stat(1), ssrc: 7, sourceTimestamp: 1000, sourceRtpTimestamp: 0xffff0000 };
  expect(policy.observe(nominal, first, 1000)).toBeNull();
  expect(policy.observe(nominal, { ...stat(4), ssrc: 7, sourceTimestamp: 4000,
    sourceRtpTimestamp: (first.sourceRtpTimestamp + 270000) >>> 0 }, 4000)).toBeNull();
});

it("clears accumulated receiver delay after catch-up or a source replacement", () => {
  const policy = new DesktopQualityPolicy();
  const sample = (n: number, media: number, ssrc = 7) => ({ ...stat(n), ssrc,
    sourceTimestamp: n * 1000, sourceRtpTimestamp: media * 90 });
  expect(policy.observe(nominal, sample(1, 1000), 1000)).toBeNull();
  expect(policy.observe(nominal, sample(2, 1500), 2000)).toBe(1000);
  expect(policy.observe(nominal, sample(3, 3000), 3000)).toBeNull();
  expect(policy.observe(nominal, sample(4, 3300), 4000)).toBe(1000);
  expect(policy.observe(nominal, sample(5, 10, 8), 5000)).toBeNull();
});

it("assesses new queue growth after application instead of cutting forever for a fixed residual offset", () => {
  const policy = new DesktopQualityPolicy();
  const sample = (n: number, media: number) => ({ ...stat(n), ssrc: 7,
    sourceTimestamp: n * 1000, sourceRtpTimestamp: media * 90 });
  expect(policy.observe(nominal, sample(1, 1000), 1000)).toBeNull();
  expect(policy.observe(nominal, sample(2, 1500), 2000)).toBe(1000);
  const applied = { ...nominal, applied_kbps: 1000, status: "limited" as const };
  for (let n = 3; n < 8; n++) expect(policy.observe(applied, sample(n, n * 1000 - 500), n * 1000)).toBeNull();
  expect(policy.observe(applied, sample(8, 7500), 8000)).toBe(1200);
  expect(policy.observe(applied, sample(9, 8000), 9000)).toBe(500);
});
