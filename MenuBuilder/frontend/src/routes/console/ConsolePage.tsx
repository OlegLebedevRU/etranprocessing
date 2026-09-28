import { useState, useEffect, useCallback } from "react";
import {
  Card,
  Select,
  Space,
  Tag,
  Typography,
  Button,
  Spin,
  Alert,
  Empty,
} from "antd";
import {
  CodeOutlined,
  ReloadOutlined,
  CheckCircleOutlined,
  DisconnectOutlined,
  DesktopOutlined,
} from "@ant-design/icons";
import { useNavigate, useSearchParams } from "react-router";
import { getDevices, type DeviceListItem } from "../../api/devices";
import { useSession } from "../../session/SessionContext";
import DeviceConsoleTab from "../devices/DeviceConsoleTab";

const { Text, Title, Paragraph } = Typography;

export default function ConsolePage() {
  const { user } = useSession();
  const navigate = useNavigate();
  const [searchParams, setSearchParams] = useSearchParams();
  const orgId = typeof user?.org_id === "number" ? user.org_id : 1;

  const [loading, setLoading] = useState(true);
  const [devices, setDevices] = useState<DeviceListItem[]>([]);
  const [selectedDevice, setSelectedDevice] = useState<DeviceListItem | null>(null);

  const fetchDevices = useCallback(async () => {
    setLoading(true);
    try {
      const res = await getDevices(orgId, { page: 1, size: 100 });
      const items = res.items || [];
      setDevices(items);

      // Check query param ?sn=...
      const snParam = searchParams.get("sn");
      if (snParam) {
        const found = items.find((d) => d.sn === snParam);
        if (found) {
          setSelectedDevice(found);
          return;
        }
      }

      setSelectedDevice((prev) => {
        if (prev && items.some((d) => d.device_id === prev.device_id)) {
          return prev;
        }
        return items.length > 0 ? items[0] : null;
      });
    } catch {
      // ignore
    } finally {
      setLoading(false);
    }
  }, [orgId, searchParams]);

  useEffect(() => {
    void fetchDevices();
  }, [fetchDevices]);

  const handleDeviceChange = (deviceId: number) => {
    const found = devices.find((d) => d.device_id === deviceId);
    if (found) {
      setSelectedDevice(found);
      setSearchParams({ sn: found.sn });
    }
  };

  const isOnline = selectedDevice?.status === "online";

  return (
    <div style={{ maxWidth: 1400, margin: "0 auto" }}>
      {/* Header bar */}
      <Card
        size="small"
        style={{ marginBottom: 16, borderRadius: 8 }}
        styles={{ body: { padding: "12px 16px" } }}
      >
        <div
          style={{
            display: "flex",
            justifyContent: "space-between",
            alignItems: "center",
            flexWrap: "wrap",
            gap: 12,
          }}
        >
          <Space align="center" size="middle">
            <Space align="center">
              <CodeOutlined style={{ fontSize: 20, color: "#1677ff" }} />
              <Title level={4} style={{ margin: 0, fontSize: 16 }}>
                Консоль управления
              </Title>
            </Space>

            {devices.length > 0 && (
              <Select
                style={{ minWidth: 260 }}
                placeholder="Выберите терминал"
                value={selectedDevice?.device_id}
                onChange={handleDeviceChange}
                options={devices.map((d) => ({
                  value: d.device_id,
                  label: (
                    <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between" }}>
                      <span style={{ fontWeight: 500 }}>{d.sn}</span>
                      <Tag
                        color={d.status === "online" ? "success" : "default"}
                        style={{ fontSize: 11, marginLeft: 8 }}
                      >
                        {d.status === "online" ? "Online" : "Offline"}
                      </Tag>
                    </div>
                  ),
                }))}
              />
            )}

            {selectedDevice && (
              <Space size="small">
                {isOnline ? (
                  <Tag icon={<CheckCircleOutlined />} color="success">
                    На связи
                  </Tag>
                ) : (
                  <Tag icon={<DisconnectOutlined />} color="default">
                    Не в сети
                  </Tag>
                )}
                {selectedDevice.sys && (
                  <Tag color="blue">{selectedDevice.sys.toUpperCase()}</Tag>
                )}
              </Space>
            )}
          </Space>

          <Space>
            <Button
              icon={<ReloadOutlined spin={loading} />}
              onClick={fetchDevices}
              size="middle"
            >
              Обновить
            </Button>
          </Space>
        </div>
      </Card>

      {/* Loading state */}
      {loading && devices.length === 0 && (
        <Card style={{ textAlign: "center", padding: "48px 0" }}>
          <Spin size="large" />
          <div style={{ marginTop: 16, color: "#8c8c8c" }}>Загрузка списка терминалов...</div>
        </Card>
      )}

      {/* Empty state: No devices */}
      {!loading && devices.length === 0 && (
        <Card style={{ textAlign: "center", padding: "48px 24px" }}>
          <Empty
            description={
              <div>
                <Title level={5}>Нет подключённых терминалов</Title>
                <Paragraph type="secondary" style={{ maxWidth: 460, margin: "0 auto 16px" }}>
                  Для использования консоли управления сначала создайте терминал
                  в разделе «Терминалы».
                </Paragraph>
              </div>
            }
          >
            <Button
              type="primary"
              size="large"
              icon={<DesktopOutlined />}
              onClick={() => navigate("/terminals")}
            >
              Перейти в «Терминалы»
            </Button>
          </Empty>
        </Card>
      )}

      {/* Main Console Tab reuse */}
      {selectedDevice && (
        <div>
          {!isOnline && (
            <Alert
              type="warning"
              showIcon
              style={{ marginBottom: 16 }}
              message="Терминал не в сети"
              description="Команды консоли могут не выполняться, пока компьютер не подключится к сети и служба Агента не перейдёт в активный режим."
            />
          )}

          <DeviceConsoleTab
            deviceId={selectedDevice.device_id}
            sn={selectedDevice.sn}
            app={selectedDevice.app}
            sys={selectedDevice.sys}
            tags={selectedDevice.tags}
            orgId={orgId}
            isActiveTab={true}
          />
        </div>
      )}
    </div>
  );
}
