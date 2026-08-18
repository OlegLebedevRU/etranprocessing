import { useEffect, useState, useCallback } from "react";
import { Button, Card, message, Table, Tooltip, Typography } from "antd";
import { ReloadOutlined } from "@ant-design/icons";
import { getMonitoring, MonitoringTerminal } from "../api/monitoring";
import PageHeader from "../components/PageHeader";
import { formatDate } from "../utils/billing";

const { Text } = Typography;

/** Minutes after which the "last payment" cell turns yellow / red. */
const PAYMENT_WARN_MINUTES = 60;
const PAYMENT_ALERT_MINUTES = 240;
/** Days before license expiry that count as "expiring soon". */
const LICENSE_WARN_DAYS = 30;

const COLOR = {
  warnBg: "#fffbe6",
  alertBg: "#fff1f0",
  warn: "#d97706",
  alert: "#dc2626",
  muted: "#94a3b8",
};

function SlotBar({ slots }: { slots: boolean[] }) {
  return (
    <div style={{ display: "flex", gap: 1, alignItems: "flex-end", height: 14 }}>
      {slots.map((active, i) => (
        <div
          key={i}
          style={{
            width: 3,
            height: active ? 14 : 10,
            borderRadius: 1,
            background: active ? "#16a34a" : "#dc2626",
          }}
        />
      ))}
    </div>
  );
}

function stateBg(val: string): string | undefined {
  const n = parseInt(val);
  if (isNaN(n)) return undefined;
  if (n >= 200) return COLOR.alertBg;
  if (n >= 100) return COLOR.warnBg;
  return undefined;
}

function lastnumBg(val: number): string | undefined {
  if (val >= 6) return COLOR.alertBg;
  if (val >= 3) return COLOR.warnBg;
  return undefined;
}

function lastnumTooltip(val: number): string {
  const totalSec = 600 * val;
  const h = Math.floor(totalSec / 3600);
  const m = Math.floor((totalSec % 3600) / 60);
  if (h >= 24) {
    const d = Math.floor(h / 24);
    return `${d}д ${h % 24}ч ${m}м`;
  }
  return `${h}ч ${m}м`;
}

/** Minutes elapsed since an ISO timestamp, or null when there is none. */
function minutesSince(iso: string | null): number | null {
  if (!iso) return null;
  const diff = Date.now() - new Date(iso).getTime();
  if (!Number.isFinite(diff)) return null;
  return Math.max(0, Math.floor(diff / 60000));
}

function formatElapsed(minutes: number): string {
  if (minutes < 60) return `${minutes}м`;
  const h = Math.floor(minutes / 60);
  if (h < 24) return `${h}ч ${minutes % 60}м`;
  const d = Math.floor(h / 24);
  return `${d}д ${h % 24}ч`;
}

function daysUntil(iso: string): number {
  return Math.floor((new Date(iso).getTime() - Date.now()) / 86400000);
}

export default function MonitoringPage() {
  const [terminals, setTerminals] = useState<MonitoringTerminal[]>([]);
  const [loading, setLoading] = useState(true);
  const [lastUpdate, setLastUpdate] = useState("");

  const load = useCallback(async () => {
    setLoading(true);
    try {
      const res = await getMonitoring();
      setTerminals(res.data.items);
      setLastUpdate(new Date().toLocaleTimeString("ru-RU"));
    } catch {
      message.error("Ошибка загрузки");
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    load();
  }, [load]);
  useEffect(() => {
    const timer = setInterval(load, 60000);
    return () => clearInterval(timer);
  }, [load]);

  const columns = [
    {
      title: "ID",
      dataIndex: "device_id",
      key: "device_id",
      render: (v: number) => (
        <Text strong style={{ fontSize: 12 }}>
          {String(v).padStart(8, "\u00A0")}
        </Text>
      ),
    },
    {
      title: "Связь",
      key: "slots",
      render: (_: unknown, record: MonitoringTerminal) => (
        <SlotBar slots={record.slots} />
      ),
    },
    {
      title: "Обмен",
      dataIndex: "lastnumconn",
      key: "lastnumconn",
      align: "center" as const,
      render: (v: number) => (
        <Tooltip title={lastnumTooltip(v)}>
          <span
            style={{
              fontSize: 12,
              background: lastnumBg(v),
              padding: "1px 5px",
              borderRadius: 4,
            }}
          >
            {v}
          </span>
        </Tooltip>
      ),
    },
    {
      title: "Платёж",
      dataIndex: "last_payment_at",
      key: "last_payment",
      align: "center" as const,
      render: (v: string | null) => {
        const minutes = minutesSince(v);
        if (minutes === null) {
          return (
            <Tooltip title="Платежей не было">
              <Text style={{ fontSize: 12, color: COLOR.muted }}>—</Text>
            </Tooltip>
          );
        }
        const alert = minutes > PAYMENT_ALERT_MINUTES;
        const warn = !alert && minutes > PAYMENT_WARN_MINUTES;
        return (
          <Tooltip title={new Date(v as string).toLocaleString("ru-RU")}>
            <span
              style={{
                fontSize: 12,
                background: alert
                  ? COLOR.alertBg
                  : warn
                    ? COLOR.warnBg
                    : undefined,
                color: alert ? COLOR.alert : warn ? COLOR.warn : undefined,
                padding: "1px 5px",
                borderRadius: 4,
                whiteSpace: "nowrap",
              }}
            >
              {formatElapsed(minutes)}
            </span>
          </Tooltip>
        );
      },
    },
    {
      title: "Лицензия",
      dataIndex: "license_expires_at",
      key: "license",
      align: "center" as const,
      render: (v: string | null) => {
        if (!v)
          return (
            <Text style={{ fontSize: 12, color: COLOR.muted }}>нет</Text>
          );
        const days = daysUntil(v);
        return (
          <Tooltip
            title={
              days < 0
                ? `Истекла ${Math.abs(days)} дн. назад`
                : `Осталось ${days} дн.`
            }
          >
            <Text
              style={{ fontSize: 12, whiteSpace: "nowrap" }}
              type={
                days < 0
                  ? "danger"
                  : days <= LICENSE_WARN_DAYS
                    ? "warning"
                    : undefined
              }
            >
              {formatDate(v)}
            </Text>
          </Tooltip>
        );
      },
    },
    {
      title: "Сертификат",
      key: "cert",
      align: "center" as const,
      render: (_: unknown, r: MonitoringTerminal) => {
        if (!r.cert_serial)
          return (
            <Text type="warning" style={{ fontSize: 12 }}>
              не выпущен
            </Text>
          );
        if (!r.cert_not_valid_after)
          return (
            <Text style={{ fontSize: 12, color: COLOR.muted }}>выпущен</Text>
          );
        const days = daysUntil(r.cert_not_valid_after);
        return (
          <Tooltip title={`Серийный номер: ${r.cert_serial}`}>
            <Text
              style={{ fontSize: 12, whiteSpace: "nowrap" }}
              type={
                days < 0 ? "danger" : days <= LICENSE_WARN_DAYS ? "warning" : undefined
              }
            >
              {formatDate(r.cert_not_valid_after)}
            </Text>
          </Tooltip>
        );
      },
    },
    {
      title: "Валид.",
      dataIndex: "validator_state",
      key: "validator_state",
      align: "center" as const,
      render: (v: string) => {
        const n = parseInt(v);
        return (
          <span
            style={{
              fontSize: 11,
              background: stateBg(v),
              padding: "1px 4px",
              borderRadius: 4,
            }}
          >
            {isNaN(n) ? "—" : n}
          </span>
        );
      },
    },
    {
      title: "Принт.",
      dataIndex: "printer_state",
      key: "printer_state",
      align: "center" as const,
      render: (v: string) => {
        const n = parseInt(v);
        return (
          <span
            style={{
              fontSize: 11,
              background: stateBg(v),
              padding: "1px 4px",
              borderRadius: 4,
            }}
          >
            {isNaN(n) ? "—" : n}
          </span>
        );
      },
    },
    {
      title: "ПО",
      dataIndex: "soft_version",
      key: "soft_version",
      render: (v: string) => <Text style={{ fontSize: 11 }}>{v}</Text>,
    },
    {
      title: "",
      dataIndex: "is_active",
      key: "active",
      align: "center" as const,
      render: (v: boolean) => (
        <span style={{ color: v ? "#16a34a" : "#dc2626", fontSize: 14 }}>●</span>
      ),
    },
    {
      title: "SN",
      dataIndex: "sn",
      key: "sn",
      render: (v: string) => (
        <Tooltip
          title={
            <Typography.Text copyable style={{ fontSize: 11 }}>
              {v}
            </Typography.Text>
          }
        >
          <Text
            code
            style={{ fontSize: 10, cursor: "default", whiteSpace: "nowrap" }}
          >
            {v.length > 10 ? v.slice(0, 10) + "…" : v}
          </Text>
        </Tooltip>
      ),
    },
  ];

  return (
    <>
      <PageHeader
        title="Мониторинг"
        subtitle={lastUpdate ? `Обновлено в ${lastUpdate}` : undefined}
        extra={
          <Button
            size="small"
            icon={<ReloadOutlined />}
            onClick={load}
            loading={loading}
          >
            Обновить
          </Button>
        }
      />

      <Card styles={{ body: { padding: 0 } }} style={{ width: "fit-content" }}>
        <Table
          dataSource={terminals}
          columns={columns}
          rowKey="terminal_id"
          loading={loading}
          size="small"
          tableLayout="auto"
          pagination={false}
        />
      </Card>
    </>
  );
}
