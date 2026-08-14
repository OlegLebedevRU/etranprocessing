import { useEffect, useState, useCallback } from "react";
import { Card, message, Space, Table, Tag, Tooltip, Typography } from "antd";
import { ReloadOutlined } from "@ant-design/icons";
import { getMonitoring, MonitoringTerminal } from "../api/monitoring";

const { Text } = Typography;

function SlotBar({ slots }: { slots: boolean[] }) {
  const labels = [
    "-2ч", "", "", "", "", "",
    "", "", "", "", "", "сейчас",
  ];
  return (
    <div style={{ display: "flex", gap: 2, alignItems: "flex-end", height: 20 }}>
      {slots.map((active, i) => (
        <Tooltip key={i} title={labels[i] || `-${12 - i}0 мин`}>
          <div
            style={{
              width: 8,
              height: active ? 18 : 8,
              borderRadius: 2,
              background: active ? "#52c41a" : "#ff4d4f",
              opacity: active ? 1 : 0.4,
              transition: "all 0.2s",
            }}
          />
        </Tooltip>
      ))}
    </div>
  );
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

  // Auto-refresh every 60 seconds
  useEffect(() => {
    const timer = setInterval(load, 60000);
    return () => clearInterval(timer);
  }, [load]);

  const columns = [
    {
      title: "ID",
      dataIndex: "device_id",
      key: "device_id",
      render: (v: number) => <Text strong style={{ fontSize: 12 }}>{v}</Text>,
    },
    {
      title: "Связь (2ч)",
      key: "slots",
      render: (_: any, record: MonitoringTerminal) => <SlotBar slots={record.slots} />,
    },
    {
      title: "SN",
      dataIndex: "sn",
      key: "sn",
      ellipsis: true,
      render: (v: string) => <Text code style={{ fontSize: 10 }}>{v}</Text>,
    },
    {
      title: "",
      dataIndex: "is_active",
      key: "active",
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
          tableLayout="auto"
          pagination={false}
        />
      </Card>
    </>
  );
}
