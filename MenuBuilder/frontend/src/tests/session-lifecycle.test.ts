import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import {
  SessionLifecycleCoordinator,
  isInputPermitted,
  type SessionCallbacks,
} from "../utils/sessionLifecycle";

describe("SessionLifecycleCoordinator & Reproduction Tests", () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("F3: Detaching input does NOT release the lease; keepalive continues ticking", async () => {
    const keepaliveMock = vi.fn().mockResolvedValue(undefined);
    const releaseMock = vi.fn().mockResolvedValue(undefined);
    const scopeMock = vi.fn().mockResolvedValue(undefined);
    const termMock = vi.fn();
    const lostMock = vi.fn();

    const coordinator = new SessionLifecycleCoordinator({
      onKeepaliveRequest: keepaliveMock,
      onReleaseRequest: releaseMock,
      onScopeChangeRequest: scopeMock,
      onSessionTerminated: termMock,
      onLeaseLost: lostMock,
    });

    // 1. Start stream session
    coordinator.startSession({
      deviceId: 773,
      leaseId: "lease-773-abc",
      streamInstanceId: "inst-1",
      keepaliveIntervalMs: 5000,
    });

    expect(coordinator.isActive).toBe(true);

    // Advance 5 seconds -> first keepalive
    await vi.advanceTimersByTimeAsync(5000);
    expect(keepaliveMock).toHaveBeenCalledTimes(1);
    expect(keepaliveMock).toHaveBeenLastCalledWith(773, "lease-773-abc");

    // 2. Operator disables remote input (detach input)
    await coordinator.detachInput();

    // Verify lease was NOT released!
    expect(releaseMock).not.toHaveBeenCalled();
    expect(scopeMock).toHaveBeenCalledWith(773, "stream");
    expect(coordinator.isActive).toBe(true);

    // Advance another 5 seconds -> second keepalive fires normally (no 404 because lease was not deleted!)
    await vi.advanceTimersByTimeAsync(5000);
    expect(keepaliveMock).toHaveBeenCalledTimes(2);

    // 3. Operator stops whole session
    await coordinator.stopSession("user_exit");
    expect(coordinator.isActive).toBe(false);
    expect(releaseMock).toHaveBeenCalledTimes(1);
    expect(releaseMock).toHaveBeenCalledWith(773, "lease-773-abc");

    // Advance another 10 seconds -> NO further keepalive calls!
    await vi.advanceTimersByTimeAsync(10000);
    expect(keepaliveMock).toHaveBeenCalledTimes(2);
  });

  it("Generation Epoch: Late in-flight keepalive 404 from old generation is safely ignored", async () => {
    let pendingReject: ((err: any) => void) | null = null;
    const keepaliveMock = vi.fn().mockImplementation(() => {
      return new Promise<void>((_, reject) => {
        pendingReject = reject;
      });
    });
    const releaseMock = vi.fn().mockResolvedValue(undefined);
    const termMock = vi.fn();
    const lostMock = vi.fn();

    const coordinator = new SessionLifecycleCoordinator({
      onKeepaliveRequest: keepaliveMock,
      onReleaseRequest: releaseMock,
      onSessionTerminated: termMock,
      onLeaseLost: lostMock,
    });

    // Gen 1 started
    coordinator.startSession({
      deviceId: 773,
      leaseId: "lease-old",
      streamInstanceId: "inst-old",
      keepaliveIntervalMs: 5000,
    });

    // Fire timer -> keepalive request is in flight
    await vi.advanceTimersByTimeAsync(5000);
    expect(keepaliveMock).toHaveBeenCalledTimes(1);

    // Before keepalive returns, user stops Gen 1 and starts Gen 2
    await coordinator.stopSession("switch");
    coordinator.startSession({
      deviceId: 773,
      leaseId: "lease-new",
      streamInstanceId: "inst-new",
      keepaliveIntervalMs: 5000,
    });

    // Now late response from Gen 1 arrives with 404
    const err404 = { response: { status: 404, data: { detail: "Lease not found" } } };
    pendingReject!(err404);
    await vi.advanceTimersByTimeAsync(10);

    // Must NOT call onLeaseLost because it was from an obsolete generation!
    expect(lostMock).not.toHaveBeenCalled();
    expect(coordinator.isActive).toBe(true);
    expect(coordinator.session?.leaseId).toBe("lease-new");
  });

  it("F4: Terminal stopped/failed event with matching stream_instance_id terminates session immediately", async () => {
    const keepaliveMock = vi.fn().mockResolvedValue(undefined);
    const releaseMock = vi.fn().mockResolvedValue(undefined);
    const termMock = vi.fn();
    const lostMock = vi.fn();

    const coordinator = new SessionLifecycleCoordinator({
      onKeepaliveRequest: keepaliveMock,
      onReleaseRequest: releaseMock,
      onSessionTerminated: termMock,
      onLeaseLost: lostMock,
    });

    coordinator.startSession({
      deviceId: 773,
      leaseId: "lease-active",
      streamInstanceId: "8cbcd134-f05e-48fd-bef7-0db96ffab8ed",
      keepaliveIntervalMs: 5000,
    });

    // Terminal stopped event arrives (e.g. from WS or REST polling)
    const handled = coordinator.handleStreamStateEvent({
      state: "stopped",
      reason: "lease_expired",
      stream_instance_id: "8cbcd134-f05e-48fd-bef7-0db96ffab8ed",
    });

    expect(handled).toBe(true);
    expect(termMock).toHaveBeenCalledWith("lease_expired", { code: "stopped" });
    expect(coordinator.isActive).toBe(false);
    expect(releaseMock).toHaveBeenCalledTimes(1);
  });

  it("Stale Epoch: Stopped event with OLD stream_instance_id is ignored", async () => {
    const keepaliveMock = vi.fn().mockResolvedValue(undefined);
    const releaseMock = vi.fn().mockResolvedValue(undefined);
    const termMock = vi.fn();
    const lostMock = vi.fn();

    const coordinator = new SessionLifecycleCoordinator({
      onKeepaliveRequest: keepaliveMock,
      onReleaseRequest: releaseMock,
      onSessionTerminated: termMock,
      onLeaseLost: lostMock,
    });

    coordinator.startSession({
      deviceId: 773,
      leaseId: "lease-current",
      streamInstanceId: "stream-current-epoch",
      keepaliveIntervalMs: 5000,
    });

    // Delayed event from previous stream arrives
    const handled = coordinator.handleStreamStateEvent({
      state: "stopped",
      reason: "lease_expired",
      stream_instance_id: "stream-old-epoch",
    });

    expect(handled).toBe(false);
    expect(termMock).not.toHaveBeenCalled();
    expect(coordinator.isActive).toBe(true);
  });

  it("Fail-closed input permission evaluation", () => {
    // 1. Unknown or undefined streamMode -> strictly forbidden
    expect(isInputPermitted(undefined, true, true)).toBe(false);
    expect(isInputPermitted(null, true, true)).toBe(false);
    expect(isInputPermitted("", true, true)).toBe(false);
    expect(isInputPermitted("unknown", true, true)).toBe(false);

    // 2. Camera mode -> forbidden
    expect(isInputPermitted("usb-camera", true, true)).toBe(false);
    expect(isInputPermitted("camera", true, true)).toBe(false);

    // 3. Desktop mode, but control not active -> forbidden
    expect(isInputPermitted("desktop", false, true)).toBe(false);

    // 4. Desktop mode, control active, but geometry not known/invalid -> forbidden
    expect(isInputPermitted("desktop", true, false)).toBe(false);

    // 5. Desktop mode, control active, and valid geometry -> PERMITTED
    expect(isInputPermitted("desktop", true, true)).toBe(true);
  });

  it("Fatal lease error (404/403/409) triggers single transition and halts renewals", async () => {
    const keepaliveMock = vi.fn().mockRejectedValue({
      response: { status: 404, data: { detail: "Lease not found" } },
    });
    const releaseMock = vi.fn().mockResolvedValue(undefined);
    const termMock = vi.fn();
    const lostMock = vi.fn();

    const coordinator = new SessionLifecycleCoordinator({
      onKeepaliveRequest: keepaliveMock,
      onReleaseRequest: releaseMock,
      onSessionTerminated: termMock,
      onLeaseLost: lostMock,
    });

    coordinator.startSession({
      deviceId: 773,
      leaseId: "lease-404",
      streamInstanceId: "inst-404",
      keepaliveIntervalMs: 5000,
    });

    // Advance 5 seconds to trigger keepalive
    await vi.advanceTimersByTimeAsync(5000);

    expect(keepaliveMock).toHaveBeenCalledTimes(1);
    expect(lostMock).toHaveBeenCalledTimes(1);
    expect(lostMock).toHaveBeenCalledWith(404, "Lease not found");
    expect(coordinator.isActive).toBe(false);

    // Advance 10 more seconds: no further attempts
    await vi.advanceTimersByTimeAsync(10000);
    expect(keepaliveMock).toHaveBeenCalledTimes(1);
  });
});
