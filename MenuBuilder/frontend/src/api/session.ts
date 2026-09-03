import { refreshAuthToken } from "./auth";

export type SessionEvent =
  | { type: "token-refreshed"; expiresAt: number }
  | { type: "tenant-switched"; orgId: number; orgName: string }
  | { type: "logout" };

const sessionChannel: BroadcastChannel | null =
  typeof window !== "undefined" && "BroadcastChannel" in window
    ? new BroadcastChannel("mb-session")
    : null;

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
    }
  };
}

export function notifySessionEvent(event: SessionEvent): void {
  if (sessionChannel) {
    sessionChannel.postMessage(event);
  }
}

let refreshTimer: ReturnType<typeof setTimeout> | null = null;
let targetExpiresAtTimestamp: number = 0; // ms epoch

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
  const delayMs = delaySec * 1000;

  refreshTimer = setTimeout(() => {
    if (typeof document !== "undefined" && document.hidden) {
      // Do not fire in background tab; lazy refresh will fire on tab visibilitychange
      return;
    }
    doSilentRefresh();
  }, delayMs);
}

export async function doSilentRefresh(): Promise<void> {
  try {
    const result = await refreshAuthToken();
    if (result.access_token) {
      localStorage.setItem("mb_token", result.access_token);
    }
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
      // If less than 5 minutes remaining (300_000 ms), trigger refresh immediately
      if (remainingMs < 300_000) {
        doSilentRefresh();
      }
    }
  });
}
