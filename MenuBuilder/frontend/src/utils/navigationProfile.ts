import { useState, useEffect } from "react";
import type { UserInfo } from "../api/auth";

export type NavigationProfile = "l4desk" | "classic";

const PROFILE_STORAGE_KEY = "app_nav_profile";
const PROFILE_CHANGE_EVENT = "app_nav_profile_change";

/**
 * Determine the active navigation profile.
 * - If user.role_id === 5 (l4desk_owner), returns "l4desk".
 * - If URL contains ?profile=l4desk or ?profile=classic, stores and returns that.
 * - If localStorage contains "l4desk", returns "l4desk".
 * - Otherwise defaults to "classic" for existing users.
 */
export function getNavigationProfile(user: UserInfo | null): NavigationProfile {
  if (typeof window !== "undefined") {
    // Check URL parameters for explicit override/testing
    const params = new URLSearchParams(window.location.search);
    const paramProfile = params.get("profile");
    if (paramProfile === "l4desk" || paramProfile === "classic") {
      try {
        localStorage.setItem(PROFILE_STORAGE_KEY, paramProfile);
      } catch {
        // ignore storage errors
      }
      return paramProfile;
    }
  }

  // L4Desk self-registered owners always default to l4desk profile
  if (user?.role_id === 5 || user?.role === "l4desk_owner") {
    return "l4desk";
  }

  if (typeof window !== "undefined") {
    try {
      const stored = localStorage.getItem(PROFILE_STORAGE_KEY);
      if (stored === "l4desk") {
        return "l4desk";
      }
    } catch {
      // ignore storage errors
    }
  }

  return "classic";
}

/**
 * Set the navigation profile and notify listeners.
 */
export function setNavigationProfile(profile: NavigationProfile): void {
  if (typeof window === "undefined") return;
  try {
    localStorage.setItem(PROFILE_STORAGE_KEY, profile);
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
    const handleProfileChange = (e: Event) => {
      const customEvent = e as CustomEvent<NavigationProfile>;
      if (customEvent.detail) {
        setProfileState(customEvent.detail);
      } else {
        setProfileState(getNavigationProfile(user));
      }
    };

    window.addEventListener(PROFILE_CHANGE_EVENT, handleProfileChange);
    window.addEventListener("storage", handleProfileChange);
    return () => {
      window.removeEventListener(PROFILE_CHANGE_EVENT, handleProfileChange);
      window.removeEventListener("storage", handleProfileChange);
    };
  }, [user]);

  const updateProfile = (newProfile: NavigationProfile) => {
    setNavigationProfile(newProfile);
    setProfileState(newProfile);
  };

  return [profile, updateProfile];
}
