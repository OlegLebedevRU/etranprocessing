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
} from "@ant-design/icons";
import { getDiagnosticsWsUrl } from "../../api/devices";

const { Text } = Typography;

interface DeviceConsoleTabProps {
  sn: string;
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
  sys,
  tags,
  orgId,
  isActiveTab = true,
}: DeviceConsoleTabProps) {
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
  const connectWebSocket = useCallback(() => {
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
    appendLine("status", `[STATUS] Подключение к сессии диагностики терминала ${sn}...`);

    try {
      const url = getDiagnosticsWsUrl(sn, orgId);
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
        appendLine("status", `[INFO] Соединение закрыто (код ${event.code}).`);
      };
    } catch (err: any) {
      setConnecting(false);
      appendLine("error", `[ERROR] Не удалось создать WebSocket: ${err.message || err}`);
    }
  }, [sn, orgId, hasSupportedSys, isWindows, appendLine]);

  // Disconnect WebSocket (with 7002 cancel if active)
  const disconnectWebSocket = useCallback(() => {
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
  }, [sn, appendLine]);

  // Tab switching behavior (keepSession check)
  const prevIsActiveTabRef = useRef<boolean>(Boolean(isActiveTab));
  useEffect(() => {
    if (prevIsActiveTabRef.current && !isActiveTab) {
      // Switched away from console tab
      if (!keepSession) {
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
  }, [isActiveTab, keepSession, sn, appendLine]);

  // Strict unmount cleanup (Drawer close / Page navigation): always cancel 7002 and disconnect
  useEffect(() => {
    return () => {
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
  }, [sn]);

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
    <div style={{ display: "flex", flexDirection: "column", gap: 10 }}>
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
          height: 420,
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
        </span>
      </div>
    </div>
  );
}
