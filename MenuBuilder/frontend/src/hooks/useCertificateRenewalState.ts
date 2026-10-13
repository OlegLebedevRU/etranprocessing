import { useCallback, useEffect, useRef, useState } from "react";
import client from "../api/client";
import { useSession } from "../session/SessionContext";

export type RenewalSnapshot = {
  allowed: boolean; reason?: string; status: string; expires_at?: string; observed_at?: string;
  installation_pending?: boolean; recovery_pin_id?: number; recoverable?: boolean;
  requires_intervention?: boolean; exit_code?: number;
};

export function isRenewalInProgress(snapshot?: RenewalSnapshot): boolean {
  return !!snapshot && (!!snapshot.installation_pending ||
    snapshot.status === "pending" || snapshot.status === "issued_waiting_identity");
}

export function useCertificateRenewalState(deviceId: number, enabled = true) {
  const { user } = useSession();
  const [snapshot, setSnapshot] = useState<RenewalSnapshot>();
  const [loading, setLoading] = useState(enabled);
  const [loadFailed, setLoadFailed] = useState(false);
  const generation = useRef(0);
  const refresh = useCallback(async () => {
    if (!enabled) return;
    const current = ++generation.current;
    setLoading(true); setLoadFailed(false);
    try {
      const { data } = await client.get<RenewalSnapshot>(`/devices/${deviceId}/certificate-renewal`);
      if (current === generation.current) setSnapshot(data);
    } catch {
      if (current === generation.current) {
        setLoadFailed(true);
        setSnapshot(previous => ({ ...previous, allowed: false,
          reason: "Проверка продления недоступна", status: previous?.status ?? "none" }));
      }
    } finally { if (current === generation.current) setLoading(false); }
  }, [deviceId, user?.org_id, enabled]);
  useEffect(() => {
    setSnapshot(undefined); void refresh();
    return () => { generation.current++; };
  }, [refresh]);
  const markQueued = (expiresAt: string) => {
    generation.current++; setLoading(false); setLoadFailed(false);
    setSnapshot(previous => ({ ...previous, allowed: previous?.allowed ?? false,
      status: "pending", expires_at: expiresAt }));
  };
  return { snapshot, loading, loadFailed, refresh, markQueued };
}

export type CertificateRenewalState = ReturnType<typeof useCertificateRenewalState>;
