import { expect, it } from "vitest";
import { DesktopQualityPolicy } from "../utils/desktopQuality";
import type { RuntimeQuality } from "../api/video";

const nominal: RuntimeQuality = { available: true, ceiling_kbps: 2000, applied_kbps: 2000,
  generation: 0, status: "nominal", report_age_ms: 0, transport_available: true, transport_pressure: false };
const stat = (n: number, lost = 0, decoded = n * 10) => ({ timestamp: n * 1000,
  packetsLost: lost, packetsReceived: n * 100, framesDecoded: decoded });

it("cuts on measured upstream queue pressure, independent of browser frame drops", () => {
  const policy = new DesktopQualityPolicy();
  expect(policy.observe({ ...nominal, transport_pressure: true }, stat(1), 1000)).toBe(1500);
  expect(policy.observe({ ...nominal, status: "pending", transport_pressure: true }, stat(2), 2000)).toBeNull();
});

it("does not mistake stalled decoding/compositing or an idle low bitrate desktop for network pressure", () => {
  const policy = new DesktopQualityPolicy();
  for (let i = 0; i < 10; i++) expect(policy.observe({ ...nominal, delivery_kbps: 50 }, stat(i, 0, 0), i * 1000)).toBeNull();
});

it("uses RTP loss deltas rather than a cumulative historical loss", () => {
  const policy = new DesktopQualityPolicy();
  expect(policy.observe(nominal, stat(1, 100), 1000)).toBeNull();
  expect(policy.observe(nominal, stat(2, 100), 2000)).toBeNull();
  expect(policy.observe(nominal, stat(3, 105), 3000)).toBe(1500);
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
  expect(policy.observe({ ...nominal, status: "unconfirmed", transport_pressure: true }, stat(1), 1000)).toBe(1500);
});
