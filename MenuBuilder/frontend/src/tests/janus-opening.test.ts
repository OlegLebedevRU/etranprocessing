import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { JanusStreamingClient } from "../api/janusClient";

class Socket {
  static instances: Socket[] = [];
  static OPEN = 1;
  readyState = 0;
  onopen: (() => void) | null = null;
  onerror: (() => void) | null = null;
  onclose: (() => void) | null = null;
  close = vi.fn();
  send = vi.fn();
  constructor() { Socket.instances.push(this); }
}

describe("opening a Janus socket", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    Socket.instances = [];
    vi.stubGlobal("WebSocket", Socket);
  });
  afterEach(() => {
    vi.unstubAllGlobals();
    vi.useRealTimers();
  });
  function client() {
    return new JanusStreamingClient({ wsUrl: "ws://localhost/janus-ws", mountpointId: 1, onRemoteTrack: vi.fn() });
  }
  it("times out twice without creating a session", async () => {
    const result = expect(client().start()).rejects.toThrow("двух попыток");
    await vi.advanceTimersByTimeAsync(13000);
    await result;
    expect(Socket.instances).toHaveLength(2);
    for (const ws of Socket.instances) expect(ws.send).not.toHaveBeenCalled();
  });
  it("cancels a pending retry when the viewer leaves", async () => {
    const c = client();
    const result = expect(c.start()).rejects.toMatchObject({ name: "AbortError" });
    Socket.instances[0].onerror?.();
    await c.stop();
    await vi.advanceTimersByTimeAsync(13000);
    await result;
    expect(Socket.instances).toHaveLength(1);
  });
  it("creates exactly one Janus session after a successful retry", async () => {
    const c = client();
    const result = expect(c.start()).rejects.toThrow("stopped");
    Socket.instances[0].onclose?.();
    await vi.advanceTimersByTimeAsync(500);
    const ws = Socket.instances[1];
    ws.readyState = Socket.OPEN;
    ws.onopen?.();
    await Promise.resolve();
    expect(ws.send).toHaveBeenCalledTimes(1);
    expect(JSON.parse(ws.send.mock.calls[0][0]).janus).toBe("create");
    await c.stop();
    await result;
  });
});
