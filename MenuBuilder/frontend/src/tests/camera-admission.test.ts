import { afterEach, describe, expect, it, vi } from "vitest";
import { admitCamera } from "../utils/cameraAdmission";
import { CameraAttempt } from "../utils/cameraAttempt";
import type { CameraPreparation } from "../api/cameras";

const selection: CameraPreparation = {
  source_id: "cam:ip000001", physical_camera_mac: "02:00:00:00:00:01", discovery_generation: 1,
  codec_policy: "auto", browser_codecs: { h264: true, h265: true, checked_at_ms: 1, valid_until_ms: 60001 },
};
function api() { return {
  prepareCamera: vi.fn().mockResolvedValue({ ticket: "opaque-prepared" }),
  cameraEnrollment: vi.fn().mockResolvedValue({ enrolled: true }),
  cameraReadiness: vi.fn().mockResolvedValue({ ticket: "opaque-ready" }),
}; }
afterEach(() => vi.useRealTimers());

describe("camera pre-lease admission", () => {
  it("orders preparation, enrollment and readiness with the captured selection", async () => {
    const attempt = new CameraAttempt(); const calls = api(); const stage = vi.fn();
    expect(await admitCamera(4623, selection, attempt, stage, calls)).toEqual({ ticket: "opaque-ready" });
    expect(calls.prepareCamera).toHaveBeenCalledOnce();
    expect(calls.prepareCamera).toHaveBeenCalledWith(4623, selection, attempt.signal);
    expect(calls.cameraReadiness).toHaveBeenCalledWith(4623, { ticket: "opaque-prepared" }, attempt.signal);
    expect(calls.prepareCamera.mock.invocationCallOrder[0]).toBeLessThan(calls.cameraEnrollment.mock.invocationCallOrder[0]);
    expect(calls.cameraEnrollment.mock.invocationCallOrder[0]).toBeLessThan(calls.cameraReadiness.mock.invocationCallOrder[0]);
    attempt.finish();
  });

  it.each(["prepareCamera", "cameraEnrollment", "cameraReadiness"] as const)("cancels %s and rejects its late completion", async target => {
    const attempt = new CameraAttempt(); const calls = api();
    let complete!: (value: any) => void;
    calls[target].mockImplementation(() => new Promise(resolve => { complete = resolve; }));
    const pending = admitCamera(4623, selection, attempt, vi.fn(), calls);
    const rejected = expect(pending).rejects.toMatchObject({ name: "AbortError" });
    for (let i = 0; i < 12; i++) await Promise.resolve();
    attempt.cancel(); await rejected;
    complete({ ticket: "obsolete", enrolled: true });
    await Promise.resolve();
    expect(calls[target]).toHaveBeenCalledOnce();
    if (target !== "cameraReadiness") expect(calls.cameraReadiness).not.toHaveBeenCalled();
  });

  it("bounds the original enrollment wait and never invokes readiness on timeout", async () => {
    vi.useFakeTimers(); const attempt = new CameraAttempt(); const calls = api();
    calls.cameraEnrollment.mockResolvedValue({ enrolled: false });
    const pending = admitCamera(4623, selection, attempt, vi.fn(), calls);
    const rejected = expect(pending).rejects.toThrow(/время|вовремя/);
    await vi.advanceTimersByTimeAsync(5100); await rejected;
    expect(calls.prepareCamera).toHaveBeenCalledOnce();
    expect(calls.cameraReadiness).not.toHaveBeenCalled(); attempt.finish();
  });

  it.each(["prepareCamera", "cameraReadiness"] as const)("never replays ambiguous %s", async target => {
    const attempt = new CameraAttempt(); const calls = api();
    calls[target].mockRejectedValue(new Error("ambiguous transport"));
    await expect(admitCamera(4623, selection, attempt, vi.fn(), calls)).rejects.toThrow("ambiguous");
    expect(calls[target]).toHaveBeenCalledOnce(); attempt.finish();
  });

  it("bounds a hung auth queue and marks expiry", async () => {
    vi.useFakeTimers(); const attempt = new CameraAttempt(1000);
    const pending = attempt.wait(new Promise(() => {}));
    const rejected = expect(pending).rejects.toMatchObject({ name: "AbortError" });
    await vi.advanceTimersByTimeAsync(1000); await rejected;
    expect(attempt.timedOut).toBe(true);
  });
});
