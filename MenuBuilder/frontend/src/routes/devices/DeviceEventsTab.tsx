import { useState, useEffect, useCallback } from "react";
import { Table, Tag, Button, Typography, message, Space } from "antd";
import type { ColumnsType } from "antd/es/table";
import { ReloadOutlined } from "@ant-design/icons";
import { getDeviceEvents, type DeviceEventItem } from "../../api/devices";

const { Text } = Typography;

interface DeviceEventsTabProps {
  deviceId: number;
  sn: string;
  orgId: number;
}

export default function DeviceEventsTab({ deviceId, orgId }: DeviceEventsTabProps) {
  const [loading, setLoading] = useState(false);
  const [events, setEvents] = useState<DeviceEventItem[]>([]);
  const [total, setTotal] = useState(0);
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(20);

  const fetchEvents = useCallback(async () => {
    setLoading(true);
    try {
      const res = await getDeviceEvents(orgId, deviceId, page, pageSize);
      setEvents(res.items || []);
      setTotal(res.total || 0);
    } catch (err: any) {
      message.error(err.message || "Ошибка загрузки журнала событий");
    } finally {
      setLoading(false);
    }
  }, [orgId, deviceId, page, pageSize]);

  useEffect(() => {
    fetchEvents();
  }, [fetchEvents]);

  const getEventName = (code: number, payload?: any) => {
    if (payload && typeof payload === "object") {
      if (typeof payload["200"] === "number" && payload["200"] === 0) {
        return "Старт устройства";
      }
    }
    switch (code) {
      case 44:
        return "Пинг (Heartbeat)";
      case 45:
        return "Нажатие сервисной кнопки";
      case 3:
        return "Прикладывание карты / ввод PIN";
      case 13:
        return "Открытие замка";
      case 14:
        return "Закрытие замка";
      case 52:
        return "Загрузка прошивки STM32";
      case 53:
        return "Обновление прошивки STM32";
      default:
        return `Событие #${code}`;
    }
  };

  const columns: ColumnsType<DeviceEventItem> = [
    {
      title: "Дата и время",
      dataIndex: "created_at",
      key: "created_at",
      width: 180,
      render: (val) => (
        <span style={{ fontSize: 13 }}>
          {val ? new Date(val).toLocaleString("ru-RU") : "—"}
        </span>
      ),
    },
    {
      title: "Код / Тип события",
      dataIndex: "event_type_code",
      key: "event_type_code",
      width: 220,
      render: (code, record) => (
        <Space direction="vertical" size={2}>
          <Tag color="geekblue" style={{ margin: 0 }}>
            Код {code}
          </Tag>
          <Text strong style={{ fontSize: 12 }}>
            {getEventName(code, record.payload)}
          </Text>
        </Space>
      ),
    },
    {
      title: "Полезная нагрузка (Payload)",
      dataIndex: "payload",
      key: "payload",
      render: (payload) => {
        if (!payload) return <Text type="secondary">—</Text>;
        const jsonStr = typeof payload === "object" ? JSON.stringify(payload) : String(payload);
        return (
          <code
            style={{
              background: "#f5f5f5",
              padding: "2px 6px",
              borderRadius: 4,
              fontSize: 12,
              wordBreak: "break-all",
              display: "inline-block",
              maxWidth: 500,
            }}
          >
            {jsonStr.length > 120 ? `${jsonStr.slice(0, 120)}...` : jsonStr}
          </code>
        );
      },
    },
  ];

  return (
    <div>
      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          alignItems: "center",
          marginBottom: 16,
          flexWrap: "wrap",
          gap: 8,
        }}
      >
        <Text type="secondary">
          Журнал входящих событий от устройства #{deviceId}
        </Text>
        <Button icon={<ReloadOutlined />} onClick={fetchEvents} loading={loading}>
          Обновить
        </Button>
      </div>

      <Table
        className="compact-table"
        rowKey={(r, idx) => r.id ? String(r.id) : `${r.created_at}_${idx}`}
        columns={columns}
        dataSource={events}
        loading={loading}
        size="small"
        scroll={{ x: 600 }}
        expandable={{
          expandedRowRender: (record) => (
            <div style={{ margin: 0 }}>
              <Text type="secondary" style={{ fontSize: 12 }}>
                Полный payload:
              </Text>
              <pre
                style={{
                  background: "#f9f9f9",
                  padding: 8,
                  borderRadius: 4,
                  fontSize: 11,
                  marginTop: 4,
                  maxHeight: 180,
                  overflowY: "auto",
                }}
              >
                {JSON.stringify(record.payload, null, 2)}
              </pre>
            </div>
          ),
          rowExpandable: (record) => Boolean(record.payload),
        }}
        pagination={{
          current: page,
          pageSize,
          total,
          showSizeChanger: true,
          pageSizeOptions: ["10", "20", "50", "100"],
          onChange: (p, ps) => {
            setPage(p);
            setPageSize(ps);
          },
        }}
      />
    </div>
  );
}
