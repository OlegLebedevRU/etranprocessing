import { afterEach, expect, it, vi } from "vitest";
import { attachPortalSocket, openPortalChannel } from "../utils/portalSocket";

afterEach(() => vi.unstubAllGlobals());

it("multiplexes independent channels and closes the transport with the user session", () => {
  const sockets: FakeSocket[] = [];
  class FakeSocket {
    static CONNECTING = 0;
    static OPEN = 1;
    static CLOSING = 2;
    static CLOSED = 3;
    readyState = 0;
    send = vi.fn();
    close = vi.fn();
    onopen?: () => void;
    constructor() { sockets.push(this); }
  }
  const location = new URL("https://portal.example.test/");
  vi.stubGlobal("window", { location });
  vi.stubGlobal("location", location);
  vi.stubGlobal("WebSocket", FakeSocket);
  vi.stubGlobal("CloseEvent", class { constructor(readonly type: string) {} });
  const detach = attachPortalSocket();
  const a = openPortalChannel({ channel: "watch", device: 773 });
  openPortalChannel({ channel: "changes", device: null, org: 7 });
  expect(sockets).toHaveLength(1);
  sockets[0].readyState = 1;
  sockets[0].onopen?.();
  expect(sockets[0].send).toHaveBeenCalledTimes(2);
  a.close();
  expect(sockets[0].close).not.toHaveBeenCalled();
  detach();
  expect(sockets[0].close).toHaveBeenCalledOnce();
});
