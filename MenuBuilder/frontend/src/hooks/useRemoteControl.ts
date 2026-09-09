import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  acquireControlLease,
  getControlWsUrl,
  releaseControlLease,
  ClickResult,
  ControlAgentStatus,
  ControlLease,
  ControlWsInbound,
  ControlWsOutbound,
} from "../api/video";

// Roadmap note: in future versions, 'enable control' flow may negotiate ffmpeg parameters
// (resolution/fps/without pad) over the control channel; currently alpha coordinate model accounts for presence.screen.

export type RemoteControlStatus =
  | "idle"
  | "acquiring"
  | "active"
  | "busy"
  | "agent_offline"
  | "desktop_locked"
  | "error";

export interface UseRemoteControlOptions {
  deviceId: number | null;
  isSessionActive: boolean;
  onErrorMessage?: (msg: string) => void;
  onClickResult?: (result: ClickResult) => void;
}

export function useRemoteControl({
  deviceId,
  isSessionActive,
  onErrorMessage,
  onClickResult,
}: UseRemoteControlOptions) {
  const [status, setStatus] = useState<RemoteControlStatus>("idle");
  const [presence, setPresence] = useState<ControlAgentStatus | null>(null);
  const [lease, setLease] = useState<ControlLease | null>(null);
  const [lastClickResult, setLastClickResult] = useState<ClickResult | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [busyOwner, setBusyOwner] = useState<string | null>(null);

  const wsRef = useRef<WebSocket | null>(null);
  const leaseRef = useRef<ControlLease | null>(null);
  const helloTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const activeDeviceIdRef = useRef<number | null>(deviceId);
  activeDeviceIdRef.current = deviceId;

  // Pending move throttling
  const lastMoveSentTimeRef = useRef<number>(0);
  const pendingMoveRef = useRef<{ x: number; y: number } | null>(null);
  const moveTimerRef = useRef<ReturnType<typeof setTimeout>>(null);

  // Pending click promises (client_ref -> resolver)
  const pendingClicksRef = useRef<
    Map<
      string,
      {
        resolve: (res: ClickResult) => void;
        timer: ReturnType<typeof setTimeout>;
      }
    >
  >(new Map());

  // Keepalive timer & last inbound message timestamp
  const lastInboundTimeRef = useRef<number>(Date.now());
  const keepaliveTimerRef = useRef<ReturnType<typeof setInterval>>(null);

  const clearMoveThrottle = () => {
    if (moveTimerRef.current) {
      clearTimeout(moveTimerRef.current);
      moveTimerRef.current = null;
    }
    pendingMoveRef.current = null;
  };

  const clearPendingClicks = () => {
    pendingClicksRef.current.forEach(({ resolve, timer }) => {
      clearTimeout(timer);
      resolve({ result: "unconfirmed" });
    });
    pendingClicksRef.current.clear();
  };

  const clearKeepalive = () => {
    if (keepaliveTimerRef.current) {
      clearInterval(keepaliveTimerRef.current);
      keepaliveTimerRef.current = null;
    }
  };

  const sendWsMessage = useCallback((msg: ControlWsInbound): boolean => {
    if (wsRef.current && wsRef.current.readyState === WebSocket.OPEN) {
      wsRef.current.send(JSON.stringify(msg));
      return true;
    }
    return false;
  }, []);

  const disable = useCallback(
    async (reason = "normal") => {
      clearMoveThrottle();
      clearPendingClicks();
      clearKeepalive();

      if (helloTimeoutRef.current) {
        clearTimeout(helloTimeoutRef.current);
        helloTimeoutRef.current = null;
      }

      if (reason !== "failed") {
        setStatus("idle");
        setErrorMessage(null);
        setBusyOwner(null);
      }

      const currentLease = leaseRef.current;
      const targetDeviceId = activeDeviceIdRef.current;
      leaseRef.current = null;
      setLease(null);

      // Send release message if WS is open and detach handlers
      if (wsRef.current) {
        const socket = wsRef.current;
        wsRef.current = null;
        socket.onopen = null;
        socket.onclose = null;
        socket.onerror = null;
        socket.onmessage = null;
        if (socket.readyState === WebSocket.OPEN) {
          try {
            socket.send(JSON.stringify({ type: "release" }));
          } catch {
            // ignore
          }
        }
        try {
          socket.close();
        } catch {
          // ignore
        }
      }

      // Best-effort DELETE lease
      if (currentLease && targetDeviceId !== null) {
        try {
          await releaseControlLease(targetDeviceId, currentLease.lease_id);
        } catch {
          // best-effort
        }
      }
    },
    []
  );

  const enable = useCallback(async () => {
    if (!deviceId || !isSessionActive) {
      return;
    }

    setStatus("acquiring");
    setErrorMessage(null);
    setBusyOwner(null);

    try {
      // 1. Acquire control lease
      const leaseData = await acquireControlLease(deviceId);
      if (activeDeviceIdRef.current !== deviceId) {
        void releaseControlLease(deviceId, leaseData.lease_id).catch(() => {});
        return;
      }

      leaseRef.current = leaseData;
      setLease(leaseData);

      // 2. Open WebSocket
      const wsUrl = getControlWsUrl(leaseData.ws_path);
      const ws = new WebSocket(wsUrl);
      wsRef.current = ws;

      // 3. Connect & wait for "hello"
      const helloPromise = new Promise<void>((resolve, reject) => {
        helloTimeoutRef.current = setTimeout(() => {
          helloTimeoutRef.current = null;
          reject(new Error("Таймаут инициализации WebSocket"));
        }, 5000);

        ws.onopen = () => {
          if (wsRef.current !== ws) return;
          // Keepalive interval: send keepalive if 10s no incoming message
          lastInboundTimeRef.current = Date.now();
          clearKeepalive();
          keepaliveTimerRef.current = setInterval(() => {
            if (Date.now() - lastInboundTimeRef.current >= 10000) {
              sendWsMessage({ type: "keepalive" });
            }
          }, 2000);
        };

        ws.onmessage = (event) => {
          if (wsRef.current !== ws) return;
          lastInboundTimeRef.current = Date.now();
          try {
            const data: ControlWsOutbound = JSON.parse(event.data);
            if (data.type === "hello") {
              if (helloTimeoutRef.current) {
                clearTimeout(helloTimeoutRef.current);
                helloTimeoutRef.current = null;
              }
              setStatus("active");
              resolve();
              return;
            }

            if (data.type === "presence") {
              setPresence({
                online: data.status === "online",
                desktop_available: data.desktop_available,
                screen: data.screen,
              });
              if (data.status === "offline") {
                setStatus("agent_offline");
              } else if (!data.desktop_available) {
                setStatus("desktop_locked");
              } else {
                setStatus("active");
              }
              return;
            }

            if (data.type === "click_result") {
              const res: ClickResult = {
                command_id: data.command_id,
                client_ref: data.client_ref,
                result: data.result,
                code: data.code,
                message: data.message,
                latency_ms: data.latency_ms,
              };
              setLastClickResult(res);
              if (onClickResult) {
                onClickResult(res);
              }
              if (data.client_ref && pendingClicksRef.current.has(data.client_ref)) {
                const pending = pendingClicksRef.current.get(data.client_ref)!;
                clearTimeout(pending.timer);
                pending.resolve(res);
                pendingClicksRef.current.delete(data.client_ref);
              }
              return;
            }

            if (data.type === "error") {
              const msg = data.code === "rate_limited"
                ? "Слишком частые действия"
                : data.message || `Ошибка: ${data.code}`;
              setErrorMessage(msg);
              if (onErrorMessage) {
                onErrorMessage(msg);
              }
              return;
            }

            if (data.type === "lease_revoked") {
              const msg = `Сессия управления завершена: ${data.reason || "отзыв аренды"}`;
              setErrorMessage(msg);
              if (onErrorMessage) {
                onErrorMessage(msg);
              }
              void disable("revoked");
              return;
            }
          } catch {
            // invalid JSON from server
          }
        };

        ws.onerror = () => {
          if (helloTimeoutRef.current) {
            clearTimeout(helloTimeoutRef.current);
            helloTimeoutRef.current = null;
          }
          if (wsRef.current !== ws) return;
          reject(new Error("Ошибка соединения WebSocket"));
        };

        ws.onclose = (ev) => {
          if (helloTimeoutRef.current) {
            clearTimeout(helloTimeoutRef.current);
            helloTimeoutRef.current = null;
          }
          if (wsRef.current !== ws) return;
          if (ev.code === 4403) {
            setStatus("busy");
            void disable("failed");
          } else {
            void disable("closed");
          }
        };
      });

      await helloPromise;
    } catch (err: any) {
      if (wsRef.current === null && leaseRef.current === null) {
        // Was explicitly disabled / aborted while acquiring or connecting
        return;
      }
      const respData = err?.response?.data;
      if (err?.response?.status === 409 || respData?.detail === "lease busy") {
        setStatus("busy");
        const owner = respData?.owner_user_id || "другой оператор";
        setBusyOwner(owner);
        const msg = `Управление занято другим оператором (#${owner})`;
        setErrorMessage(msg);
        if (onErrorMessage) onErrorMessage(msg);
      } else {
        setStatus("error");
        const msg = err?.message || err?.response?.data?.detail || "Ошибка включения управления";
        setErrorMessage(msg);
        if (onErrorMessage) onErrorMessage(msg);
      }
      await disable("failed");
    }
  }, [deviceId, isSessionActive, disable, onErrorMessage, onClickResult, sendWsMessage]);

  const sendMove = useCallback(
    (x: number, y: number) => {
      if (status !== "active") return;

      const now = Date.now();
      const throttleIntervalMs = 100; // <= 10 moves per second

      pendingMoveRef.current = { x, y };

      if (now - lastMoveSentTimeRef.current >= throttleIntervalMs) {
        lastMoveSentTimeRef.current = now;
        sendWsMessage({ type: "pointer_move", x, y });
        pendingMoveRef.current = null;
      } else if (!moveTimerRef.current) {
        const remaining = throttleIntervalMs - (now - lastMoveSentTimeRef.current);
        moveTimerRef.current = setTimeout(() => {
          moveTimerRef.current = null;
          lastMoveSentTimeRef.current = Date.now();
          if (pendingMoveRef.current) {
            sendWsMessage({
              type: "pointer_move",
              x: pendingMoveRef.current.x,
              y: pendingMoveRef.current.y,
            });
            pendingMoveRef.current = null;
          }
        }, remaining);
      }
    },
    [status, sendWsMessage]
  );

  const sendClick = useCallback(
    async (x: number, y: number): Promise<ClickResult> => {
      if (status !== "active") {
        return { result: "unconfirmed", message: "Управление не активно" };
      }

      // 1. Flush any pending move or send final move with same coordinates
      clearMoveThrottle();
      sendWsMessage({ type: "pointer_move", x, y });
      lastMoveSentTimeRef.current = Date.now();

      // 2. Prepare mouse_click with client_ref
      const clientRef = `c_${Date.now()}_${Math.random().toString(36).slice(2, 8)}`;

      return new Promise<ClickResult>((resolve) => {
        const timer = setTimeout(() => {
          pendingClicksRef.current.delete(clientRef);
          const unconfResult: ClickResult = {
            client_ref: clientRef,
            result: "unconfirmed",
            message: "Клик не подтверждён — повторите вручную",
          };
          setLastClickResult(unconfResult);
          if (onClickResult) {
            onClickResult(unconfResult);
          }
          resolve(unconfResult);
        }, 7000);

        pendingClicksRef.current.set(clientRef, { resolve, timer });

        const sent = sendWsMessage({
          type: "mouse_click",
          x,
          y,
          button: "left",
          client_ref: clientRef,
        });

        if (!sent) {
          clearTimeout(timer);
          pendingClicksRef.current.delete(clientRef);
          const failResult: ClickResult = {
            result: "unconfirmed",
            message: "Не удалось отправить команду клика",
          };
          resolve(failResult);
        }
      });
    },
    [status, sendWsMessage, onClickResult]
  );

  // Auto-disable when session stops
  useEffect(() => {
    if (!isSessionActive && status !== "idle") {
      void disable("session_stopped");
    }
  }, [isSessionActive, status, disable]);

  const prevDeviceIdRef = useRef<number | null>(deviceId);
  useEffect(() => {
    if (prevDeviceIdRef.current !== deviceId) {
      prevDeviceIdRef.current = deviceId;
      void disable("device_changed");
    }
  }, [deviceId, disable]);

  // Cleanup on unmount or broadcast logout
  useEffect(() => {
    const channel =
      typeof window !== "undefined" && "BroadcastChannel" in window
        ? new BroadcastChannel("mb-session")
        : null;

    if (channel) {
      channel.onmessage = (event) => {
        if (event.data?.type === "logout") {
          void disable("logout");
        }
      };
    }

    return () => {
      if (channel) {
        channel.close();
      }
      void disable("unmount");
    };
  }, [disable]);

  return useMemo(
    () => ({
      status,
      presence,
      lease,
      lastClickResult,
      errorMessage,
      busyOwner,
      enable,
      disable,
      sendMove,
      sendClick,
      setPresence,
    }),
    [
      status,
      presence,
      lease,
      lastClickResult,
      errorMessage,
      busyOwner,
      enable,
      disable,
      sendMove,
      sendClick,
    ]
  );
}
