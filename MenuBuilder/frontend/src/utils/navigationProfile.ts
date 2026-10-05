import { useState, useEffect } from "react";
import type { UserInfo } from "../api/auth";

export type NavigationProfile = "l4desk" | "classic";

const PROFILE_STORAGE_KEY = "app_nav_profile";
const PROFILE_CHANGE_EVENT = "app_nav_profile_change";

/**
 * Role 5 always uses L4Desk. Tenant site restrictions still gate site access;
 * stored choices and URL overrides only apply to roles allowed to switch.
 */
function storageKey(user: UserInfo | null): string {
  return `${PROFILE_STORAGE_KEY}:${user?.org_id ?? "platform"}`;
}

export function isProfileAllowed(user: UserInfo | null, profile: NavigationProfile): boolean {
  if (user?.role_id === 3 && !user.is_superuser && profile === "l4desk") return false;
  if ((user?.role_id === 5 || user?.role === "l4desk_owner") && profile === "classic") return false;
  return !user?.site_mode || user.site_mode === "both" || user.site_mode === profile;
}

export function getNavigationProfile(user: UserInfo | null): NavigationProfile {
  if (user?.role_id === 3 && !user.is_superuser) return "classic";
  if (user?.role_id === 5 || user?.role === "l4desk_owner") return "l4desk";
  if (user?.site_mode === "classic" || user?.site_mode === "l4desk") {
    return user.site_mode;
  }
  if (typeof window !== "undefined") {
    // Check URL parameters for explicit override/testing
    const params = new URLSearchParams(window.location.search);
    const paramProfile = params.get("profile");
    if (paramProfile === "l4desk" || paramProfile === "classic") {
      try {
        localStorage.setItem(storageKey(user), paramProfile);
      } catch {
        // ignore storage errors
      }
      return paramProfile;
    }
  }

  if (typeof window !== "undefined") {
    try {
      const stored = localStorage.getItem(storageKey(user));
      if (stored === "l4desk" || stored === "classic") {
        return stored;
      }
    } catch {
      // ignore storage errors
    }
  }

  if (user?.role_id === 3) return "classic";
  if (user?.default_site) return user.default_site;

  return "classic";
}

/**
 * Set the navigation profile and notify listeners.
 */
export function setNavigationProfile(profile: NavigationProfile, user: UserInfo | null = null): void {
  if (typeof window === "undefined") return;
  if (!isProfileAllowed(user, profile)) return;
  try {
    localStorage.setItem(storageKey(user), profile);
    window.dispatchEvent(new CustomEvent(PROFILE_CHANGE_EVENT, { detail: profile }));
  } catch {
    // ignore
  }
}

/**
 * React hook to observe and update the current navigation profile.
 */
export function useNavigationProfile(user: UserInfo | null): [
  NavigationProfile,
  (profile: NavigationProfile) => void
] {
  const [profile, setProfileState] = useState<NavigationProfile>(() =>
    getNavigationProfile(user)
  );

  useEffect(() => {
    setProfileState(getNavigationProfile(user));
  }, [user]);

  useEffect(() => {
    const handleProfileChange = () => {
      setProfileState(getNavigationProfile(user));
    };

    window.addEventListener(PROFILE_CHANGE_EVENT, handleProfileChange);
    window.addEventListener("storage", handleProfileChange);
    return () => {
      window.removeEventListener(PROFILE_CHANGE_EVENT, handleProfileChange);
      window.removeEventListener("storage", handleProfileChange);
    };
  }, [user]);

  const updateProfile = (newProfile: NavigationProfile) => {
    if (!isProfileAllowed(user, newProfile)) return;
    setNavigationProfile(newProfile, user);
    setProfileState(newProfile);
  };

  return [profile, updateProfile];
}
