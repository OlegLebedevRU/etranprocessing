import { useEffect, useState, useCallback, useMemo } from "react";
import { useSearchParams, useNavigate } from "react-router";
import {
  Card,
  Table,
  Tag,
  Button,
  Input,
  Select,
  Space,
  Badge,
  Typography,
  Tooltip,
  Drawer,
  Tabs,
  Alert,
  message,
  Descriptions,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import {
  ReloadOutlined,
  SearchOutlined,
  ClusterOutlined,
  CodeOutlined,
  UnorderedListOutlined,
  TagsOutlined,
  DashboardOutlined,
  InfoCircleOutlined,
  CopyOutlined,
  ControlOutlined,
  StopOutlined,
} from "@ant-design/icons";
import { getDevices, type DeviceListItem } from "../../api/devices";
import { getAdminOrganizations, type AdminOrg } from "../../api/admin";
import DeviceTasksTab from "./DeviceTasksTab";
import DeviceEventsTab from "./DeviceEventsTab";
import DeviceTagsTab from "./DeviceTagsTab";
import DeviceConsoleTab from "./DeviceConsoleTab";
import DevicePassportTab from "./DevicePassportTab";

const { Title, Text } = Typography;

export default function DevicesManagementPage() {
  const navigate = useNavigate();
  const [searchParams, setSearchParams] = useSearchParams();

  // Superuser check
  const isSuperuser = localStorage.getItem("mb_is_superuser") === "true";

  // Data states
  const [loading, setLoading] = useState(false);
  const [devices, setDevices] = useState<DeviceListItem[]>([]);
  const [orgs, setOrgs] = useState<AdminOrg[]>([]);

  // Filter states
  const [selectedOrgId, setSelectedOrgId] = useState<number | undefined>(() => {
    const stored = localStorage.getItem("mb_current_org_id");
    return stored ? Number(stored) : undefined;
  });
  const [searchQuery, setSearchQuery] = useState("");
  const [statusFilter, setStatusFilter] = useState<string>("all");

  // Selected device for drawer
  const [selectedDevice, setSelectedDevice] = useState<DeviceListItem | null>(null);
  const [activeTabKey, setActiveTabKey] = useState<string>("info");

  // Load organizations for filter
  useEffect(() => {
    getAdminOrganizations()
      .then((data) => {
        setOrgs(data);
        if (data.length > 0) {
          setSelectedOrgId((prev) => {
            if (prev !== undefined && data.some((o) => o.org_id === prev)) {
              return prev;
            }
            return data[0].org_id;
          });
        }
      })
      .catch(() => {});
  }, []);

  // Fetch devices
  const fetchDevicesList = useCallback(async () => {
    if (selectedOrgId === undefined) return;
    setLoading(true);
    try {
      const items = await getDevices(selectedOrgId);
      setDevices(items);

      // Deep link support via ?device_id=X
      const urlDeviceId = searchParams.get("device_id");
      if (urlDeviceId) {
        const found = items.find((d) => String(d.device_id) === urlDeviceId);
        if (found) {
          setSelectedDevice(found);
        }
      }
    } catch (err: any) {
      message.error(err.message || "Ошибка загрузки списка IoT устройств");
    } finally {
      setLoading(false);
    }
  }, [selectedOrgId, searchParams]);

  useEffect(() => {
    fetchDevicesList();
  }, [fetchDevicesList]);

  // Handle opening device drawer
  const handleOpenDevice = (dev: DeviceListItem, defaultTab = "info") => {
    setSelectedDevice(dev);
    setActiveTabKey(defaultTab);
    setSearchParams({ device_id: String(dev.device_id) });
  };

  const handleCloseDrawer = () => {
    setSelectedDevice(null);
    setSearchParams({});
  };

  const copyToClipboard = (text: string) => {
    navigator.clipboard.writeText(text).then(() => {
      message.success("Скопировано в буфер обмена");
    });
  };

  // Filtered devices list
  const filteredDevices = useMemo(() => {
    return devices.filter((d) => {
      // Status filter
      if (statusFilter === "online" && d.status !== "online") return false;
      if (statusFilter === "offline" && d.status !== "offline") return false;
      if (statusFilter === "blocked" && d.status !== "blocked" && !d.is_blocked)
        return false;

      // Search query
      if (searchQuery.trim()) {
        const query = searchQuery.trim().toLowerCase();
        const matchesId = String(d.device_id).includes(query);
        const matchesSn = d.sn.toLowerCase().includes(query);
        const matchesApp = d.app?.toLowerCase().includes(query);
        const matchesDesc = d.description?.toLowerCase().includes(query);
        const matchesViolation = (d.violation_type || "").toLowerCase().includes(query);
        const matchesReason = (d.violation_details?.reason || "").toLowerCase().includes(query);
        const matchesTags = d.tags.some(
          (t) =>
            t.tag.toLowerCase().includes(query) ||
            t.value.toLowerCase().includes(query)
        );
        return (
          matchesId ||
          matchesSn ||
          matchesApp ||
          matchesDesc ||
          matchesViolation ||
          matchesReason ||
          matchesTags
        );
      }
      return true;
    });
  }, [devices, statusFilter, searchQuery]);

  if (!isSuperuser) {
    return (
      <div style={{ padding: 24 }}>
        <Alert
          type="error"
          showIcon
          message="Доступ ограничен"
          description="Раздел «Управление устройствами» доступен только суперадминистраторам системы."
          action={
            <Button type="primary" onClick={() => navigate("/monitoring")}>
              В Мониторинг
            </Button>
          }
        />
      </div>
    );
  }

  const columns: ColumnsType<DeviceListItem> = [
    {
      title: "№ (ID)",
      dataIndex: "device_id",
      key: "device_id",
      width: 140,
      sorter: (a, b) => a.device_id - b.device_id,
      render: (id, record) => {
        const isBlocked = record.status === "blocked" || record.is_blocked;
        const isOnline = record.status === "online";
        const badgeStatus = isBlocked ? "error" : isOnline ? "success" : "default";
        return (
          <Space>
            <Badge status={badgeStatus} />
            <Text strong style={isBlocked ? { color: "#cf1322" } : undefined}>
              #{id}
            </Text>
          </Space>
        );
      },
    },
    {
      title: "Серийный номер (SN)",
      dataIndex: "sn",
      key: "sn",
      width: 220,
      render: (sn) => (
        <Space size={4}>
          <Text code style={{ fontSize: 13 }}>
            {sn}
          </Text>
          <Tooltip title="Копировать SN">
            <Button
              type="text"
              size="small"
              icon={<CopyOutlined />}
              onClick={(e) => {
                e.stopPropagation();
                copyToClipboard(sn);
              }}
            />
          </Tooltip>
        </Space>
      ),
    },
    {
      title: "Статус связи",
      key: "status",
      width: 190,
      render: (_, record) => {
        if (record.status === "blocked" || record.is_blocked) {
          const violationType = record.violation_type;
          const isSnCollision = violationType === "SN_COLLISION";
          const isDeviceClone = violationType === "DEVICE_CLONE";
          const label = isSnCollision
            ? "Коллизия SN"
            : isDeviceClone
              ? "Клон устройства"
              : "Заблокировано";
          const subLabel = isSnCollision
            ? "Дубликат сертификата"
            : isDeviceClone
              ? "Клон накопителя / IP"
              : record.violation_details?.reason || "Инцидент безопасности";

          return (
            <Tooltip
              title={
                <div style={{ fontSize: 12 }}>
                  <div>
                    <strong>🔴 Доступ заблокирован</strong>
                  </div>
                  <div>Тип нарушения: {violationType || "SN_COLLISION"}</div>
                  {record.violation_details?.reason && (
                    <div>Причина: {record.violation_details.reason}</div>
                  )}
                  {record.violation_details?.flapping_count !== undefined && (
                    <div>
                      Попыток (flapping):{" "}
                      {record.violation_details.flapping_count}
                    </div>
                  )}
                  {record.violation_details?.conflicting_hosts &&
                    record.violation_details.conflicting_hosts.length > 0 && (
                      <div>
                        Конфликт хостов:{" "}
                        {record.violation_details.conflicting_hosts.join(", ")}
                      </div>
                    )}
                </div>
              }
            >
              <Space direction="vertical" size={2}>
                <Tag
                  color="error"
                  icon={<StopOutlined />}
                  style={{ margin: 0, fontWeight: 500 }}
                >
                  {label}
                </Tag>
                <span style={{ fontSize: 11, color: "#ff4d4f" }}>{subLabel}</span>
              </Space>
            </Tooltip>
          );
        }

        const isOnline = record.status === "online";
        const ageText =
          record.ageSeconds !== undefined
            ? record.ageSeconds < 60
              ? `${record.ageSeconds}с назад`
              : `${Math.floor(record.ageSeconds / 60)}м назад`
            : "—";

        return (
          <Space direction="vertical" size={2}>
            <Tag color={isOnline ? "success" : "default"} style={{ margin: 0 }}>
              {isOnline ? "Онлайн" : "Оффлайн"}
            </Tag>
            <span style={{ fontSize: 11, color: "#8c8c8c" }}>{ageText}</span>
          </Space>
        );
      },
    },
    {
      title: "Приложение / Платформа",
      dataIndex: "app",
      key: "app",
      width: 180,
      render: (app) => (app ? <Tag color="blue">{app}</Tag> : <Text type="secondary">—</Text>),
    },
    {
      title: "Описание / Теги",
      key: "tags_info",
      render: (_, record) => (
        <Space direction="vertical" size={2}>
          {record.description && (
            <Text style={{ fontSize: 12 }}>{record.description}</Text>
          )}
          {record.tags && record.tags.length > 0 && (
            <Space size={4} wrap>
              {record.tags.slice(0, 3).map((t) => (
                <Tag key={t.tag} color="default" style={{ fontSize: 11 }}>
                  {t.tag}: {t.value}
                </Tag>
              ))}
              {record.tags.length > 3 && (
                <Tag style={{ fontSize: 11 }}>+{record.tags.length - 3}</Tag>
              )}
            </Space>
          )}
        </Space>
      ),
    },
    {
      title: "Действия",
      key: "actions",
      width: 140,
      align: "center",
      render: (_, record) => (
        <Button
          size="small"
          type="primary"
          icon={<ControlOutlined />}
          onClick={() => handleOpenDevice(record, "info")}
        >
          Управление
        </Button>
      ),
    },
  ];

  return (
    <Card bordered={false} style={{ borderRadius: 8 }}>
      {/* Title & Description */}
      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          alignItems: "center",
          marginBottom: 16,
          flexWrap: "wrap",
          gap: 12,
        }}
      >
        <div>
          <Title level={4} style={{ margin: 0, display: "flex", alignItems: "center", gap: 8 }}>
            <ClusterOutlined style={{ color: "#1677ff" }} /> Управление устройствами (Leo4 IoT)
          </Title>
          <Text type="secondary">
            Центр управления IoT-устройствами, запуск асинхронных RPC-задач, журнал событий и консоль диагностики
          </Text>
        </div>
        <Button icon={<ReloadOutlined />} onClick={fetchDevicesList} loading={loading}>
          Обновить список
        </Button>
      </div>

      {/* Filter Toolbar */}
      <div
        style={{
          display: "flex",
          flexWrap: "wrap",
          gap: 10,
          marginBottom: 16,
          padding: "12px",
          background: "#fafafa",
          borderRadius: 6,
          alignItems: "center",
        }}
      >
        <Select
          placeholder="Организация"
          value={selectedOrgId}
          onChange={(val) => setSelectedOrgId(val)}
          style={{ width: 240 }}
          showSearch
          optionFilterProp="label"
          options={orgs.map((o) => ({
            value: o.org_id,
            label: `${o.org_name} (#${o.org_id})`,
          }))}
        />

        <Select
          value={statusFilter}
          onChange={(val) => setStatusFilter(val)}
          style={{ width: 230 }}
          options={[
            { value: "all", label: "Все статусы" },
            { value: "online", label: "Только онлайн" },
            { value: "offline", label: "Только оффлайн" },
            { value: "blocked", label: "Заблокированные (Коллизии / Клоны)" },
          ]}
        />

        <Input
          placeholder="Поиск по ID, SN, имени, приложению или коллизии..."
          prefix={<SearchOutlined style={{ color: "#bfbfbf" }} />}
          value={searchQuery}
          onChange={(e) => setSearchQuery(e.target.value)}
          style={{ width: 300 }}
          allowClear
        />

        <div style={{ marginLeft: "auto" }}>
          <Text type="secondary" style={{ fontSize: 13 }}>
            Найдено устройств: <strong>{filteredDevices.length}</strong>
          </Text>
        </div>
      </div>

      {/* Devices Table */}
      <Table
        rowKey="device_id"
        columns={columns}
        dataSource={filteredDevices}
        loading={loading}
        size="small"
        onRow={(record) => ({
          onClick: () => handleOpenDevice(record, "info"),
          style: { cursor: "pointer" },
        })}
        pagination={{
          position: ["topRight", "bottomRight"],
          defaultPageSize: 20,
          showSizeChanger: true,
          pageSizeOptions: ["10", "20", "50", "100"],
        }}
      />

      {/* Device Detail Drawer */}
      <Drawer
        title={
          selectedDevice ? (
            <Space size={12} wrap>
              <ClusterOutlined style={{ color: "#1677ff" }} />
              <span>
                Устройство #{selectedDevice.device_id} (SN: {selectedDevice.sn})
              </span>
              {selectedDevice.status === "blocked" || selectedDevice.is_blocked ? (
                <Tag color="error" icon={<StopOutlined />}>
                  Заблокировано (
                  {selectedDevice.violation_type === "SN_COLLISION"
                    ? "Коллизия сертификатов"
                    : selectedDevice.violation_type === "DEVICE_CLONE"
                      ? "Клон устройства"
                      : "Инцидент безопасности"}
                  )
                </Tag>
              ) : (
                <Tag
                  color={
                    selectedDevice.status === "online" ? "success" : "default"
                  }
                >
                  {selectedDevice.status === "online" ? "Онлайн" : "Оффлайн"}
                </Tag>
              )}
            </Space>
          ) : (
            "Устройство"
          )
        }
        open={Boolean(selectedDevice)}
        onClose={handleCloseDrawer}
        width={880}
        destroyOnClose
      >
        {selectedDevice && selectedOrgId !== undefined && (
          <Tabs
            activeKey={activeTabKey}
            onChange={setActiveTabKey}
            items={[
              {
                key: "info",
                label: (
                  <span>
                    <InfoCircleOutlined /> Паспорт
                  </span>
                ),
                children: (
                  <DevicePassportTab
                    deviceId={selectedDevice.device_id}
                    sn={selectedDevice.sn}
                    orgId={selectedOrgId}
                  />
                ),
              },
              {
                key: "events",
                label: (
                  <span>
                    <DashboardOutlined /> События
                  </span>
                ),
                children: (
                  <DeviceEventsTab
                    deviceId={selectedDevice.device_id}
                    sn={selectedDevice.sn}
                    orgId={selectedOrgId}
                  />
                ),
              },
              {
                key: "tasks",
                label: (
                  <span>
                    <UnorderedListOutlined /> Команды (RPC)
                  </span>
                ),
                children: (
                  <DeviceTasksTab
                    deviceId={selectedDevice.device_id}
                    sn={selectedDevice.sn}
                    orgId={selectedOrgId}
                  />
                ),
              },
              {
                key: "tags",
                label: (
                  <span>
                    <TagsOutlined /> Теги
                  </span>
                ),
                children: (
                  <DeviceTagsTab
                    deviceId={selectedDevice.device_id}
                    orgId={selectedOrgId}
                    tags={selectedDevice.tags}
                    onTagsUpdated={fetchDevicesList}
                  />
                ),
              },
              {
                key: "console",
                label: (
                  <span>
                    <CodeOutlined /> Консоль
                  </span>
                ),
                children: (
                  <DeviceConsoleTab
                    sn={selectedDevice.sn}
                    app={selectedDevice.app}
                    sys={selectedDevice.sys}
                    tags={selectedDevice.tags}
                    orgId={selectedOrgId}
                    isActiveTab={activeTabKey === "console"}
                  />
                ),
              },
            ]}
          />
        )}
      </Drawer>
    </Card>
  );
}
