import { createContext, useCallback, useContext, useEffect, useRef, useState, type ReactNode } from "react";
import client from "../api/client";

const Context = createContext({ count: 0, refresh: () => {} });

/** One browser-owned poll for both badges; no requests for ordinary users or hidden tabs. */
export function AdminDeviceEventsProvider({ enabled, children }: { enabled: boolean; children: ReactNode }) {
  const [count, setCount] = useState(0);
  const active = useRef(false);
  const queued = useRef(false);
  const permitted = useRef(enabled);
  permitted.current = enabled;
  const mounted = useRef(false);
  const refresh = useCallback(async () => {
    if (!permitted.current || !mounted.current || document.visibilityState === "hidden") return;
    if (active.current) { queued.current = true; return; }
    active.current = true;
    try {
      const { data } = await client.get<{ new_count: number }>("/admin/device-events/count");
      if (mounted.current && permitted.current && Number.isSafeInteger(data.new_count) && data.new_count >= 0) setCount(data.new_count);
    } catch {
      // Keep the last known count during a transient failure; never manufacture zero.
    } finally {
      active.current = false;
      if (queued.current && mounted.current) { queued.current = false; void refresh(); }
    }
  }, [enabled]);
  useEffect(() => {
    mounted.current = true;
    if (!enabled) setCount(0);
    void refresh();
    const timer = enabled ? window.setInterval(() => void refresh(), 60_000) : undefined;
    const onVisible = () => { void refresh(); };
    if (enabled) document.addEventListener("visibilitychange", onVisible);
    return () => {
      mounted.current = false;
      window.clearInterval(timer);
      document.removeEventListener("visibilitychange", onVisible);
    };
  }, [enabled, refresh]);
  return <Context.Provider value={{ count: enabled ? count : 0, refresh }}>{children}</Context.Provider>;
}

export const useAdminDeviceEvents = () => useContext(Context);
