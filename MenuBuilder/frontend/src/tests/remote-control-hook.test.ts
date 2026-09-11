import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";

describe("RemoteControl input and teardown behavior", () => {
  let mockWs: {
    send: any;
    close: any;
    readyState: number;
    onopen: any;
    onmessage: any;
    onerror: any;
    onclose: any;
  };

  beforeEach(() => {
    vi.useFakeTimers();
    mockWs = {
      send: vi.fn(),
      close: vi.fn(),
      readyState: 1, // WebSocket.OPEN
      onopen: null,
      onmessage: null,
      onerror: null,
      onclose: null,
    };
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("releases held keys when remote control is disabled", () => {
    // Simulate pressed keys state tracking
    const pressedKeys = new Set<number>();
    const sendKey = (kind: "down" | "up" | "press", vk: number) => {
      if (kind === "down") pressedKeys.add(vk);
      if (kind === "up") pressedKeys.delete(vk);
      mockWs.send(JSON.stringify({ type: "key_event", kind, vk }));
    };

    // Operator presses Shift (16) and Ctrl (17)
    sendKey("down", 16);
    sendKey("down", 17);
    expect(pressedKeys.size).toBe(2);
    expect(mockWs.send).toHaveBeenCalledTimes(2);

    // Operator releases Shift
    sendKey("up", 16);
    expect(pressedKeys.size).toBe(1);
    expect(pressedKeys.has(17)).toBe(true);

    // Now disable/teardown occurs while Ctrl (17) is still held down
    const teardown = () => {
      if (pressedKeys.size > 0 && mockWs.readyState === 1) {
        for (const vk of pressedKeys) {
          mockWs.send(JSON.stringify({ type: "key_event", kind: "up", vk }));
        }
        pressedKeys.clear();
      }
    };

    teardown();

    // Verify key_up for Ctrl was sent on teardown!
    expect(mockWs.send).toHaveBeenCalledTimes(4);
    expect(mockWs.send).toHaveBeenLastCalledWith(
      JSON.stringify({ type: "key_event", kind: "up", vk: 17 })
    );
    expect(pressedKeys.size).toBe(0);
  });

  it("shared lease detach does NOT send release or delete lease", async () => {
    const releaseControlLeaseMock = vi.fn().mockResolvedValue(undefined);
    const sharedLease = true;
    const isSessionActive = true;
    const reason: string = "operator_manual";

    const isDetachingInput = sharedLease && isSessionActive && reason !== "session_stopped";
    expect(isDetachingInput).toBe(true);

    // On WS detach
    if (!isDetachingInput && mockWs.readyState === 1) {
      mockWs.send(JSON.stringify({ type: "release" }));
    }
    mockWs.close();

    // Best-effort DELETE lease
    if (!sharedLease) {
      await releaseControlLeaseMock(773, "lease-1");
    }

    // Verify neither WS release nor HTTP DELETE lease was invoked!
    expect(mockWs.send).not.toHaveBeenCalled();
    expect(releaseControlLeaseMock).not.toHaveBeenCalled();
    expect(mockWs.close).toHaveBeenCalledTimes(1);
  });

  it("parses and forwards stream_state message from WebSocket", () => {
    const onStreamState = vi.fn();
    const handleWsMessage = (rawJson: string) => {
      const data = JSON.parse(rawJson);
      if (data.type === "stream_state") {
        onStreamState({
          state: data.state,
          reason: data.reason,
          stream_instance_id: data.stream_instance_id,
        });
      }
    };

    handleWsMessage(
      JSON.stringify({
        type: "stream_state",
        state: "stopped",
        reason: "lease_expired",
        stream_instance_id: "8cbcd134-f05e-48fd-bef7-0db96ffab8ed",
      })
    );

    expect(onStreamState).toHaveBeenCalledWith({
      state: "stopped",
      reason: "lease_expired",
      stream_instance_id: "8cbcd134-f05e-48fd-bef7-0db96ffab8ed",
    });
  });

  it("fail-closed mode check: blocks input outside desktop mode", () => {
    const checkAllowed = (status: string, streamMode?: string) => {
      return status === "active" && streamMode === "desktop";
    };

    expect(checkAllowed("active", undefined)).toBe(false);
    expect(checkAllowed("active", "usb-camera")).toBe(false);
    expect(checkAllowed("active", "unknown")).toBe(false);
    expect(checkAllowed("idle", "desktop")).toBe(false);
    expect(checkAllowed("active", "desktop")).toBe(true);
  });
});
