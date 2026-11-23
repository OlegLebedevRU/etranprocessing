import { afterEach, describe, expect, it, vi } from "vitest";
import { admitCamera, readCameraForStart } from "../utils/cameraAdmission";
import * as cameras from "../api/cameras";
import { CameraAttempt } from "../utils/cameraAttempt";
import type { CameraPreparation } from "../api/cameras";

const selection: CameraPreparation = {
  source_id: "cam:ip000001", physical_camera_mac: "02:00:00:00:00:01", discovery_generation: 1,
  codec_policy: "auto", profile_id: "default", browser_codecs: { h264: true, h265: true, checked_at_ms: 1, valid_until_ms: 60001 },
};
function api() { return {
  prepareCamera: vi.fn().mockResolvedValue({ ticket: "opaque-prepared" }),
  cameraEnrollment: vi.fn().mockResolvedValue({ enrolled: true }),
  cameraReadiness: vi.fn().mockResolvedValue({ ticket: "opaque-ready" }),
}; }
afterEach(() => vi.useRealTimers());

describe("camera refresh on an explicit start", () => {
  afterEach(() => vi.restoreAllMocks());
  const verifiedCamera: cameras.CameraState = {
    camera_id: selection.source_id,
    discovery: { state: "ready", generation: 1, physical_port: 2, ethernet_mode: "ports",
      candidates: [{ candidate_id: selection.physical_camera_mac, mac: selection.physical_camera_mac,
        ip: null, port_verified: true, auth: "verified", native_api: "supported" }] },
  };
  it("uses a fresh source-scoped media observation from the firmware", async () => {
    const state = { device_id: 4623, terminal_online: true, lease_active: false,
      snapshot: { snapshot_id: "ready", received_at: new Date().toISOString(), cameras: [verifiedCamera],
        media_observations: [{ source_id: selection.source_id, actual_track: { codec: "h264" } }] } };
    vi.spyOn(cameras, "getCamera").mockResolvedValue(state);
    const send = vi.spyOn(cameras, "commandCamera");
    expect(await readCameraForStart(4623, new CameraAttempt(null))).toBe(state);
    expect(send).not.toHaveBeenCalled();
  });
  it("reports missing media separately after the device verified the port", async () => {
    const state = { device_id: 4623, terminal_online: true, lease_active: false,
      snapshot: { snapshot_id: "read", operation_id: "task", cameras: [verifiedCamera], media_observations: [] } };
    vi.spyOn(cameras, "getCamera").mockResolvedValue(state);
    vi.spyOn(cameras, "commandCamera").mockResolvedValue({ id: "task" });
    vi.spyOn(cameras, "getCameraCommand").mockResolvedValue({ id: "task", status: 3,
      results: [{ status_code: 200, result: { snapshot_id: "read" } }] });
    await expect(readCameraForStart(4623, new CameraAttempt(null))).rejects.toThrow("видеодорожку RTSP");
  });
  it("requests one read and ignores another operation's gauge", async () => {
    vi.useFakeTimers();
    const state = { device_id: 4623, terminal_online: true, lease_active: false };
    const snapshot = { snapshot_id: "new", operation_id: "task", cameras: [] };
    const read = vi.spyOn(cameras, "getCamera").mockResolvedValueOnce({ ...state, snapshot: null })
      .mockResolvedValueOnce({ ...state, snapshot: { ...snapshot, operation_id: null } })
      .mockResolvedValue({ ...state, snapshot });
    const send = vi.spyOn(cameras, "commandCamera").mockResolvedValue({ id: "task" });
    vi.spyOn(cameras, "getCameraCommand").mockResolvedValue({ id: "task", status: 3,
      results: [{ status_code: 200, result: { snapshot_id: "new" } }] });
    const pending = readCameraForStart(4623, new CameraAttempt(null));
    await vi.advanceTimersByTimeAsync(1000);
    expect((await pending).snapshot).toEqual(snapshot);
    expect(send).toHaveBeenCalledTimes(1);
    expect(read).toHaveBeenCalledTimes(3);
  });
  it("leaves a busy camera alone", async () => {
    vi.spyOn(cameras, "getCamera").mockResolvedValue({ device_id: 4623, terminal_online: true, lease_active: true, snapshot: null });
    const send = vi.spyOn(cameras, "commandCamera");
    await expect(readCameraForStart(4623, new CameraAttempt(null))).rejects.toThrow("арендой");
    expect(send).not.toHaveBeenCalled();
  });
  it.each([["camera_unavailable", 2], ["auth_required", 1]])(
    "only requests discovery after a confirmed recoverable failure (%s)", async (reason, count) => {
      const state = { device_id: 4623, terminal_online: true, lease_active: false };
      const snapshot = { snapshot_id: "response", operation_id: "read",
        cameras: [{ camera_id: "cam:ip000001", capabilities: { methods: [165] } }] };
      vi.spyOn(cameras, "getCamera").mockResolvedValueOnce({ ...state, snapshot: null })
        .mockResolvedValueOnce({ ...state, snapshot })
        .mockResolvedValue({ ...state, snapshot: { ...snapshot, operation_id: "search" } });
      const send = vi.spyOn(cameras, "commandCamera").mockResolvedValueOnce({ id: "read" })
        .mockResolvedValueOnce({ id: "search" });
      vi.spyOn(cameras, "getCameraCommand").mockResolvedValue({ id: "read", status: 3,
        results: [{ status_code: 502, result: { snapshot_id: "response", error: String(reason) } }] });
      await expect(readCameraForStart(4623, new CameraAttempt(null))).rejects.toThrow(String(reason));
      expect(send).toHaveBeenCalledTimes(Number(count));
    });
});

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
    await vi.advanceTimersByTimeAsync(30100); await rejected;
    expect(calls.prepareCamera).toHaveBeenCalledOnce();
    expect(calls.cameraReadiness).not.toHaveBeenCalled(); attempt.finish();
  });

  it("waits for a later HELLO after 5001ms without extending the original overall deadline", async () => {
    vi.useFakeTimers(); const attempt = new CameraAttempt(); const calls = api();
    calls.cameraEnrollment.mockResolvedValue({ enrolled: false });
    const pending = admitCamera(4623, selection, attempt, vi.fn(), calls);
    await vi.advanceTimersByTimeAsync(5001);
    expect(calls.cameraReadiness).not.toHaveBeenCalled();
    calls.cameraEnrollment.mockResolvedValue({ enrolled: true });
    await vi.advanceTimersByTimeAsync(300);
    expect(await pending).toEqual({ ticket: "opaque-ready" });
    expect(calls.prepareCamera).toHaveBeenCalledOnce();attempt.finish();
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

  it("keeps a cancellation-only guard alive across independently bounded desktop phases", async () => {
    vi.useFakeTimers(); const attempt = new CameraAttempt(null);
    await vi.advanceTimersByTimeAsync(9000 + 115000);
    expect(() => attempt.check()).not.toThrow();
    expect(attempt.timedOut).toBe(false);
    attempt.cancel();
    expect(() => attempt.check()).toThrow();
    expect(attempt.timedOut).toBe(false);
  });
});
