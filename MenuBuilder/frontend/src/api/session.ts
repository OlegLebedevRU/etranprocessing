import { refreshAuthToken } from "./auth";

export type SessionEvent =
    | { type: "token-refreshed"; expiresAt: number }
    | { type: "tenant-switched"; orgId: number; orgName: string }
    | { type: "logout" };

const sessionChannel: BroadcastChannel | null =
    typeof window !== "undefined" && "BroadcastChannel" in window
        ? new BroadcastChannel("mb-session")
        : null;

let refreshTimer: ReturnType<typeof setTimeout> | null = null;
let targetExpiresAtTimestamp = 0; // ms epoch

if (sessionChannel) {
  sessionChannel.onmessage = (event: MessageEvent<SessionEvent>) => {
    const data = event.data;
    if (!data) return;
    if (data.type === "tenant-switched") {
      window.location.reload();
    } else if (data.type === "logout") {
      if (window.location.pathname !== "/login") {
        window.location.href = "/login";
      }
    } else if (data.type === "token-refreshed") {
      // Another tab refreshed the shared cookie — re-align our own timer instead of refreshing again
      const remainingSec = Math.floor((data.expiresAt - Date.now()) / 1000);
      if (remainingSec > 0) {
        scheduleRefresh(remainingSec);
      }
    }
  };
}

export function notifySessionEvent(event: SessionEvent): void {
  if (sessionChannel) {
    sessionChannel.postMessage(event);
  }
}

export function scheduleRefresh(expiresInSec: number): void {
  const env = (import.meta as unknown as { env?: Record<string, string | undefined> }).env;
  if (env?.VITE_SILENT_REFRESH === "false") {
    return;
  }
  if (refreshTimer) {
    clearTimeout(refreshTimer);
    refreshTimer = null;
  }

  targetExpiresAtTimestamp = Date.now() + expiresInSec * 1000;

  // Proactive silent refresh: 300s (5 minutes) before expiration, minimum 30s
  const delaySec = Math.max(expiresInSec - 300, 30);
  refreshTimer = setTimeout(() => {
    if (typeof document !== "undefined" && document.hidden) {
      // Background tab: skip; visibilitychange handler will refresh on return
      return;
    }
    void doSilentRefresh();
  }, delaySec * 1000);
}

export async function doSilentRefresh(): Promise<void> {
  try {
    const result = await refreshAuthToken();
    scheduleRefresh(result.expires_in);
    notifySessionEvent({
      type: "token-refreshed",
      expiresAt: Date.now() + result.expires_in * 1000,
    });
  } catch (err) {
    console.warn("Silent token refresh failed, fallback to reactive refresh:", err);
  }
}

if (typeof document !== "undefined") {
  document.addEventListener("visibilitychange", () => {
    if (!document.hidden && targetExpiresAtTimestamp > 0) {
      const remainingMs = targetExpiresAtTimestamp - Date.now();
      if (remainingMs < 300_000) {
        void doSilentRefresh();
      }
    }
  });
}