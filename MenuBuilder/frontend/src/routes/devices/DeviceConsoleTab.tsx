import { useState, useEffect, useRef, useCallback, useMemo } from "react";
import {
  Button,
  Input,
  Space,
  Tag,
  Typography,
  message,
  Switch,
  Tooltip,
  Select,
  Progress,
  Badge,
  Alert,
  Segmented,
} from "antd";
import type { InputRef } from "antd";
import {
  PoweroffOutlined,
  ClearOutlined,
  SendOutlined,
  CopyOutlined,
  PlayCircleOutlined,
  StopOutlined,
  WifiOutlined,
  CloseCircleOutlined,
  LoadingOutlined,
  WindowsOutlined,
  CodeOutlined,
  WarningOutlined,
  ApiOutlined,
  FullscreenOutlined,
  FullscreenExitOutlined,
} from "@ant-design/icons";
import { getDiagnosticsWsUrl } from "../../api/devices";
import { useSession } from "../../session/SessionContext";
import {
  acquireControlLease,
  keepaliveControlLease,
  releaseControlLease,
} from "../../api/video";

const { Text } = Typography;

interface DeviceConsoleTabProps {
  sn: string;
  deviceId?: number;
  app?: string;
  sys?: string;
  tags?: Array<{ tag: string; value: string }>;
  orgId: number;
  isActiveTab?: boolean;
}

interface ConsoleLine {
  id: number;
  ts: string;
  text: string;
  kind: "stdout" | "stderr" | "status" | "info" | "sent" | "error";
  seq?: number;
}

type ExecState =
  | "idle"
  | "queued"
  | "notified"
  | "accepted"
  | "running"
  | "cancelling"
  | "completed"
  | "error"
  | "timeout";

const DEFAULT_TIMEOUT_SEC = 30;
const HISTORY_STORAGE_KEY = "l4con_cmd_history";

function generateUUID(): string {
  return "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx".replace(/[xy]/g, (c) => {
    const r = (Math.random() * 16) | 0;
    const v = c === "x" ? r : (r & 0x3) | 0x8;
    return v.toString(16);
  });
}

export default function DeviceConsoleTab({
  sn,
  deviceId,
  sys,
  tags,
  orgId,
  isActiveTab = true,
}: DeviceConsoleTabProps) {
  const { user } = useSession();
  const [leaseId, setLeaseId] = useState<string | null>(null);
  const leaseIdRef = useRef<string | null>(null);
  const keepaliveTimerRef = useRef<number | null>(null);

  // Resolve system tag (windows / esp32 / none)
  const resolvedSys = useMemo(() => {
    if (sys) return sys.toLowerCase().trim();
    if (tags && tags.length > 0) {
      const found = tags.find((t) => t.tag.toLowerCase().trim() === "sys");
      if (found) return found.value.toLowerCase().trim();
    }
    return "";
  }, [sys, tags]);

  const isWindows = resolvedSys === "windows";
  const isEsp32 = resolvedSys === "esp32";
  const hasSupportedSys = isWindows || isEsp32;

  // Session persistence and connection state
  const [keepSession, setKeepSession] = useState<boolean>(false);
  const [connected, setConnected] = useState(false);
  const [connecting, setConnecting] = useState(false);
  const [lines, setLines] = useState<ConsoleLine[]>([]);
  const [commandInput, setCommandInput] = useState("");
  const [autoScroll, setAutoScroll] = useState(true);
  const [isLogging, setIsLogging] = useState(false);
  const [shell, setShell] = useState<"cmd" | "powershell">("cmd");
  const [timeoutSec, setTimeoutSec] = useState<number>(DEFAULT_TIMEOUT_SEC);

  // Execution State & Concurrency Lock
  const [execState, setExecState] = useState<ExecState>("idle");
  const [activeSessionId, setActiveSessionId] = useState<string | null>(null);
  const [elapsedSec, setElapsedSec] = useState<number>(0);
  const [lastExitCode, setLastExitCode] = useState<number | null>(null);

  // Fullscreen and dynamic height state (in-memory only, no localStorage)
  const [isFullScreen, setIsFullScreen] = useState<boolean>(false);
  const [terminalHeight, setTerminalHeight] = useState<number>(420);
  const isDraggingRef = useRef<boolean>(false);
  const startYRef = useRef<number>(0);
  const startHeightRef = useRef<number>(420);

  // Command History Navigation (↑ / ↓)
  const [history, setHistory] = useState<string[]>(() => {
    try {
      const saved = localStorage.getItem(HISTORY_STORAGE_KEY);
      return saved ? JSON.parse(saved) : [];
    } catch {
      return [];
    }
  });
  const [historyIndex, setHistoryIndex] = useState<number>(-1);

  const wsRef = useRef<WebSocket | null>(null);
  const lineSeqRef = useRef<number>(0);
  const consoleBottomRef = useRef<HTMLDivElement | null>(null);
  const inputRef = useRef<InputRef>(null);
  const timerRef = useRef<number | null>(null);
  const activeSessionIdRef = useRef<string | null>(null);
  const lastStatusRef = useRef<string>("");
  const seenSeqSetRef = useRef<Set<number>>(new Set());

  // Keep ref synchronized with activeSessionId
  useEffect(() => {
    activeSessionIdRef.current = activeSessionId;
  }, [activeSessionId]);

  // Exit full-screen on Escape key (capture phase ensures it intercepts before Drawer closes)
  useEffect(() => {
    if (!isFullScreen) return;

    const handleEsc = (e: KeyboardEvent) => {
      if (e.key === "Escape" || e.key === "Esc") {
        e.preventDefault();
        e.stopPropagation();
        setIsFullScreen(false);
      }
    };

    window.addEventListener("keydown", handleEsc, true);
    return () => {
      window.removeEventListener("keydown", handleEsc, true);
    };
  }, [isFullScreen]);

  // Handle vertical resizing of the console terminal in compact mode
  const handleMouseDownResize = useCallback(
    (e: React.MouseEvent) => {
      e.preventDefault();
      isDraggingRef.current = true;
      startYRef.current = e.clientY;
      startHeightRef.current = terminalHeight;
      document.body.style.userSelect = "none";
      document.body.style.cursor = "row-resize";

      const handleMouseMove = (moveEvent: MouseEvent) => {
        if (!isDraggingRef.current) return;
        const delta = moveEvent.clientY - startYRef.current;
        const newHeight = Math.max(160, Math.min(window.innerHeight - 200, startHeightRef.current + delta));
        setTerminalHeight(newHeight);
      };

      const handleMouseUp = () => {
        isDraggingRef.current = false;
        document.body.style.userSelect = "";
        document.body.style.cursor = "";
        window.removeEventListener("mousemove", handleMouseMove);
        window.removeEventListener("mouseup", handleMouseUp);
      };

      window.addEventListener("mousemove", handleMouseMove);
      window.addEventListener("mouseup", handleMouseUp);
    },
    [terminalHeight]
  );

  const appendLine = useCallback(
    (kind: ConsoleLine["kind"], text: string, seq?: number) => {
      lineSeqRef.current += 1;
      const nowStr = new Date().toLocaleTimeString("ru-RU", {
        hour: "2-digit",
        minute: "2-digit",
        second: "2-digit",
      });
      setLines((prev) => [
        ...prev,
        { id: lineSeqRef.current, ts: nowStr, text, kind, seq },
      ]);
    },
    []
  );

  // Return focus to input when execution completes
  const prevExecStateRef = useRef<ExecState>(execState);
  useEffect(() => {
    const wasExecuting =
      prevExecStateRef.current === "queued" ||
      prevExecStateRef.current === "notified" ||
      prevExecStateRef.current === "accepted" ||
      prevExecStateRef.current === "running" ||
      prevExecStateRef.current === "cancelling";

    const isNowDone =
      execState === "idle" ||
      execState === "completed" ||
      execState === "error" ||
      execState === "timeout";

    if (wasExecuting && isNowDone && connected && isWindows) {
      setTimeout(() => {
        inputRef.current?.focus();
      }, 50);
    }
    prevExecStateRef.current = execState;
  }, [execState, connected, isWindows]);

  // Timer effect for active execution
  useEffect(() => {
    if (
      execState !== "idle" &&
      execState !== "completed" &&
      execState !== "error" &&
      execState !== "timeout"
    ) {
      setElapsedSec(0);
      timerRef.current = window.setInterval(() => {
        setElapsedSec((prev) => {
          const next = prev + 1;
          if (next >= timeoutSec + 5) {
            setExecState("timeout");
            appendLine(
              "error",
              `[TIMEOUT] Превышен максимальный таймаут ожидания (${timeoutSec}c).`
            );
          }
          return next;
        });
      }, 1000);
    } else {
      if (timerRef.current) {
        clearInterval(timerRef.current);
        timerRef.current = null;
      }
    }
    return () => {
      if (timerRef.current) {
        clearInterval(timerRef.current);
        timerRef.current = null;
      }
    };
  }, [execState, timeoutSec, appendLine]);

  // Connect WebSocket manually on button click
  const connectWebSocket = useCallback(async () => {
    if (!sn) return;
    if (!hasSupportedSys) {
      message.warning("Консоль недоступна: у устройства отсутствует поддерживаемый тег sys (windows / esp32)");
      return;
    }

    if (wsRef.current) {
      wsRef.current.close();
      wsRef.current = null;
    }

    setConnecting(true);
    lastStatusRef.current = "";
    seenSeqSetRef.current.clear();

    const effectiveDeviceId = deviceId;
    if (!effectiveDeviceId) {
      setConnecting(false);
      appendLine("error", "[ERROR] Не указан идентификатор устройства (deviceId).");
      return;
    }

    appendLine("status", `[STATUS] Запрос монопольной аренды терминала (scope: console)...`);

    const consoleSessionId = user?.session_id || user?.sub || `console-${Date.now()}`;
    let currentLeaseId: string | null = null;
    let effectiveSessionId = consoleSessionId;
    try {
      const lease = await acquireControlLease(effectiveDeviceId, "console", undefined, consoleSessionId);
      currentLeaseId = lease.lease_id;
      if (lease.owner_session_id) {
        effectiveSessionId = lease.owner_session_id;
      }
      setLeaseId(lease.lease_id);
      leaseIdRef.current = lease.lease_id;
      appendLine("status", `[STATUS] Аренда получена (lease_id: ${lease.lease_id}).`);

      if (keepaliveTimerRef.current) {
        clearInterval(keepaliveTimerRef.current);
      }
      keepaliveTimerRef.current = window.setInterval(async () => {
        if (leaseIdRef.current) {
          try {
            await keepaliveControlLease(effectiveDeviceId, leaseIdRef.current);
          } catch (e) {
            console.warn("Console lease keepalive failed", e);
          }
        }
      }, (lease.keepalive_sec || 10) * 1000);
    } catch (err: any) {
      setConnecting(false);
      const detail = err.response?.data?.detail;
      if (err.response?.status === 409) {
        if (detail && typeof detail === "object" && detail.code === "lease_taken") {
          const owner = detail.owner_role
            ? `оператором (${detail.owner_role}, ${detail.owner_masked || detail.owner_user_id || "..."})`
            : "другим пользователем";
          const exp = detail.expires_at ? ` до ${new Date(detail.expires_at).toLocaleTimeString()}` : "";
          appendLine("error", `[ERROR] Терминал занят ${owner}${exp}.`);
        } else if (
          detail &&
          typeof detail === "object" &&
          (detail.code === "session_busy" || detail.message)
        ) {
          appendLine(
            "error",
            `[ERROR] ${detail.message || "Терминал занят другой сессией (видео или консоль)."}`
          );
        } else {
          appendLine(
            "error",
            `[ERROR] Конфликт аренды терминала: ${typeof detail === "string" ? detail : detail?.detail || JSON.stringify(detail)}`
          );
        }
      } else if (err.response?.status === 403) {
        const msg =
          (typeof detail === "object" ? detail.message : detail) ||
          "Доступ к консоли разрешён администраторам и пользователям L4Desk.";
        appendLine("error", `[ERROR] ${msg}`);
      } else {
        appendLine("error", `[ERROR] Ошибка получения аренды: ${err.response?.data?.detail || err.message}`);
      }
      return;
    }

    appendLine("status", `[STATUS] Подключение к сессии диагностики терминала ${sn}...`);

    try {
      const url = getDiagnosticsWsUrl(sn, orgId, currentLeaseId, effectiveSessionId);
      const ws = new WebSocket(url);
      wsRef.current = ws;

      ws.onopen = () => {
        setConnected(true);
        setConnecting(false);
        appendLine("status", "[STATUS] WebSocket соединение с сервисом диагностики установлено.");
        if (isWindows) {
          setTimeout(() => {
            inputRef.current?.focus();
          }, 100);
        }
      };

      ws.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data);

          // 1. Output streaming message with seq deduplication
          if (msg.type === "output" || msg.kind === "stdout" || msg.kind === "stderr") {
            setExecState("running");
            const kind: ConsoleLine["kind"] =
              msg.kind === "stderr" || msg.kind === "error" ? "stderr" : "stdout";
            const text = msg.data !== undefined ? msg.data : msg.text || "";

            const seq = msg.seq;
            if (seq !== undefined && typeof seq === "number") {
              if (seenSeqSetRef.current.has(seq)) {
                // Ignore duplicate chunk
                return;
              }
              seenSeqSetRef.current.add(seq);
            }

            if (text) {
              appendLine(kind, text, msg.seq);
            }

            if (msg.eof) {
              const code = msg.exit_code !== undefined ? msg.exit_code : 0;
              setLastExitCode(code);
              setExecState("completed");
              appendLine(
                "status",
                `[COMPLETED] Процесс завершён. Код возврата: ${code} ${
                  msg.truncated ? "(вывод обрезан по лимиту)" : ""
                }`
              );
              setActiveSessionId(null);
            }
          }
          // 2. State & Status progression with deduplication
          else if (msg.type === "status") {
            const st = msg.status || msg.state;
            if (st === lastStatusRef.current && (st === "started" || st === "running")) {
              return; // Skip duplicate identical status
            }
            lastStatusRef.current = st;

            if (st === "started" || st === "running") {
              setExecState("running");
              appendLine("status", `[STATUS] Выполняется на терминале...`);
            } else if (st === "notified") {
              setExecState("notified");
              appendLine("status", `[STATUS] Уведомление отправлено на терминал...`);
            } else if (st === "accepted") {
              setExecState("accepted");
              appendLine("status", `[STATUS] Терминал принял задачу к исполнению.`);
            } else if (st === "stopped" || st === "closed") {
              setExecState("idle");
              setActiveSessionId(null);
              appendLine("status", `[STATUS] Сессия завершена: ${st}`);
            } else {
              appendLine("status", `[STATUS] ${st}`);
            }
          }
          // 3. Error messages
          else if (msg.type === "error" || msg.error) {
            setExecState("error");
            setActiveSessionId(null);
            appendLine("error", `[ERROR] ${msg.error || JSON.stringify(msg)}`);
          } else {
            appendLine("info", typeof msg === "string" ? msg : JSON.stringify(msg));
          }
        } catch {
          appendLine("stdout", event.data);
        }
      };

      ws.onerror = () => {
        appendLine("error", "[ERROR] Ошибка соединения WebSocket.");
      };

      ws.onclose = (event) => {
        setConnected(false);
        setConnecting(false);
        setIsLogging(false);
        setExecState("idle");
        setActiveSessionId(null);
        if (event.code === 4409) {
          appendLine("error", `[ERROR] Соединение отклонено: конфликт монопольной аренды терминала (код 4409).`);
        } else {
          appendLine("status", `[INFO] Соединение закрыто (код ${event.code}).`);
        }
      };
    } catch (err: any) {
      setConnecting(false);
      appendLine("error", `[ERROR] Не удалось создать WebSocket: ${err.message || err}`);
    }
  }, [sn, orgId, deviceId, hasSupportedSys, isWindows, user, appendLine]);

  // Disconnect WebSocket (with 7002 cancel if active)
  const disconnectWebSocket = useCallback(() => {
    if (keepaliveTimerRef.current) {
      clearInterval(keepaliveTimerRef.current);
      keepaliveTimerRef.current = null;
    }

    if (leaseIdRef.current && deviceId) {
      const lid = leaseIdRef.current;
      leaseIdRef.current = null;
      setLeaseId(null);
      releaseControlLease(deviceId, lid).catch(() => {});
    }

    if (wsRef.current) {
      if (wsRef.current.readyState === WebSocket.OPEN && activeSessionIdRef.current) {
        try {
          wsRef.current.send(
            JSON.stringify({
              type: "cancel",
              session_id: activeSessionIdRef.current,
              reason: "User disconnected session",
              sn,
            })
          );
        } catch {
          // Ignore
        }
      }
      wsRef.current.close();
      wsRef.current = null;
    }
    setConnected(false);
    setConnecting(false);
    setIsLogging(false);
    setExecState("idle");
    setActiveSessionId(null);
    appendLine("info", "[INFO] Сеанс диагностики отключен пользователем.");
  }, [sn, deviceId, appendLine]);

  // Tab switching behavior (keepSession check)
  const prevIsActiveTabRef = useRef<boolean>(Boolean(isActiveTab));
  useEffect(() => {
    if (prevIsActiveTabRef.current && !isActiveTab) {
      // Switched away from console tab
      if (!keepSession) {
        if (keepaliveTimerRef.current) {
          clearInterval(keepaliveTimerRef.current);
          keepaliveTimerRef.current = null;
        }
        if (leaseIdRef.current && deviceId) {
          const lid = leaseIdRef.current;
          leaseIdRef.current = null;
          setLeaseId(null);
          releaseControlLease(deviceId, lid).catch(() => {});
        }
        if (wsRef.current) {
          if (wsRef.current.readyState === WebSocket.OPEN && activeSessionIdRef.current) {
            try {
              wsRef.current.send(
                JSON.stringify({
                  type: "cancel",
                  session_id: activeSessionIdRef.current,
                  reason: "Tab switched away (keepSession disabled)",
                  sn,
                })
              );
            } catch {
              // Ignore
            }
          }
          wsRef.current.close();
          wsRef.current = null;
        }
        setConnected(false);
        setConnecting(false);
        setIsLogging(false);
        setExecState("idle");
        setActiveSessionId(null);
        appendLine("info", "[INFO] Сеанс завершён при смене вкладки (сохранение сессии выключено).");
      }
    }
    prevIsActiveTabRef.current = Boolean(isActiveTab);
  }, [isActiveTab, keepSession, sn, deviceId, appendLine]);

  // Strict unmount cleanup (Drawer close / Page navigation): always cancel 7002 and disconnect
  useEffect(() => {
    return () => {
      if (keepaliveTimerRef.current) {
        clearInterval(keepaliveTimerRef.current);
        keepaliveTimerRef.current = null;
      }
      if (leaseIdRef.current && deviceId) {
        const lid = leaseIdRef.current;
        leaseIdRef.current = null;
        releaseControlLease(deviceId, lid).catch(() => {});
      }
      if (wsRef.current) {
        if (wsRef.current.readyState === WebSocket.OPEN) {
          try {
            wsRef.current.send(
              JSON.stringify({
                type: "cancel",
                session_id: activeSessionIdRef.current || generateUUID(),
                reason: "Console drawer closed / page unmounted",
                sn,
              })
            );
          } catch {
            // Ignore
          }
        }
        wsRef.current.close();
        wsRef.current = null;
      }
    };
  }, [sn, deviceId]);

  // Auto-scroll
  useEffect(() => {
    if (autoScroll && consoleBottomRef.current) {
      consoleBottomRef.current.scrollIntoView({ behavior: "smooth" });
    }
  }, [lines, autoScroll]);

  const sendRawMessage = (msg: Record<string, any>) => {
    if (!wsRef.current || wsRef.current.readyState !== WebSocket.OPEN) {
      message.warning("Нет активного соединения с терминалом. Нажмите «Подключить».");
      return;
    }
    wsRef.current.send(JSON.stringify(msg));
  };

  const handleSendCommand = (cmdToRun?: string) => {
    if (!hasSupportedSys || !isWindows) {
      message.warning("Исполнение CLI команд доступно только для устройств с тегом sys=windows");
      return;
    }

    const cmd = (cmdToRun !== undefined ? cmdToRun : commandInput).trim();
    if (!cmd) return;

    if (
      execState !== "idle" &&
      execState !== "completed" &&
      execState !== "error" &&
      execState !== "timeout"
    ) {
      message.warning("Предыдущая команда ещё выполняется. Дождитесь завершения или прервите её.");
      return;
    }

    const sessionId = generateUUID();
    setActiveSessionId(sessionId);
    setExecState("queued");
    setLastExitCode(null);
    lastStatusRef.current = "";
    seenSeqSetRef.current.clear();

    appendLine("sent", `> [${shell.toUpperCase()}] ${cmd}`);

    // Update command history
    const updatedHistory = [cmd, ...history.filter((h) => h !== cmd)].slice(0, 50);
    setHistory(updatedHistory);
    setHistoryIndex(-1);
    try {
      localStorage.setItem(HISTORY_STORAGE_KEY, JSON.stringify(updatedHistory));
    } catch {
      // Ignore storage errors
    }

    // Protocol 7001 RPC Execute Message
    sendRawMessage({
      type: "exec",
      session_id: sessionId,
      command_id: "raw_cmd",
      command_line: cmd,
      shell,
      ttl_sec: timeoutSec,
      max_output_bytes: 1048576,
      sn,
    });

    if (cmdToRun === undefined) {
      setCommandInput("");
    }
  };

  const handleCancelCommand = () => {
    if (!activeSessionId) return;
    setExecState("cancelling");
    appendLine("status", "[CANCEL] Отправлен сигнал прерывания команды (Kill Process Tree 7002)...");

    // Protocol 7002 RPC Cancel Message
    sendRawMessage({
      type: "cancel",
      session_id: activeSessionId,
      reason: "User cancelled via UI",
      sn,
    });
  };

  const handleKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === "ArrowUp") {
      e.preventDefault();
      if (history.length === 0) return;
      const nextIndex = historyIndex + 1 < history.length ? historyIndex + 1 : historyIndex;
      setHistoryIndex(nextIndex);
      setCommandInput(history[nextIndex]);
    } else if (e.key === "ArrowDown") {
      e.preventDefault();
      if (historyIndex > 0) {
        const nextIndex = historyIndex - 1;
        setHistoryIndex(nextIndex);
        setCommandInput(history[nextIndex]);
      } else if (historyIndex === 0) {
        setHistoryIndex(-1);
        setCommandInput("");
      }
    }
  };

  const handleToggleLog = () => {
    if (isLogging) {
      sendRawMessage({ type: "stop_log", sn });
      setIsLogging(false);
      appendLine("status", "[STATUS] Запрошена остановка потока логов (7000 stop).");
    } else {
      sendRawMessage({ type: "start_log", sn });
      setIsLogging(true);
      appendLine("status", "[STATUS] Запрошен старт потока логов устройства (7000 start).");
    }
  };

  const handleClearConsole = () => {
    setLines([]);
    lineSeqRef.current = 0;
    seenSeqSetRef.current.clear();
  };

  const handleCopyLogs = () => {
    const text = lines.map((l) => `[${l.ts}] ${l.text}`).join("\n");
    navigator.clipboard.writeText(text).then(() => {
      message.success("Лог консоли скопирован в буфер обмена");
    });
  };

  const isExecuting =
    execState === "queued" ||
    execState === "notified" ||
    execState === "accepted" ||
    execState === "running" ||
    execState === "cancelling";

  // Windows-specific Diagnostic Quick Snippets
  const QUICK_SNIPPETS = [
    { label: "ipconfig /all", cmd: "ipconfig /all" },
    { label: "netstat -ano", cmd: "netstat -ano" },
    { label: "tasklist /v", cmd: "tasklist /v" },
    { label: "sc query", cmd: "sc query" },
    { label: "ping 8.8.8.8", cmd: "ping -n 4 8.8.8.8" },
    { label: "systeminfo", cmd: "systeminfo" },
    { label: "dir C:\\", cmd: "dir C:\\" },
    { label: "Disk Free", cmd: "wmic logicaldisk get name,size,freespace" },
  ];

  return (
    <div
      style={
        isFullScreen
          ? {
              position: "fixed",
              top: 0,
              left: 0,
              right: 0,
              bottom: 0,
              width: "100vw",
              height: "100vh",
              zIndex: 1100,
              background: "#f6f7f9",
              padding: "16px 24px",
              boxSizing: "border-box",
              display: "flex",
              flexDirection: "column",
              gap: 10,
              overflow: "hidden",
            }
          : {
              display: "flex",
              flexDirection: "column",
              gap: 10,
            }
      }
    >
      {/* Fullscreen Header Info Bar */}
      {isFullScreen && (
        <div
          style={{
            display: "flex",
            justifyContent: "space-between",
            alignItems: "center",
            paddingBottom: 4,
            borderBottom: "1px solid #e2e8f0",
          }}
        >
          <Space size={8}>
            <CodeOutlined style={{ color: "#2563eb", fontSize: 16 }} />
            <Text strong style={{ fontSize: 14 }}>
              Консоль диагностики устройства #{sn}
            </Text>
            {resolvedSys && <Tag color="blue">{resolvedSys.toUpperCase()}</Tag>}
            <Tag color={connected ? "success" : "default"}>
              {connected ? "Подключено" : "Отключено"}
            </Tag>
          </Space>
          <Space size={8}>
            <Text type="secondary" style={{ fontSize: 12 }}>
              Нажмите <kbd style={{ padding: "1px 5px", background: "#e2e8f0", borderRadius: 3, fontSize: 11 }}>Esc</kbd> для возврата в компактный вид
            </Text>
          </Space>
        </div>
      )}

      {/* Alert if system tag is missing */}
      {!hasSupportedSys && (
        <Alert
          type="warning"
          showIcon
          icon={<WarningOutlined />}
          message="Консольное управление недоступно"
          description={
            <span>
              У устройства отсутствует тег <Text code>sys</Text>. Для включения CLI-управления или чтения логов задайте тег{" "}
              <Text code>sys: windows</Text> или <Text code>sys: esp32</Text> во вкладке «Теги».
            </span>
          }
          style={{ borderRadius: 6 }}
        />
      )}

      {/* Header toolbar */}
      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          alignItems: "center",
          flexWrap: "wrap",
          gap: 8,
          background: "#fafafa",
          padding: "8px 12px",
          borderRadius: 6,
          border: "1px solid #f0f0f0",
        }}
      >
        <Space size={8} wrap>
          <Tag color={connected ? "success" : connecting ? "processing" : "default"}>
            <Space size={4}>
              <WifiOutlined />
              {connected ? "MQTT/WS Онлайн" : connecting ? "Подключение..." : "Отключено"}
            </Space>
          </Tag>

          <Button
            size="small"
            type={connected ? "default" : "primary"}
            icon={connected ? <PoweroffOutlined /> : <ApiOutlined />}
            onClick={connected ? disconnectWebSocket : connectWebSocket}
            loading={connecting}
            disabled={!hasSupportedSys}
          >
            {connected ? "Отключить" : "Подключить"}
          </Button>

          <Tooltip title="Если включено — WebSocket сессия сохраняется при переключении между вкладками Задачи/События/Консоль. Если выключено — сессия завершается (7002) при смене вкладки.">
            <span style={{ fontSize: 12, display: "flex", alignItems: "center", gap: 4, marginLeft: 4 }}>
              Сохранять сессию:
              <Switch size="small" checked={keepSession} onChange={setKeepSession} disabled={!hasSupportedSys} />
            </span>
          </Tooltip>

          {isEsp32 && (
            <Button
              size="small"
              type="primary"
              icon={isLogging ? <StopOutlined /> : <PlayCircleOutlined />}
              onClick={handleToggleLog}
              disabled={!connected}
            >
              {isLogging ? "Остановить логи" : "Live Logs (ESP32)"}
            </Button>
          )}

          {isWindows && (
            <>
              <Button
                size="small"
                icon={isLogging ? <StopOutlined /> : <PlayCircleOutlined />}
                onClick={handleToggleLog}
                disabled={!connected}
              >
                {isLogging ? "Остановить логи" : "Live Logs"}
              </Button>

              <Space size={4} style={{ marginLeft: 6 }}>
                <Text type="secondary" style={{ fontSize: 12 }}>
                  Оболочка:
                </Text>
                <Select
                  size="small"
                  value={shell}
                  onChange={setShell}
                  style={{ width: 115 }}
                  options={[
                    { label: "cmd.exe", value: "cmd" },
                    { label: "PowerShell", value: "powershell" },
                  ]}
                  disabled={isExecuting || !connected}
                />
              </Space>

              <Space size={4}>
                <Text type="secondary" style={{ fontSize: 12 }}>
                  Таймаут:
                </Text>
                <Select
                  size="small"
                  value={timeoutSec}
                  onChange={setTimeoutSec}
                  style={{ width: 75 }}
                  options={[
                    { label: "15c", value: 15 },
                    { label: "30c", value: 30 },
                    { label: "60c", value: 60 },
                    { label: "120c", value: 120 },
                  ]}
                  disabled={isExecuting || !connected}
                />
              </Space>
            </>
          )}
        </Space>

        <Space size={12}>
          <Segmented
            size="small"
            value={isFullScreen ? "fullscreen" : "compact"}
            onChange={(val) => setIsFullScreen(val === "fullscreen")}
            options={[
              {
                label: "Компактный",
                value: "compact",
                icon: <FullscreenExitOutlined />,
              },
              {
                label: "Полный экран",
                value: "fullscreen",
                icon: <FullscreenOutlined />,
              },
            ]}
          />
          <Tooltip title="Автоматическая прокрутка вниз при новом выводе">
            <span style={{ fontSize: 12, display: "flex", alignItems: "center", gap: 4 }}>
              Автоскролл:
              <Switch size="small" checked={autoScroll} onChange={setAutoScroll} />
            </span>
          </Tooltip>
          <Button size="small" icon={<CopyOutlined />} onClick={handleCopyLogs} disabled={lines.length === 0}>
            Копировать
          </Button>
          <Button size="small" icon={<ClearOutlined />} onClick={handleClearConsole} disabled={lines.length === 0}>
            Очистить
          </Button>
        </Space>
      </div>

      {/* Quick Snippets Bar (Windows only) */}
      {isWindows && (
        <div style={{ display: "flex", gap: 6, flexWrap: "wrap", alignItems: "center" }}>
          <Text type="secondary" style={{ fontSize: 12, display: "flex", alignItems: "center", gap: 4 }}>
            <WindowsOutlined /> Быстрые команды:
          </Text>
          {QUICK_SNIPPETS.map((btn) => (
            <Button
              key={btn.label}
              size="small"
              disabled={!connected || isExecuting}
              onClick={() => {
                handleSendCommand(btn.cmd);
                setTimeout(() => inputRef.current?.focus(), 50);
              }}
              style={{ fontSize: 11 }}
            >
              {btn.label}
            </Button>
          ))}
        </div>
      )}

      {/* Terminal Output Area */}
      <div
        style={{
          background: "#141414",
          color: "#d4d4d4",
          fontFamily: "'JetBrains Mono', Consolas, 'Courier New', monospace",
          fontSize: 12,
          padding: 12,
          borderRadius: 6,
          height: isFullScreen ? "auto" : terminalHeight,
          flex: isFullScreen ? 1 : undefined,
          minHeight: 160,
          overflowY: "auto",
          display: "flex",
          flexDirection: "column",
          border: "1px solid #303030",
          boxShadow: "inset 0 2px 6px rgba(0,0,0,0.4)",
        }}
      >
        {lines.length === 0 ? (
          <div style={{ color: "#666", textAlign: "center", margin: "auto" }}>
            {!hasSupportedSys
              ? "Консоль отключена. Задайте тег sys=windows или sys=esp32 во вкладке «Теги»."
              : !connected
              ? "Сессия не активна. Нажмите «Подключить» для начала работы с консолью."
              : isEsp32
              ? "ESP32 подключен. Нажмите «Live Logs (ESP32)» для запуска потока логов."
              : "Терминал диагностики готов. Введите команду в поле ниже и нажмите Enter."}
          </div>
        ) : (
          lines.map((line) => {
            let color = "#d4d4d4";
            if (line.kind === "sent") color = "#4fc1ff";
            else if (line.kind === "stderr" || line.kind === "error") color = "#f48771";
            else if (line.kind === "status") color = "#73d13d";
            else if (line.kind === "info") color = "#ffe58f";

            return (
              <div
                key={line.id}
                style={{
                  lineHeight: "1.45",
                  wordBreak: "break-all",
                  whiteSpace: "pre-wrap",
                  color,
                }}
              >
                <span style={{ color: "#555", marginRight: 8, userSelect: "none" }}>
                  [{line.ts}]
                </span>
                {line.seq !== undefined && (
                  <span style={{ color: "#444", marginRight: 6, userSelect: "none", fontSize: 10 }}>
                    #{line.seq}
                  </span>
                )}
                {line.text}
              </div>
            );
          })
        )}
        <div ref={consoleBottomRef} />
      </div>

      {/* Height Resizer Handle (Compact mode only) */}
      {!isFullScreen && (
        <div
          onMouseDown={handleMouseDownResize}
          style={{
            height: 8,
            cursor: "row-resize",
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            margin: "-6px 0 -2px 0",
            zIndex: 2,
            userSelect: "none",
          }}
          title="Потяните для изменения высоты консоли"
        >
          <div
            style={{
              width: 38,
              height: 4,
              borderRadius: 2,
              backgroundColor: "#d1d5db",
            }}
          />
        </div>
      )}

      {/* Status progression and Active Execution Bar */}
      {isExecuting && (
        <div
          style={{
            background: "#fffbe6",
            border: "1px solid #ffe58f",
            padding: "6px 12px",
            borderRadius: 6,
            display: "flex",
            justifyContent: "space-between",
            alignItems: "center",
          }}
        >
          <Space size={8}>
            <LoadingOutlined spin style={{ color: "#faad14" }} />
            <Text style={{ fontSize: 12, fontWeight: 500 }}>
              {execState === "queued" && "Постановка в очередь сервера..."}
              {execState === "notified" && "Отправка уведомления на терминал (MQTT RPC)..."}
              {execState === "accepted" && "Терминал принял задачу, запуск процесса..."}
              {execState === "running" && "Процесс выполняется на терминале (получение вывода)..."}
              {execState === "cancelling" && "Запрошено принудительное прерывание процесса (7002)..."}
            </Text>
            <Tag color="processing">{elapsedSec}с / {timeoutSec}с</Tag>
          </Space>
          <Progress
            percent={Math.min(100, Math.round((elapsedSec / timeoutSec) * 100))}
            size="small"
            status={execState === "cancelling" ? "exception" : "active"}
            style={{ width: 140, margin: 0 }}
          />
        </div>
      )}

      {/* Command Input Bar with Strict Locking */}
      {isWindows && (
        <div style={{ display: "flex", gap: 8, alignItems: "center" }}>
          <Input
            ref={inputRef}
            prefix={<CodeOutlined style={{ color: "#888", marginRight: 4 }} />}
            placeholder={
              !connected
                ? "Нажмите «Подключить» для ввода команд..."
                : isExecuting
                ? "Выполняется команда... Ожидайте завершения или нажмите «Прервать»"
                : `Введите команду для ${shell.toUpperCase()} (напр. ipconfig /all, tasklist, sc query)...`
            }
            value={commandInput}
            onChange={(e) => setCommandInput(e.target.value)}
            onPressEnter={() => handleSendCommand()}
            onKeyDown={handleKeyDown}
            disabled={!connected || isExecuting}
            style={{ fontFamily: "'JetBrains Mono', Consolas, monospace", fontSize: 13 }}
          />

          {isExecuting ? (
            <Button
              danger
              type="primary"
              icon={<CloseCircleOutlined />}
              onClick={handleCancelCommand}
              loading={execState === "cancelling"}
            >
              Прервать ({elapsedSec}с)
            </Button>
          ) : (
            <Button
              type="primary"
              icon={<SendOutlined />}
              onClick={() => {
                handleSendCommand();
                setTimeout(() => inputRef.current?.focus(), 50);
              }}
              disabled={!connected || !commandInput.trim()}
            >
              Выполнить
            </Button>
          )}
        </div>
      )}

      {/* Footer Info Bar */}
      <div style={{ display: "flex", justifyContent: "space-between", fontSize: 11, color: "#888" }}>
        <span>
          Терминал: <Text code>{sn}</Text> | Платформа:{" "}
          <Text code>{resolvedSys ? `sys=${resolvedSys}` : "тег отсутствует"}</Text> | Роль:{" "}
          <Text code>extra_service</Text> (l4con)
        </span>
        <span>
          {lastExitCode !== null && (
            <Badge
              status={lastExitCode === 0 ? "success" : "error"}
              text={`Exit Code: ${lastExitCode}`}
              style={{ fontSize: 11 }}
            />
          )}
          {isWindows && <span style={{ marginLeft: 12 }}>История: клавиши ↑ / ↓</span>}
          {isFullScreen && <span style={{ marginLeft: 12 }}>Выход: Esc</span>}
        </span>
      </div>
    </div>
  );
}
