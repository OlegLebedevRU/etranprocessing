import { openPortalChannel, type PortalChannel } from "./portalSocket";
// Hints only wake authorized snapshot reads. A missed hint is recovered by the caller's deadline.
type Listener = (device: number | null, kinds: string[]) => void;
const listeners = new Set<Listener>();

export function publishChange(device: number | null, kinds: string[]) {
  for (const listener of listeners) listener(device, kinds);
}

export function onChange(device: number | null, kinds: readonly string[], callback: () => void) {
  const listener: Listener = (target, incoming) => {
    if (target === device && (incoming.includes("sync") || kinds.some(kind => incoming.includes(kind)))) callback();
  };
  listeners.add(listener);
  return () => { listeners.delete(listener); };
}

export function waitForChange(device: number, kinds: string[], signal: AbortSignal, timeout: number) {
  return new Promise<void>((resolve, reject) => {
    const finish = () => { cleanup(); resolve(); };
    const abort = () => { cleanup(); reject(new DOMException("Запуск отменён или истекло время ожидания", "AbortError")); };
    const off = onChange(device, kinds, finish);
    const timer = setTimeout(finish, Math.max(0, timeout));
    function cleanup() { clearTimeout(timer); off(); signal.removeEventListener("abort", abort); }
    signal.addEventListener("abort", abort, { once: true });
    if (signal.aborted) abort();
  });
}

const connections = new Map<string, { count: number; close: () => void }>();

export function watchChanges(org: number | null | undefined, device: number | null = null) {
  if (!org || typeof WebSocket === "undefined") return () => {};
  const key = String(org) + "/" + String(device);
  const existing = connections.get(key);
  if (existing) { existing.count++; return release; }
  let closed = false, retry = 1000;
  let socket: PortalChannel | undefined, timer: ReturnType<typeof setTimeout> | undefined;
  const connect = () => {
    if (closed) return;
    socket = openPortalChannel({ channel: "changes", org: org!, device });
    socket.onopen = () => { retry = 1000; publishChange(device, ["sync"]); };
    socket.onmessage = e => {
      try {
        const notice = JSON.parse(e.data);
        if (notice.type === "change" && Array.isArray(notice.kinds)) publishChange(device, notice.kinds);
      } catch { socket?.close(); }
    };
    socket.onclose = e => {
      if (closed || [4401,4403,4404].includes(e.code)) return;
      timer = setTimeout(connect, retry);
      retry = Math.min(15000, retry * 2);
    };
  };
  connections.set(key, { count: 1, close: () => { closed = true; clearTimeout(timer); socket?.close(); } });
  connect();
  function release() {
    const entry = connections.get(key);
    if (entry && --entry.count === 0) { connections.delete(key); entry.close(); }
  }
  return release;
}
