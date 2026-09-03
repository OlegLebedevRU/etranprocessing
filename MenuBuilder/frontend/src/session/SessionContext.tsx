import React, { createContext, useContext, useEffect, useState } from "react";
import { getMe, type UserInfo } from "../api/auth";
import { scheduleRefresh } from "../api/session";

export interface SessionContextType {
  user: UserInfo | null;
  loading: boolean;
  refreshUser: (forceFresh?: boolean) => Promise<UserInfo | null>;
  setUser: (user: UserInfo | null) => void;
}

const SessionContext = createContext<SessionContextType>({
  user: null,
  loading: true,
  refreshUser: async () => null,
  setUser: () => {},
});

export const SessionProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const [user, setUser] = useState<UserInfo | null>(null);
  const [loading, setLoading] = useState(true);

  // Migration for legacy clients: clean up mb_token / mb_master_token from localStorage
  useEffect(() => {
    if (typeof window !== "undefined") {
      const transport = (import.meta as unknown as { env?: Record<string, string | undefined> }).env
          ?.VITE_AUTH_TRANSPORT;
      if (transport !== "bearer") {
        localStorage.removeItem("mb_token");
        localStorage.removeItem("mb_master_token");
      }
    }
  }, []);

  const refreshUser = async (forceFresh = false): Promise<UserInfo | null> => {
    try {
      const data = await getMe(forceFresh);
      setUser(data);
      if (data?.timezone) {
        localStorage.setItem("org_timezone", data.timezone);
      }
      // After a page reload nobody else schedules the silent refresh — do it from /auth/me
      if (data?.expires_at) {
        const remainingSec = Math.floor((Date.parse(data.expires_at) - Date.now()) / 1000);
        if (remainingSec > 0) {
          scheduleRefresh(remainingSec);
        }
      }
      return data;
    } catch {
      setUser(null);
      return null;
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    void refreshUser();
  }, []);

  return (
      <SessionContext.Provider value={{ user, loading, refreshUser, setUser }}>
        {children}
      </SessionContext.Provider>
  );
};

export const useSession = () => useContext(SessionContext);