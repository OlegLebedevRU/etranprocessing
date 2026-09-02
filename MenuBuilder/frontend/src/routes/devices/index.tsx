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
  Grid,
  Row,
  Col,
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
  CheckCircleOutlined,
  ClockCircleOutlined,
} from "@ant-design/icons";
import { getDevices, type DeviceListItem } from "../../api/devices";
import { getAdminOrganizations, type AdminOrg } from "../../api/admin";
import DeviceTasksTab from "./DeviceTasksTab";
import DeviceEventsTab from "./DeviceEventsTab";
import DeviceTagsTab from "./DeviceTagsTab";
import DeviceConsoleTab from "./DeviceConsoleTab";
import DevicePassportTab from "./DevicePassportTab";

const { Title, Text } = Typography;
const { useBreakpoint } = Grid;

export default function DevicesManagementPage() {
  const navigate = useNavigate();
  const [searchParams, setSearchParams] = useSearchParams();
  const screens = useBreakpoint();
  const isMobile = !screens.md;

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

  // KPI Metrics for dashboard
  const kpiStats = useMemo(() => {
    let online = 0;
    let offline = 0;
    let blocked = 0;
    for (const d of devices) {
      if (d.status === "blocked" || d.is_blocked) {
        blocked++;
      } else if (d.status === "online") {
        online++;
      } else {
        offline++;
      }
    }
    return {
      total: devices.length,
      online,
      offline,
      blocked,
    };
  }, [devices]);

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
      title: isMobile ? "ID" : "№ (ID)",
      dataIndex: "device_id",
      key: "device_id",
      width: isMobile ? 70 : 100,
      fixed: isMobile ? "left" : undefined,
      sorter: (a, b) => a.device_id - b.device_id,
      render: (id, record) => {
        const isBlocked = record.status === "blocked" || record.is_blocked;
        const isOnline = record.status === "online";
        const badgeStatus = isBlocked ? "error" : isOnline ? "success" : "default";
        return (
          <Space size={4}>
            <Badge status={badgeStatus} />
            <Text strong style={isBlocked ? { color: "#cf1322", fontSize: 12 } : { fontSize: 12 }}>
              #{id}
            </Text>
          </Space>
        );
      },
    },
    {
      title: isMobile ? "SN" : "Серийный номер (SN)",
      dataIndex: "sn",
      key: "sn",
      width: isMobile ? 125 : 180,
      render: (sn) => {
        const displaySn =
          isMobile && sn.length > 8 ? `${sn.slice(0, 4)}…${sn.slice(-3)}` : sn;
        return (
          <Space size={4}>
            <Tooltip title={`Серийный номер: ${sn}`} mouseEnterDelay={0.35}>
              <Text code style={{ fontSize: 12, cursor: "pointer" }}>
                {displaySn}
              </Text>
            </Tooltip>
            <Tooltip title="Копировать SN" mouseEnterDelay={0.35}>
              <Button
                type="text"
                size="small"
                icon={<CopyOutlined style={{ fontSize: 11 }} />}
                style={{ width: 22, height: 22, padding: 0 }}
                onClick={(e) => {
                  e.stopPropagation();
                  copyToClipboard(sn);
                }}
              />
            </Tooltip>
          </Space>
        );
      },
    },
    {
      title: isMobile ? "Статус" : "Связь",
      key: "status",
      width: isMobile ? 90 : 115,
      render: (_, record) => {
        if (record.status === "blocked" || record.is_blocked) {
          const violationType = record.violation_type;
          const isSnCollision = violationType === "SN_COLLISION";
          const isDeviceClone = violationType === "DEVICE_CLONE";
          const mnemonic = isSnCollision ? "ERR:SN" : isDeviceClone ? "CLONE" : "BLOCK";
          const subText = isSnCollision ? "Коллизия" : isDeviceClone ? "Клон" : "Блок";

          return (
            <Tooltip
              mouseEnterDelay={0.35}
              title={
                <div style={{ fontSize: 12 }}>
                  <div>
                    <strong>🔴 Заблокировано: {isSnCollision ? "Коллизия SN" : isDeviceClone ? "Клон устройства" : "Инцидент безопасности"}</strong>
                  </div>
                  <div>Тип нарушения: {violationType || "SN_COLLISION"}</div>
                  {record.violation_details?.reason && (
                    <div>Причина: {record.violation_details.reason}</div>
                  )}
                  {record.violation_details?.flapping_count !== undefined && (
                    <div>Попыток (flapping): {record.violation_details.flapping_count}</div>
                  )}
                  {record.violation_details?.conflicting_hosts &&
                    record.violation_details.conflicting_hosts.length > 0 && (
                      <div>Конфликт хостов: {record.violation_details.conflicting_hosts.join(", ")}</div>
                    )}
                </div>
              }
            >
              <div style={{ lineHeight: 1.15 }}>
                <Tag
                  color="error"
                  icon={<StopOutlined style={{ fontSize: 10 }} />}
                  style={{ margin: 0, fontWeight: 700, fontSize: 10, padding: "0 4px" }}
                >
                  {mnemonic}
                </Tag>
                <div style={{ fontSize: 10, color: "#ff4d4f", marginTop: 2 }}>{subText}</div>
              </div>
            </Tooltip>
          );
        }

        const isOnline = record.status === "online";
        const ageText =
          record.ageSeconds !== undefined
            ? record.ageSeconds < 60
              ? `${record.ageSeconds}с`
              : record.ageSeconds < 3600
                ? `${Math.floor(record.ageSeconds / 60)}м`
                : record.ageSeconds < 86400
                  ? `${Math.floor(record.ageSeconds / 3600)}ч`
                  : `${Math.floor(record.ageSeconds / 86400)}д`
            : "—";

        const fullAgeText =
          record.ageSeconds !== undefined
            ? record.ageSeconds < 60
              ? `${record.ageSeconds} сек. назад`
              : record.ageSeconds < 3600
                ? `${Math.floor(record.ageSeconds / 60)} мин. назад`
                : `${Math.floor(record.ageSeconds / 3600)} ч. назад`
            : "нет данных";

        return (
          <Tooltip
            mouseEnterDelay={0.35}
            title={
              isOnline
                ? `🟢 На связи (Online), последний пинг: ${fullAgeText}`
                : `⚪ Не на связи (Offline), последний контакт: ${fullAgeText}`
            }
          >
            <div style={{ lineHeight: 1.15 }}>
              <Tag
                color={isOnline ? "success" : "default"}
                style={{
                  margin: 0,
                  fontWeight: 700,
                  fontSize: 10,
                  padding: "0 5px",
                  letterSpacing: 0.5,
                }}
              >
                {isOnline ? "ON" : "OFF"}
              </Tag>
              <div
                style={{
                  fontSize: 10,
                  color: isOnline ? "#52c41a" : "#8c8c8c",
                  marginTop: 2,
                  fontFamily: "monospace",
                }}
              >
                {ageText}
              </div>
            </div>
          </Tooltip>
        );
      },
    },
    {
      title: isMobile ? "App" : "Приложение / Платформа",
      dataIndex: "app",
      key: "app",
      width: isMobile ? 85 : 150,
      render: (app) =>
        app ? (
          <Tag color="blue" style={{ fontSize: 11, margin: 0, maxWidth: "100%", textOverflow: "ellipsis", overflow: "hidden" }}>
            {app}
          </Tag>
        ) : (
          <Text type="secondary" style={{ fontSize: 11 }}>—</Text>
        ),
    },
    {
      title: isMobile ? "Теги" : "Описание / Теги",
      key: "tags_info",
      render: (_, record) => {
        const visibleTags = isMobile ? record.tags.slice(0, 1) : record.tags.slice(0, 3);
        const hiddenCount = record.tags.length - visibleTags.length;

        const allTagsTooltip = (
          <div style={{ fontSize: 11 }}>
            {record.description && <div style={{ marginBottom: 4 }}><strong>Описание:</strong> {record.description}</div>}
            <div><strong>Все теги ({record.tags.length}):</strong></div>
            {record.tags.map((t) => (
              <div key={t.tag} style={{ fontFamily: "monospace", marginTop: 2 }}>
                {t.tag}: {t.value}
              </div>
            ))}
          </div>
        );

        return (
          <div style={{ lineHeight: 1.25 }}>
            {record.description && !isMobile && (
              <div style={{ fontSize: 11, color: "#595959", marginBottom: 2, overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap", maxWidth: 260 }}>
                {record.description}
              </div>
            )}
            {record.tags && record.tags.length > 0 ? (
              <Space size={3} wrap style={{ alignItems: "center" }}>
                {visibleTags.map((t) => (
                  <Tag
                    key={t.tag}
                    color="default"
                    style={{
                      fontSize: 10,
                      padding: "0 4px",
                      margin: 0,
                      borderRadius: 3,
                    }}
                  >
                    {t.tag}:{t.value}
                  </Tag>
                ))}
                {hiddenCount > 0 && (
                  <Tooltip title={allTagsTooltip} mouseEnterDelay={0.35}>
                    <Tag
                      style={{
                        fontSize: 10,
                        padding: "0 4px",
                        margin: 0,
                        cursor: "pointer",
                        borderRadius: 3,
                        background: "#f0f0f0",
                      }}
                    >
                      +{hiddenCount}
                    </Tag>
                  </Tooltip>
                )}
              </Space>
            ) : (
              !record.description && <Text type="secondary" style={{ fontSize: 11 }}>—</Text>
            )}
          </div>
        );
      },
    },
    {
      title: "",
      key: "actions",
      width: isMobile ? 48 : 110,
      align: "center",
      render: (_, record) =>
        isMobile ? (
          <Tooltip title="Управление устройством" mouseEnterDelay={0.35}>
            <Button
              size="small"
              type="primary"
              icon={<ControlOutlined style={{ fontSize: 12 }} />}
              style={{ width: 28, height: 28, padding: 0 }}
              onClick={(e) => {
                e.stopPropagation();
                handleOpenDevice(record, "info");
              }}
            />
          </Tooltip>
        ) : (
          <Button
            size="small"
            type="primary"
            icon={<ControlOutlined />}
            onClick={(e) => {
              e.stopPropagation();
              handleOpenDevice(record, "info");
            }}
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
          marginBottom: 14,
          flexWrap: "wrap",
          gap: 10,
        }}
      >
        <div>
          <Title level={4} style={{ margin: 0, display: "flex", alignItems: "center", gap: 8, fontSize: isMobile ? 16 : 20 }}>
            <ClusterOutlined style={{ color: "#1677ff" }} /> Управление устройствами (Leo4 IoT)
          </Title>
          <Text type="secondary" style={{ fontSize: isMobile ? 11 : 13 }}>
            Центр мониторинга IoT-терминалов, запуск RPC-задач, журнал событий и консоль диагностики
          </Text>
        </div>
        <Button
          icon={<ReloadOutlined />}
          onClick={fetchDevicesList}
          loading={loading}
          size={isMobile ? "small" : "middle"}
        >
          Обновить
        </Button>
      </div>

      {/* UX Dashboard KPI Statistic Cards */}
      <Row gutter={[8, 8]} style={{ marginBottom: 14 }}>
        <Col xs={12} sm={6}>
          <div
            onClick={() => setStatusFilter("all")}
            style={{
              cursor: "pointer",
              padding: "8px 12px",
              background: statusFilter === "all" ? "#e6f4ff" : "#fafafa",
              border: `1px solid ${statusFilter === "all" ? "#1677ff" : "#e8e8e8"}`,
              borderRadius: 6,
              transition: "all 0.2s",
            }}
          >
            <div style={{ fontSize: 11, color: "#8c8c8c", fontWeight: 500 }}>Всего устройств</div>
            <div style={{ fontSize: 18, fontWeight: 700, color: "#1f1f1f" }}>
              {kpiStats.total}
            </div>
          </div>
        </Col>
        <Col xs={12} sm={6}>
          <div
            onClick={() => setStatusFilter("online")}
            style={{
              cursor: "pointer",
              padding: "8px 12px",
              background: statusFilter === "online" ? "#f6ffed" : "#fafafa",
              border: `1px solid ${statusFilter === "online" ? "#52c41a" : "#e8e8e8"}`,
              borderRadius: 6,
              transition: "all 0.2s",
            }}
          >
            <div style={{ fontSize: 11, color: "#52c41a", fontWeight: 600, display: "flex", alignItems: "center", gap: 4 }}>
              <CheckCircleOutlined /> Онлайн (ON)
            </div>
            <div style={{ fontSize: 18, fontWeight: 700, color: "#52c41a" }}>
              {kpiStats.online}
            </div>
          </div>
        </Col>
        <Col xs={12} sm={6}>
          <div
            onClick={() => setStatusFilter("offline")}
            style={{
              cursor: "pointer",
              padding: "8px 12px",
              background: statusFilter === "offline" ? "#f5f5f5" : "#fafafa",
              border: `1px solid ${statusFilter === "offline" ? "#8c8c8c" : "#e8e8e8"}`,
              borderRadius: 6,
              transition: "all 0.2s",
            }}
          >
            <div style={{ fontSize: 11, color: "#8c8c8c", fontWeight: 500, display: "flex", alignItems: "center", gap: 4 }}>
              <ClockCircleOutlined /> Оффлайн (OFF)
            </div>
            <div style={{ fontSize: 18, fontWeight: 700, color: "#595959" }}>
              {kpiStats.offline}
            </div>
          </div>
        </Col>
        <Col xs={12} sm={6}>
          <div
            onClick={() => setStatusFilter("blocked")}
            style={{
              cursor: "pointer",
              padding: "8px 12px",
              background: statusFilter === "blocked" ? "#fff1f0" : "#fafafa",
              border: `1px solid ${statusFilter === "blocked" ? "#ff4d4f" : "#e8e8e8"}`,
              borderRadius: 6,
              transition: "all 0.2s",
            }}
          >
            <div style={{ fontSize: 11, color: "#ff4d4f", fontWeight: 600, display: "flex", alignItems: "center", gap: 4 }}>
              <StopOutlined /> Блокировки (ERR)
            </div>
            <div style={{ fontSize: 18, fontWeight: 700, color: "#ff4d4f" }}>
              {kpiStats.blocked}
            </div>
          </div>
        </Col>
      </Row>

      {/* Filter Toolbar */}
      <div
        style={{
          display: "flex",
          flexWrap: "wrap",
          gap: 8,
          marginBottom: 14,
          padding: isMobile ? "10px" : "12px",
          background: "#fafafa",
          border: "1px solid #f0f0f0",
          borderRadius: 6,
          alignItems: "center",
        }}
      >
        <Select
          placeholder="Организация"
          value={selectedOrgId}
          onChange={(val) => setSelectedOrgId(val)}
          style={{ flex: isMobile ? "1 1 100%" : "0 0 230px" }}
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
          style={{ flex: isMobile ? "1 1 100%" : "0 0 200px" }}
          options={[
            { value: "all", label: "Все статусы" },
            { value: "online", label: "Только онлайн [ON]" },
            { value: "offline", label: "Только оффлайн [OFF]" },
            { value: "blocked", label: "Заблокированные [ERR]" },
          ]}
        />

        <Input
          placeholder="Поиск ID, SN, имени, app, тегам..."
          prefix={<SearchOutlined style={{ color: "#bfbfbf" }} />}
          value={searchQuery}
          onChange={(e) => setSearchQuery(e.target.value)}
          style={{ flex: isMobile ? "1 1 100%" : "1 1 240px" }}
          allowClear
        />

        <div style={{ marginLeft: isMobile ? 0 : "auto", width: isMobile ? "100%" : "auto" }}>
          <Text type="secondary" style={{ fontSize: 12 }}>
            Найдено: <strong>{filteredDevices.length}</strong>
          </Text>
        </div>
      </div>

      {/* Devices Table */}
      <Table
        className="compact-table"
        rowKey="device_id"
        columns={columns}
        dataSource={filteredDevices}
        loading={loading}
        size="small"
        scroll={{ x: 680 }}
        onRow={(record) => ({
          onClick: () => handleOpenDevice(record, "info"),
          style: { cursor: "pointer" },
        })}
        pagination={{
          position: ["topRight", "bottomRight"],
          defaultPageSize: 20,
          showSizeChanger: true,
          pageSizeOptions: ["10", "20", "50", "100"],
          simple: isMobile,
          size: "small",
        }}
      />

      {/* Device Detail Drawer */}
      <Drawer
        title={
          selectedDevice ? (
            <Space size={8} wrap style={{ alignItems: "center" }}>
              <ClusterOutlined style={{ color: "#1677ff" }} />
              <span style={{ fontSize: isMobile ? 14 : 16, fontWeight: 600 }}>
                #{selectedDevice.device_id} (SN: {selectedDevice.sn})
              </span>
              {selectedDevice.status === "blocked" || selectedDevice.is_blocked ? (
                <Tag color="error" icon={<StopOutlined />} style={{ fontSize: 11, margin: 0 }}>
                  {selectedDevice.violation_type === "SN_COLLISION"
                    ? "ERR:SN (Коллизия)"
                    : selectedDevice.violation_type === "DEVICE_CLONE"
                      ? "CLONE (Клон)"
                      : "BLOCK (Блокировка)"}
                </Tag>
              ) : (
                <Tag
                  color={selectedDevice.status === "online" ? "success" : "default"}
                  style={{ fontSize: 11, margin: 0, fontWeight: 600 }}
                >
                  {selectedDevice.status === "online" ? "ON (Онлайн)" : "OFF (Оффлайн)"}
                </Tag>
              )}
            </Space>
          ) : (
            "Устройство"
          )
        }
        open={Boolean(selectedDevice)}
        onClose={handleCloseDrawer}
        width={isMobile ? "100%" : 880}
        styles={{
          body: {
            padding: isMobile ? "10px 8px" : "16px 20px",
          },
        }}
        destroyOnClose
      >
        {selectedDevice && selectedOrgId !== undefined && (
          <Tabs
            activeKey={activeTabKey}
            onChange={setActiveTabKey}
            size={isMobile ? "small" : "middle"}
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
