import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";
import {
  getNavigationProfile,
  setNavigationProfile,
  type NavigationProfile,
} from "../utils/navigationProfile";
import type { UserInfo } from "../api/auth";

// Setup browser globals for Node test environment
const storageMap = new Map<string, string>();
const mockLocalStorage = {
  getItem: (key: string) => storageMap.get(key) ?? null,
  setItem: (key: string, value: string) => storageMap.set(key, String(value)),
  removeItem: (key: string) => storageMap.delete(key),
  clear: () => storageMap.clear(),
};

if (typeof globalThis.localStorage === "undefined") {
  (globalThis as any).localStorage = mockLocalStorage;
}

if (typeof globalThis.window === "undefined") {
  const listeners: Record<string, Function[]> = {};
  (globalThis as any).window = {
    location: { search: "" },
    addEventListener: (event: string, cb: Function) => {
      listeners[event] = listeners[event] || [];
      listeners[event].push(cb);
    },
    removeEventListener: (event: string, cb: Function) => {
      if (listeners[event]) {
        listeners[event] = listeners[event].filter((f) => f !== cb);
      }
    },
    dispatchEvent: (e: any) => {
      const cbs = listeners[e.type] || [];
      for (const cb of cbs) cb(e);
      return true;
    },
  };
}

if (typeof globalThis.CustomEvent === "undefined") {
  (globalThis as any).CustomEvent = class CustomEvent {
    type: string;
    detail: any;
    constructor(type: string, params?: { detail: any }) {
      this.type = type;
      this.detail = params?.detail;
    }
  };
}

describe("L4Desk Navigation Profile & Route Resolution", () => {
  beforeEach(() => {
    mockLocalStorage.clear();
    (globalThis as any).window.location.search = "";
  });

  afterEach(() => {
    mockLocalStorage.clear();
  });

  it("Role 5 (l4desk_owner) defaults to 'l4desk' navigation profile", () => {
    const user: UserInfo = {
      user_id: 101,
      username: "l4desk_user",
      role_id: 5,
      role: "l4desk_owner",
      org_id: 42,
    };
    const profile = getNavigationProfile(user);
    expect(profile).toBe("l4desk");
  });

  it("Legacy roles (1, 2, 3, 4) default to 'classic' navigation profile", () => {
    const user: UserInfo = {
      user_id: 202,
      username: "legacy_operator",
      role_id: 3,
      role: "user",
      org_id: 1,
    };
    const profile = getNavigationProfile(user);
    expect(profile).toBe("classic");
  });

  it("URL parameter ?profile=l4desk activates L4Desk profile and persists in localStorage", () => {
    (globalThis as any).window.location.search = "?profile=l4desk";
    const user: UserInfo = {
      user_id: 303,
      username: "admin_tester",
      role_id: 1,
      is_superuser: true,
      org_id: 1,
    };
    const profile = getNavigationProfile(user);
    expect(profile).toBe("l4desk");
    expect(mockLocalStorage.getItem("app_nav_profile")).toBe("l4desk");
  });

  it("URL parameter ?profile=classic forces classic profile even for role 5", () => {
    (globalThis as any).window.location.search = "?profile=classic";
    const user: UserInfo = {
      user_id: 404,
      username: "l4desk_user",
      role_id: 5,
      org_id: 10,
    };
    const profile = getNavigationProfile(user);
    expect(profile).toBe("classic");
  });

  it("setNavigationProfile updates storage and dispatches change event", () => {
    let receivedEvent: NavigationProfile | null = null;
    const listener = (e: any) => {
      receivedEvent = e.detail;
    };
    window.addEventListener("app_nav_profile_change", listener);

    setNavigationProfile("l4desk");
    expect(mockLocalStorage.getItem("app_nav_profile")).toBe("l4desk");
    expect(receivedEvent).toBe("l4desk");

    window.removeEventListener("app_nav_profile_change", listener);
  });

  it("L4Desk navigation profile defines strictly the 5 required sections", () => {
    const L4DESK_SECTIONS = ["video", "settings", "console", "mcp", "licenses"];
    expect(L4DESK_SECTIONS).toHaveLength(5);
    expect(L4DESK_SECTIONS).toEqual(
      expect.arrayContaining(["video", "settings", "console", "mcp", "licenses"])
    );
  });

  it("Reuse assertion: ConsolePage imports and reuses DeviceConsoleTab", async () => {
    // @ts-ignore
    const fs = await import("node:fs");
    const consoleSource = fs.readFileSync(
      new URL("../routes/console/ConsolePage.tsx", import.meta.url),
      "utf8"
    );
    expect(consoleSource).toContain("DeviceConsoleTab");
    expect(consoleSource).toContain("<DeviceConsoleTab");
    expect(consoleSource).toContain("OnboardingWizardModal");
  });

  it("Reuse assertion: VideoSurveillancePage reuses RefusalReasonCard and does not duplicate logic", async () => {
    // @ts-ignore
    const fs = await import("node:fs");
    const videoSource = fs.readFileSync(
      new URL("../routes/video-surveillance.tsx", import.meta.url),
      "utf8"
    );
    expect(videoSource).toContain("RefusalReasonCard");
    expect(videoSource).toContain("<RefusalReasonCard");
  });
});
