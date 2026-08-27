import { useState, useEffect, useRef, useCallback } from "react";
import { Button, Input, Space, Tag, Typography, message, Switch, Tooltip } from "antd";
import {
  PoweroffOutlined,
  ClearOutlined,
  SendOutlined,
  CopyOutlined,
  PlayCircleOutlined,
  StopOutlined,
  WifiOutlined,
} from "@ant-design/icons";
import { getDiagnosticsWsUrl } from "../../api/devices";

const { Text } = Typography;

interface DeviceConsoleTabProps {
  sn: string;
  app?: string;
  orgId: number;
}

interface ConsoleLine {
  id: number;
  ts: string;
  text: string;
  kind: "stdout" | "stderr" | "status" | "info" | "sent";
}

export default function DeviceConsoleTab({ sn, orgId }: DeviceConsoleTabProps) {
  const [connected, setConnected] = useState(false);
  const [connecting, setConnecting] = useState(false);
  const [lines, setLines] = useState<ConsoleLine[]>([]);
  const [commandInput, setCommandInput] = useState("");
  const [autoScroll, setAutoScroll] = useState(true);
  const [isLogging, setIsLogging] = useState(false);

  const wsRef = useRef<WebSocket | null>(null);
  const lineSeqRef = useRef<number>(0);
  const consoleBottomRef = useRef<HTMLDivElement | null>(null);

  const appendLine = useCallback((kind: ConsoleLine["kind"], text: string) => {
    lineSeqRef.current += 1;
    const nowStr = new Date().toLocaleTimeString("ru-RU", {
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
    });
    setLines((prev) => [
      ...prev,
      { id: lineSeqRef.current, ts: nowStr, text, kind },
    ]);
  }, []);

  const connectWebSocket = useCallback(() => {
    if (!sn) return;
    if (wsRef.current) {
      wsRef.current.close();
      wsRef.current = null;
    }

    setConnecting(true);
    appendLine("status", `Подключение к сессии диагностики устройства ${sn}...`);

    try {
      const url = getDiagnosticsWsUrl(sn, orgId);
      const ws = new WebSocket(url);
      wsRef.current = ws;

      ws.onopen = () => {
        setConnected(true);
        setConnecting(false);
        appendLine("status", "WebSocket соединение успешно установлено");
      };

      ws.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          if (data.type === "stdout") {
            appendLine("stdout", data.text || data.data || JSON.stringify(data));
          } else if (data.type === "stderr" || data.error) {
            appendLine("stderr", data.error || data.text || JSON.stringify(data));
          } else if (data.type === "status") {
            appendLine("status", `Статус: ${data.status}`);
          } else {
            appendLine("info", typeof data === "string" ? data : JSON.stringify(data));
          }
        } catch {
          appendLine("stdout", event.data);
        }
      };

      ws.onerror = () => {
        appendLine("stderr", "Ошибка соединения WebSocket");
      };

      ws.onclose = (event) => {
        setConnected(false);
        setConnecting(false);
        setIsLogging(false);
        appendLine("status", `Соединение закрыто (код ${event.code})`);
      };
    } catch (err: any) {
      setConnecting(false);
      appendLine("stderr", `Не удалось создать WebSocket: ${err.message || err}`);
    }
  }, [sn, orgId, appendLine]);

  const disconnectWebSocket = () => {
    if (wsRef.current) {
      wsRef.current.close();
      wsRef.current = null;
    }
    setConnected(false);
    setConnecting(false);
    setIsLogging(false);
  };

  useEffect(() => {
    connectWebSocket();
    return () => {
      if (wsRef.current) {
        wsRef.current.close();
        wsRef.current = null;
      }
    };
  }, [connectWebSocket]);

  useEffect(() => {
    if (autoScroll && consoleBottomRef.current) {
      consoleBottomRef.current.scrollIntoView({ behavior: "smooth" });
    }
  }, [lines, autoScroll]);

  const sendRawMessage = (msg: Record<string, any>) => {
    if (!wsRef.current || wsRef.current.readyState !== WebSocket.OPEN) {
      message.warning("Нет активного соединения с устройством");
      return;
    }
    wsRef.current.send(JSON.stringify(msg));
  };

  const handleSendCommand = () => {
    const cmd = commandInput.trim();
    if (!cmd) return;
    appendLine("sent", `> ${cmd}`);
    sendRawMessage({
      type: "exec",
      command: cmd,
      sn,
    });
    setCommandInput("");
  };

  const handleToggleLog = () => {
    if (isLogging) {
      sendRawMessage({ type: "stop_log", sn });
      setIsLogging(false);
      appendLine("status", "Запрошена остановка потока логов");
    } else {
      sendRawMessage({ type: "start_log", sn });
      setIsLogging(true);
      appendLine("status", "Запрошен старт потока логов устройства");
    }
  };

  const handleClearConsole = () => {
    setLines([]);
  };

  const handleCopyLogs = () => {
    const text = lines.map((l) => `[${l.ts}] ${l.text}`).join("\n");
    navigator.clipboard.writeText(text).then(() => {
      message.success("Лог консоли скопирован в буфер обмена");
    });
  };

  return (
    <div>
      {/* Header toolbar */}
      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          alignItems: "center",
          marginBottom: 12,
          flexWrap: "wrap",
          gap: 8,
        }}
      >
        <Space size={8} wrap>
          <Tag color={connected ? "success" : connecting ? "processing" : "default"}>
            <Space size={4}>
              <WifiOutlined />
              {connected ? "Подключено" : connecting ? "Подключение..." : "Отключено"}
            </Space>
          </Tag>
          <Button
            size="small"
            type={connected ? "default" : "primary"}
            icon={<PoweroffOutlined />}
            onClick={connected ? disconnectWebSocket : connectWebSocket}
            loading={connecting}
          >
            {connected ? "Отключить" : "Подключить"}
          </Button>
          <Button
            size="small"
            icon={isLogging ? <StopOutlined /> : <PlayCircleOutlined />}
            onClick={handleToggleLog}
            disabled={!connected}
          >
            {isLogging ? "Остановить логи" : "Стриминг логов"}
          </Button>
        </Space>

        <Space size={12}>
          <Tooltip title="Автоматическая прокрутка к последнему сообщению">
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

      {/* Preset Command Quick Buttons */}
      <div style={{ marginBottom: 12, display: "flex", gap: 6, flexWrap: "wrap" }}>
        <Text type="secondary" style={{ fontSize: 12, alignSelf: "center", marginRight: 4 }}>
          Быстрые команды:
        </Text>
        {[
          { label: "Ping", cmd: "ping" },
          { label: "SysInfo", cmd: "sysinfo" },
          { label: "Uptime", cmd: "uptime" },
          { label: "Memory (free -m)", cmd: "free -m" },
          { label: "Disk (df -h)", cmd: "df -h" },
          { label: "Processes (top)", cmd: "top -n 1" },
        ].map((btn) => (
          <Button
            key={btn.label}
            size="small"
            disabled={!connected}
            onClick={() => {
              appendLine("sent", `> ${btn.cmd}`);
              sendRawMessage({ type: "exec", command: btn.cmd, sn });
            }}
          >
            {btn.label}
          </Button>
        ))}
      </div>

      {/* Terminal Output Area */}
      <div
        style={{
          background: "#1e1e1e",
          color: "#d4d4d4",
          fontFamily: "Consolas, 'Courier New', monospace",
          fontSize: 12,
          padding: 12,
          borderRadius: 6,
          height: 380,
          overflowY: "auto",
          display: "flex",
          flexDirection: "column",
          border: "1px solid #333",
        }}
      >
        {lines.length === 0 ? (
          <div style={{ color: "#666", textAlign: "center", margin: "auto" }}>
            Терминал пуст. Подключитесь и отправьте команду для начала диагностики.
          </div>
        ) : (
          lines.map((line) => {
            let color = "#d4d4d4";
            if (line.kind === "sent") color = "#4fc1ff";
            else if (line.kind === "stderr") color = "#f48771";
            else if (line.kind === "status") color = "#6a9955";
            else if (line.kind === "info") color = "#dcdcaa";

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
                {line.text}
              </div>
            );
          })
        )}
        <div ref={consoleBottomRef} />
      </div>

      {/* Command Input Bar */}
      <div style={{ display: "flex", gap: 8, marginTop: 10 }}>
        <Input
          placeholder="Введите команду для отправки на устройство..."
          value={commandInput}
          onChange={(e) => setCommandInput(e.target.value)}
          onPressEnter={handleSendCommand}
          disabled={!connected}
          style={{ fontFamily: "monospace" }}
        />
        <Button
          type="primary"
          icon={<SendOutlined />}
          onClick={handleSendCommand}
          disabled={!connected || !commandInput.trim()}
        >
          Отправить
        </Button>
      </div>
    </div>
  );
}
