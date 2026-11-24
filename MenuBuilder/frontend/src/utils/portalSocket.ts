/** One transport per authenticated tab. Logical channels retain their own lifecycle. */
type View = { channel: "admin-events" | "licenses" | "mqtt-admin" } | { channel: "monitoring"; page: number; size: number; search: string };
type Topic = View | { channel: "changes"; org: number; device: number | null }
  | { channel: "watch"; device: number }
  | { channel: "control" | "renew"; device: number; lease: string };

let transport: WebSocket | undefined;
let nextId = 0;
let attached = false;
let retry: ReturnType<typeof setTimeout> | undefined;
let retryMs = 1000;
const channels = new Map<number, PortalChannel>();

export class PortalChannel {
  readyState: number = WebSocket.CONNECTING;
  onopen: (() => void) | null = null;
  onmessage: ((event: MessageEvent<string>) => void) | null = null;
  onclose: ((event: CloseEvent) => void) | null = null;
  onerror: (() => void) | null = null;
  readonly id = ++nextId;

  constructor(readonly topic: Topic) {}
  send(data: string) {
    if (this.readyState !== WebSocket.OPEN || transport?.readyState !== WebSocket.OPEN) {
      throw new Error("Portal channel closed");
    }
    transport.send(JSON.stringify({ id: this.id, op: "data", data }));
  }
  close() {
    if (transport?.readyState === WebSocket.OPEN && channels.has(this.id)) {
      transport.send(JSON.stringify({ id: this.id, op: "close" }));
    }
    finish(this, 1000);
  }
}

function finish(channel: PortalChannel, code: number) {
  if (!channels.delete(channel.id)) return;
  channel.readyState = WebSocket.CLOSED;
  channel.onclose?.(new CloseEvent("close", { code }));
}

function subscribe(ws: WebSocket, channel: PortalChannel) {
  ws.send(JSON.stringify({ id: channel.id, op: "open", ...channel.topic }));
}

function resetPortalSocket() {
  attached = false;
  clearTimeout(retry);
  retryMs = 1000;
  const ws = transport;
  transport = undefined;
  ws?.close();
  for (const channel of [...channels.values()]) finish(channel, 4401);
}

export function openPortalChannel(topic: Topic) {
  if (transport && transport.readyState >= WebSocket.CLOSING) {
    transport = undefined;
    for (const entry of [...channels.values()]) finish(entry, 1001);
  }
  const channel = new PortalChannel(topic);
  channels.set(channel.id, channel);
  if (transport?.readyState === WebSocket.OPEN) subscribe(transport, channel);
  else connect();
  return channel;
}

export function attachPortalSocket() {
  attached = true;
  connect();
  return resetPortalSocket;
}

function connect() {
  if (transport) return;
  clearTimeout(retry);
  const url = new URL("/api/v1/video/portal/ws", window.location.href);
  url.protocol = location.protocol === "https:" ? "wss:" : "ws:";
  const ws = new WebSocket(url);
  transport = ws;
  ws.onopen = () => {
    if (ws !== transport) return;
    retryMs = 1000;
    for (const entry of channels.values()) subscribe(ws, entry);
  };
  ws.onmessage = event => {
    if (ws !== transport) return;
    try {
      const msg = JSON.parse(event.data);
      const entry = channels.get(msg.id);
      if (!entry) return;
      if (msg.op === "open") {
        entry.readyState = WebSocket.OPEN;
        entry.onopen?.();
      } else if (msg.op === "data" && entry.readyState === WebSocket.OPEN) {
        entry.onmessage?.(new MessageEvent("message", { data: msg.data }));
      } else if (msg.op === "close") {
        finish(entry, msg.code || 1000);
      }
    } catch { ws.close(); }
  };
  ws.onclose = event => {
    if (ws !== transport) return;
    transport = undefined;
    for (const entry of [...channels.values()]) finish(entry, event.code);
    if (attached && ![4401, 4403].includes(event.code)) {
      retry = setTimeout(connect, retryMs + Math.random() * 500);
      retryMs = Math.min(15000, retryMs * 2);
    }
  };
}

/** HTTP is a compatibility path only before a command has been sent. */
export function renewPortalLease(device: number, lease: string, signal?: AbortSignal): Promise<void> | null {
  if (!transport || transport.readyState !== WebSocket.OPEN) return null;
  signal?.throwIfAborted();
  return new Promise<void>((resolve, reject) => {
    const channel = openPortalChannel({ channel: "renew", device, lease });
    const stop = () => { channel.onclose = null; channel.close(); };
    const abort = () => { stop(); reject(new DOMException("Aborted", "AbortError")); };
    const timer = setTimeout(cancel, 8000);
    function cleanup() {
      clearTimeout(timer);
      signal?.removeEventListener("abort", cancel);
    }
    function cancel() { cleanup(); abort(); }
    signal?.addEventListener("abort", cancel, { once: true });
    channel.onmessage = event => {
      const result = JSON.parse(event.data);
      cleanup();
      if (result.status === 200) resolve();
      else reject(Object.assign(new Error("Lease renewal refused"), { response: { status: result.status, data: { detail: result.detail } } }));
    };
    channel.onclose = () => { cleanup(); reject(new Error("Lease renewal not confirmed")); };
  });
}

/** Share the tab transport and release server-side subscriptions while hidden. */
export function watchPortalView<T>(topic: View, receive: (value: T) => void, fallback: () => void) {
  let channel: PortalChannel | undefined;
  let timer: ReturnType<typeof setTimeout>;
  let delay = 1000;
  let lastFallback = 0;
  const stop = () => {
    clearTimeout(timer);
    if (channel) { channel.onclose = null; channel.close(); channel = undefined; }
  };
  const connectView = () => {
    stop();
    if (document.visibilityState === "hidden") return;
    channel = openPortalChannel(topic);
    channel.onmessage = event => {
      delay = 1000;
      receive(JSON.parse(event.data) as T);
    };
    channel.onclose = event => {
      if ([4401, 4403].includes(event.code)) return;
      if (Date.now() - lastFallback >= 60_000) {
        lastFallback = Date.now();
        fallback();
      }
      timer = setTimeout(connectView, delay + Math.random() * 500);
      delay = Math.min(30_000, delay * 2);
    };
  };
  document.addEventListener("visibilitychange", connectView);
  connectView();
  return () => { document.removeEventListener("visibilitychange", connectView); stop(); };
}
