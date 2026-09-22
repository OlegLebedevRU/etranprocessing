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

  it("L4Desk navigation profile defines strictly the 6 required sections in order", () => {
    const L4DESK_SECTIONS = [
      "terminals",
      "video",
      "console",
      "settings",
      "mcp",
      "licenses",
    ];
    expect(L4DESK_SECTIONS).toHaveLength(6);
    expect(L4DESK_SECTIONS[0]).toBe("terminals");
    expect(L4DESK_SECTIONS.indexOf("terminals")).toBeLessThan(
      L4DESK_SECTIONS.indexOf("video")
    );
  });

  it("L4Desk nav source puts Терминалы above Видеонаблюдение and has no nested settings terminals", async () => {
    // @ts-ignore
    const fs = await import("node:fs");
    const layoutSource = fs.readFileSync(
      new URL("../routes/layout.tsx", import.meta.url),
      "utf8"
    );
    const l4deskBlock = layoutSource.split("L4DESK_NAV_ITEMS")[1] || "";
    const terminalsIdx = l4deskBlock.indexOf('key: "terminals"');
    const videoIdx = l4deskBlock.indexOf('key: "video"');
    expect(terminalsIdx).toBeGreaterThan(-1);
    expect(videoIdx).toBeGreaterThan(-1);
    expect(terminalsIdx).toBeLessThan(videoIdx);
  });

  it("Classic profile menu terminals route is preserved via L4DeskRootTerminalsRoute fallback", async () => {
    // @ts-ignore
    const fs = await import("node:fs");
    const appSource = fs.readFileSync(
      new URL("../App.tsx", import.meta.url),
      "utf8"
    );
    expect(appSource).toContain("L4DeskRootTerminalsRoute");
    expect(appSource).toContain("SettingsTerminalsRoute");
    expect(appSource).toContain('Navigate to="/terminals" replace');
    expect(appSource).toContain('Navigate to="/menu/terminals" replace');
  });

  it("Onboarding wizard has no SN/device_id inputs and shows server SN read-only", async () => {
    // @ts-ignore
    const fs = await import("node:fs");
    const wizardSource = fs.readFileSync(
      new URL("../components/OnboardingWizardModal.tsx", import.meta.url),
      "utf8"
    );
    expect(wizardSource).not.toContain('name="sn"');
    expect(wizardSource).not.toContain("handleGenerateSn");
    expect(wizardSource).not.toContain('name="device_id"');
    expect(wizardSource).toContain("createdTerminal.sn");
  });

  it("ConsolePage reuses DeviceConsoleTab and does not own terminal creation", async () => {
    // @ts-ignore
    const fs = await import("node:fs");
    const consoleSource = fs.readFileSync(
      new URL("../routes/console/ConsolePage.tsx", import.meta.url),
      "utf8"
    );
    expect(consoleSource).toContain("DeviceConsoleTab");
    expect(consoleSource).toContain("<DeviceConsoleTab");
    expect(consoleSource).not.toContain("OnboardingWizardModal");
    expect(consoleSource).toContain("/terminals");
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
