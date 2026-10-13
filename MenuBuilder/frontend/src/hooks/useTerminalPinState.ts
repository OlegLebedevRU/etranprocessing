import { useCallback, useEffect, useRef, useState } from "react";
import { getTerminalPin, type TerminalPin } from "../api/settings";

export function hasActiveManualPin(pin?: TerminalPin | null): boolean {
  return pin?.status === "issued" && Date.parse(pin.expires_at) > Date.now();
}

export function useTerminalPinState(terminalId: number, orgId: number, enabled: boolean) {
  const [pin, setPin] = useState<TerminalPin | null>();
  const [loading, setLoading] = useState(enabled);
  const [loadFailed, setLoadFailed] = useState(false);
  const generation = useRef(0);
  const reload = useCallback(async () => {
    if (!enabled) return;
    const current = ++generation.current;
    setLoading(true); setLoadFailed(false);
    try {
      const result = await getTerminalPin(terminalId);
      if (current === generation.current) {
        setPin(result);
        const operationKey = `l4desk-pin-operation:${orgId}:${terminalId}`;
        if (result && sessionStorage.getItem(operationKey) === result.operation_id) {
          sessionStorage.removeItem(operationKey);
        }
      }
    }
    catch { if (current === generation.current) setLoadFailed(true); }
    finally { if (current === generation.current) setLoading(false); }
  }, [terminalId, orgId, enabled]);
  useEffect(() => {
    setPin(undefined); void reload();
    return () => { generation.current++; };
  }, [reload]);
  const acceptIssued = (result: TerminalPin) => {
    generation.current++; setPin(result); setLoading(false); setLoadFailed(false);
  };
  return { pin, loading, loadFailed, reload, acceptIssued };
}
export type TerminalPinState = ReturnType<typeof useTerminalPinState>;
