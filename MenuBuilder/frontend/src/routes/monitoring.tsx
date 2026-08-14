import { useEffect, useState, useCallback } from "react";
import { Card, message, Space, Table, Tooltip, Typography } from "antd";
import { ReloadOutlined } from "@ant-design/icons";
import { getMonitoring, MonitoringTerminal } from "../api/monitoring";

const { Text } = Typography;

function SlotBar({ slots }: { slots: boolean[] }) {
  return (
    <div style={{ display: "flex", gap: 1, alignItems: "flex-end", height: 14 }}>
      {slots.map((active, i) => (
        <div
          key={i}
          style={{
            width: 3,
            height: active ? 14 : 4,
            borderRadius: 1,
            background: active ? "#52c41a" : "#ff4d4f",
            opacity: active ? 1 : 0.4,
          }}
        />
      ))}
    </div>
  );
}

function stateBg(val: string): string | undefined {
  const n = parseInt(val);
  if (isNaN(n)) return undefined;
  if (n >= 200) return "#fff1f0";
  if (n >= 100) return "#fffbe6";
  return undefined;
}

function lastnumBg(val: number): string | undefined {
  if (val >= 6) return "#fff1f0";
  if (val >= 3) return "#fffbe6";
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

export default function MonitoringPage() {
  const [terminals, setTerminals] = useState<MonitoringTerminal[]>([]);
  const [loading, setLoading] = useState(true);
  const [lastUpdate, setLastUpdate] = useState("");

  const load = useCallback(async () => {
    setLoading(true);
    try {
      const res = await getMonitoring();
      setTerminals(res.data.items);
      setLastUpdate(new Date().toLocaleTimeString());
    } catch {
      message.error("Ошибка загрузки");
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => { load(); }, [load]);
  useEffect(() => {
    const timer = setInterval(load, 60000);
    return () => clearInterval(timer);
  }, [load]);

  const columns = [
    {
      title: "ID",
      dataIndex: "device_id",
      key: "device_id",
      width: 56,
      render: (v: number) => <Text strong style={{ fontSize: 12 }}>{v}</Text>,
    },
    {
      title: "Связь",
      key: "slots",
      width: 60,
      render: (_: any, record: MonitoringTerminal) => <SlotBar slots={record.slots} />,
    },
    {
      title: "Обмен",
      dataIndex: "lastnumconn",
      key: "lastnumconn",
      width: 48,
      align: "center" as const,
      render: (v: number) => (
        <Tooltip title={lastnumTooltip(v)}>
          <span style={{ fontSize: 12, background: lastnumBg(v), padding: "0 4px", borderRadius: 2 }}>
            {v}
          </span>
        </Tooltip>
      ),
    },
    {
      title: "Валид.",
      dataIndex: "validator_state",
      key: "validator_state",
      width: 42,
      align: "center" as const,
      render: (v: string) => {
        const n = parseInt(v);
        return (
          <span style={{ fontSize: 11, background: stateBg(v), padding: "0 3px", borderRadius: 2 }}>
            {isNaN(n) ? "—" : n}
          </span>
        );
      },
    },
    {
      title: "Принт.",
      dataIndex: "printer_state",
      key: "printer_state",
      width: 42,
      align: "center" as const,
      render: (v: string) => {
        const n = parseInt(v);
        return (
          <span style={{ fontSize: 11, background: stateBg(v), padding: "0 3px", borderRadius: 2 }}>
            {isNaN(n) ? "—" : n}
          </span>
        );
      },
    },
    {
      title: "ПО",
      dataIndex: "soft_version",
      key: "soft_version",
      width: 80,
      render: (v: string) => <Text style={{ fontSize: 11 }}>{v}</Text>,
    },
    {
      title: "SN",
      dataIndex: "sn",
      key: "sn",
      width: 80,
      render: (v: string) => (
        <Tooltip title={<Typography.Text copyable style={{ fontSize: 11 }}>{v}</Typography.Text>}>
          <Text code style={{ fontSize: 10, cursor: "default" }}>
            {v.length > 10 ? v.slice(0, 10) + "…" : v}
          </Text>
        </Tooltip>
      ),
    },
    {
      title: "",
      dataIndex: "is_active",
      key: "active",
      width: 24,
      render: (v: boolean) => (
        <span style={{ color: v ? "#52c41a" : "#ff4d4f", fontSize: 14 }}>●</span>
      ),
    },
  ];

  return (
    <>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 8 }}>
        <Text strong style={{ fontSize: 14 }}>Мониторинг</Text>
        <Space size={8}>
          {lastUpdate && <Text type="secondary" style={{ fontSize: 11 }}>Обновлено: {lastUpdate}</Text>}
          <ReloadOutlined
            style={{ fontSize: 14, cursor: "pointer", color: "#1677ff" }}
            onClick={load}
          />
        </Space>
      </div>

      <Card styles={{ body: { padding: 0 } }}>
        <Table
          dataSource={terminals}
          columns={columns}
          rowKey="terminal_id"
          loading={loading}
          size="small"
          tableLayout="fixed"
          pagination={false}
        />
      </Card>
    </>
  );
}
