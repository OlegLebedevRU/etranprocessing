import { useEffect, useState, useCallback, useRef } from "react";
import { Button, Card, Input, message, Space, Table, Tooltip, Typography } from "antd";
import { CloudOutlined, ReloadOutlined, SearchOutlined } from "@ant-design/icons";
import { getMonitoring, MonitoringTerminal } from "../api/monitoring";
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
  const [total, setTotal] = useState(0);
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(50);
  const [search, setSearch] = useState("");
  const [searchInput, setSearchInput] = useState("");
  const [loading, setLoading] = useState(true);
  const [lastUpdate, setLastUpdate] = useState("");
  const [simulatedRefreshing, setSimulatedRefreshing] = useState(false);
  const lastManualFetchTimeRef = useRef<number>(0);

  const load = useCallback(async (isSilent = false) => {
    if (!isSilent) {
      setLoading(true);
    }
    try {
      const res = await getMonitoring(page, pageSize, search);
      setTerminals(res.data.items);
      setTotal(res.data.total ?? res.data.items.length);
      setLastUpdate(new Date().toLocaleTimeString("ru-RU"));
    } catch {
      if (!isSilent) {
        message.error("Ошибка загрузки");
      }
    } finally {
      if (!isSilent) {
        setLoading(false);
      }
    }
  }, [page, pageSize, search]);

  useEffect(() => {
    load(false);
  }, [load]);

  // Auto-refresh: 1 time per minute (60,000 ms)
  useEffect(() => {
    const timer = setInterval(() => {
      load(true);
    }, 60000);
    return () => clearInterval(timer);
  }, [load]);

  const handleManualRefresh = () => {
    const now = Date.now();
    const elapsed = now - lastManualFetchTimeRef.current;
    if (elapsed < 5000) {
      // Cooldown protection: simulate refresh feedback without backend call
      setSimulatedRefreshing(true);
      setTimeout(() => {
        setSimulatedRefreshing(false);
        setLastUpdate(new Date().toLocaleTimeString("ru-RU"));
      }, 300);
      return;
    }
    lastManualFetchTimeRef.current = now;
    load(false);
  };

  const handleSearch = () => {
    setPage(1);
    setSearch(searchInput.trim());
  };

  const columns = [
    {
      title: "Терминал",
      dataIndex: "device_id",
      key: "device_id",
      render: (v: number, record: MonitoringTerminal) => (
        <span style={{ display: "inline-flex", alignItems: "center", gap: 4 }}>
          <Text strong style={{ fontSize: 12 }}>
            {String(v).padStart(8, "\u00A0")}
          </Text>
          {record.iot_provisioned && (
            <Tooltip
              title={
                record.iot_provisioned_at
                  ? `Зарегистрирован в Leo4 IoT (${new Date(record.iot_provisioned_at).toLocaleDateString("ru-RU")})`
                  : "Зарегистрирован в Leo4 IoT"
              }
            >
              <CloudOutlined
                style={{
                  fontSize: 12,
                  color: "#94a3b8",
                  cursor: "default",
                }}
              />
            </Tooltip>
          )}
        </span>
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
      title: "Платеж",
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
      title: "Инкассация",
      dataIndex: "last_inkass_at",
      key: "last_inkass",
      align: "center" as const,
      render: (v: string | null) => {
        if (!v) {
          return (
            <Tooltip title="Инкассаций не было">
              <Text style={{ fontSize: 12, color: COLOR.muted }}>—</Text>
            </Tooltip>
          );
        }
        const d = new Date(v);
        const dateStr = d.toLocaleDateString("ru-RU");
        const timeStr = d.toLocaleTimeString("ru-RU", {
          hour: "2-digit",
          minute: "2-digit",
        });
        return (
          <Tooltip title={d.toLocaleString("ru-RU")}>
            <Text style={{ fontSize: 12, whiteSpace: "nowrap" }}>
              {dateStr} {timeStr}
            </Text>
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
            <span
              style={{
                fontSize: 12,
                background: COLOR.alertBg,
                color: COLOR.alert,
                padding: "1px 5px",
                borderRadius: 4,
                fontWeight: 500,
              }}
            >
              нет
            </span>
          );
        const days = daysUntil(v);
        const isExpired = days < 0;
        const isWarn = days <= LICENSE_WARN_DAYS;
        return (
          <Tooltip
            title={
              isExpired
                ? `Истекла ${Math.abs(days)} дн. назад`
                : `Осталось ${days} дн.`
            }
          >
            <span
              style={{
                fontSize: 12,
                whiteSpace: "nowrap",
                background: isExpired ? COLOR.alertBg : isWarn ? COLOR.warnBg : undefined,
                color: isExpired ? COLOR.alert : isWarn ? COLOR.warn : undefined,
                padding: isExpired || isWarn ? "1px 5px" : undefined,
                borderRadius: 4,
                fontWeight: isExpired ? 500 : undefined,
              }}
            >
              {formatDate(v)}
            </span>
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
        const isExpired = days < 0;
        const isWarn = days <= LICENSE_WARN_DAYS;
        return (
          <Tooltip title={`Серийный номер: ${r.cert_serial}`}>
            <span
              style={{
                fontSize: 12,
                whiteSpace: "nowrap",
                background: isExpired ? COLOR.alertBg : isWarn ? COLOR.warnBg : undefined,
                color: isExpired ? COLOR.alert : isWarn ? COLOR.warn : undefined,
                padding: isExpired || isWarn ? "1px 5px" : undefined,
              }}
            >
              {formatDate(r.cert_not_valid_after)}
            </span>
          </Tooltip>
        );
      },
    },
    {
      title: "Купюрник",
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
      title: "Принтер",
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
      title: "Адрес",
      dataIndex: "address",
      key: "address",
      render: (v: string | null) =>
        v ? (
          <Tooltip title={v}>
            <Text style={{ fontSize: 12, maxWidth: 180, display: "inline-block" }} ellipsis>
              {v}
            </Text>
          </Tooltip>
        ) : (
          <Text style={{ fontSize: 12, color: COLOR.muted }}>—</Text>
        ),
    },
    {
      title: "Тип",
      dataIndex: "terminal_type_name",
      key: "terminal_type",
      render: (v: string | null, record: MonitoringTerminal) => {
        if (record.terminal_type_id !== undefined && record.terminal_type_id !== null) {
          return (
            <Text style={{ fontSize: 12, whiteSpace: "nowrap" }}>
              {v ? `${record.terminal_type_id}: ${v}` : `${record.terminal_type_id}`}
            </Text>
          );
        }
        return <Text style={{ fontSize: 12, whiteSpace: "nowrap" }}>{v || "—"}</Text>;
      },
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
      <div
        style={{
          display: "flex",
          alignItems: "center",
          justifyContent: "space-between",
          gap: 12,
          marginBottom: 12,
          flexWrap: "wrap",
        }}
      >
        <Space size={16} align="center" wrap>
          <div>
            <Typography.Title level={5} style={{ margin: 0, fontWeight: 600 }}>
              Мониторинг
            </Typography.Title>
            {lastUpdate && (
              <Text type="secondary" style={{ fontSize: 12 }}>
                Обновлено в {lastUpdate}
              </Text>
            )}
          </div>

          <Space size={8} wrap>
            <Input
              placeholder="Поиск ID / SN / адрес"
              prefix={<SearchOutlined />}
              value={searchInput}
              onChange={(e) => setSearchInput(e.target.value)}
              onPressEnter={handleSearch}
              allowClear
              style={{ width: 220 }}
              size="small"
            />
            <Button size="small" type="primary" onClick={handleSearch}>
              Найти
            </Button>
            <Button
              size="small"
              icon={<ReloadOutlined />}
              onClick={handleManualRefresh}
              loading={loading || simulatedRefreshing}
            >
              Обновить
            </Button>
          </Space>
        </Space>
      </div>

      <Card styles={{ body: { padding: 0 } }} style={{ width: "100%" }}>
        <Table
          className="compact-table"
          dataSource={terminals}
          columns={columns}
          rowKey="terminal_id"
          loading={loading}
          size="small"
          tableLayout="auto"
          pagination={{
            position: ["topRight", "bottomRight"],
            current: page,
            pageSize,
            total,
            defaultPageSize: 50,
            showSizeChanger: true,
            pageSizeOptions: ["10", "20", "50", "100"],
            showTotal: (tot, range) => `${range[0]}-${range[1]} из ${tot}`,
            onChange: (p, ps) => {
              setPage(p);
              setPageSize(ps);
            },
          }}
        />
      </Card>
    </>
  );
}
