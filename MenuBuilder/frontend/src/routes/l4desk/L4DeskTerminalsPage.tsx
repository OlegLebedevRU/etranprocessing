import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  Alert,
  Button,
  Card,
  Empty,
  Tooltip,
  Input,
  Pagination,
  Popconfirm,
  Popover,
  Segmented,
  Select,
  Space,
  Table,
  Tag,
  Typography,
  message,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import {
  ClockCircleOutlined,
  CodeOutlined,
  DesktopOutlined,
  EditOutlined,
  PoweroffOutlined,
  PlusOutlined,
  KeyOutlined,
  ReloadOutlined,
  SafetyCertificateOutlined,
  SyncOutlined,
  VideoCameraOutlined,
} from "@ant-design/icons";
import { useNavigate } from "react-router";
import {
  listTerminalsSettings,
  retryTerminalOnboarding,
  getTerminalPin,
  renewTerminalPin,
  setTerminalActivity,
  type TerminalPin,
  type TerminalSettingsItem,
} from "../../api/settings";
import { useSession } from "../../session/SessionContext";
import OnboardingWizardModal from "../../components/OnboardingWizardModal";
import { getDevices, type DeviceListItem } from "../../api/devices";
import TerminalSettingsEditModal from "../../components/TerminalSettingsEditModal";
import { certificatePresentation, filterTerminals, terminalStatus, type TerminalFilter } from "../../utils/terminalPresentation";

const { Text, Title } = Typography;

function pinTag(item: TerminalSettingsItem) {
  if (item.pin_state === "issued") {
    return (
      <Tag icon={<SafetyCertificateOutlined />} color="processing">
        PIN выдан
      </Tag>
    );
  }
  if (item.pin_state === "consumed") {
    return (
      <Tag icon={<SafetyCertificateOutlined />} color="success">
        PIN активирован
      </Tag>
    );
  }
  return (
    <Tag icon={<ClockCircleOutlined />} color="default">
      PIN: {item.pin_state || "pending"}
    </Tag>
  );
}

export default function L4DeskTerminalsPage() {
  const { user } = useSession();
  const canChangeTerminals = !user?.is_superuser && (user?.role_id === 3 || user?.role_id === 5);
  const navigate = useNavigate();
  const [loading, setLoading] = useState(true);
  const [terminals, setTerminals] = useState<TerminalSettingsItem[]>([]);
  const [totalCount, setTotalCount] = useState(0);
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(20);
  const [searchInput, setSearchInput] = useState("");
  const [search, setSearch] = useState("");
  const [deviceInput, setDeviceInput] = useState("");
  const [deviceFilter, setDeviceFilter] = useState("");
  const [sortBy, setSortBy] = useState("device_id");
  const [sortOrder, setSortOrder] = useState<"asc" | "desc">("asc");
  const [statusFilter, setStatusFilter] = useState<TerminalFilter>("online");
  const [activityPending, setActivityPending] = useState<number | null>(null);
  const [presenceError, setPresenceError] = useState(false);
  const [listTenant, setListTenant] = useState<number | null | undefined>(user?.org_id);
  const [wizardOpen, setWizardOpen] = useState(false);
  const [editingTerminal, setEditingTerminal] = useState<TerminalSettingsItem | null>(null);
  const [retryingId, setRetryingId] = useState<number | null>(null);
  const [devices, setDevices] = useState<Map<number, DeviceListItem>>(new Map());
  const fetching = useRef(false);
  const generation = useRef(0);
  const [pins, setPins] = useState<Map<number, TerminalPin | null>>(new Map());
  const [issuingPin, setIssuingPin] = useState<number | null>(null);
  const pinOperations = useRef(new Map<number, string>());

  const fetchTerminals = useCallback(async (silent = false) => {
    if (fetching.current) return;
    const currentGeneration = generation.current;
    fetching.current = true;
    if (!silent) setLoading(true);
    try {
      const data = await listTerminalsSettings({
        org_id: user?.org_id || undefined,
        search: search || undefined,
        device_filter: deviceFilter || undefined,
        sort_by: sortBy,
        sort_order: sortOrder,
        all: true,
      });
      if (currentGeneration !== generation.current) return;
      setListTenant(user?.org_id);
      setTerminals(data.items || []);
      setTotalCount(data.total_count);
      // Use the management screen's connection view; database flags are not live presence.
      try {
        const current = new Map<number, DeviceListItem>();
        const missing = new Set(data.items.map(item => item.device_id));
        let page = 1;
        while (missing.size) {
          const result = await getDevices({ orgId: user?.org_id ?? undefined, page, size: 100 });
          for (const device of result.items) {
            if (missing.delete(device.device_id)) current.set(device.device_id, device);
          }
          if (page >= result.pages || result.items.length === 0) break;
          page += 1;
        }
        if (currentGeneration === generation.current) { setDevices(current); setPresenceError(false); }
      } catch {
        if (currentGeneration === generation.current) { setDevices(new Map()); setPresenceError(true); }
      }
    } catch (error: any) {
      if (currentGeneration === generation.current) {
        setTerminals([]);
        setTotalCount(0);
        setPins(new Map());
        setDevices(new Map());
        const detail = error?.response?.data?.detail;
        message.error(typeof detail === "string" ? detail : "Не удалось загрузить список терминалов");
      }
    } finally {
      if (currentGeneration === generation.current) {
        setLoading(false);
        fetching.current = false;
      }
    }
  }, [user?.org_id, search, deviceFilter, sortBy, sortOrder]);

  useEffect(() => {
    generation.current += 1;
    fetching.current = false;
    setTerminals([]);
    setTotalCount(0);
    setPins(new Map());
    setDevices(new Map());
    setIssuingPin(null);
    setActivityPending(null);
    setPresenceError(false);
    setEditingTerminal(null);
    setRetryingId(null);
    setWizardOpen(false);
    pinOperations.current.clear();
    void fetchTerminals();
    const timer = window.setInterval(() => {
      if (document.visibilityState === "visible") void fetchTerminals(true);
    }, 30_000);
    return () => {
      generation.current += 1;
      window.clearInterval(timer);
    };
  }, [fetchTerminals]);

  const handleRetry = async (record: TerminalSettingsItem) => {
    const currentGeneration = generation.current;
    setRetryingId(record.id);
    try {
      await retryTerminalOnboarding(record.id);
      if (currentGeneration !== generation.current) return;
      message.success(`Повторное подключение терминала ${record.device_id ?? ""} запущено`);
      void fetchTerminals();
    } catch (err: any) {
      if (currentGeneration !== generation.current) return;
      message.error(err.response?.data?.detail || "Ошибка повторного provisioning");
    } finally {
      if (currentGeneration === generation.current) setRetryingId(null);
    }
  };

  const handleActivity = async (record: TerminalSettingsItem) => {
    if (activityPending !== null) return;
    const currentGeneration = generation.current;
    setActivityPending(record.id);
    try {
      const updated = await setTerminalActivity(record.id, !record.is_active);
      if (currentGeneration !== generation.current) return;
      setTerminals(previous => previous.map(item => item.id === record.id ? { ...item, is_active: updated.is_active } : item));
      message.success(updated.is_active ? "Терминал включён" : "Терминал отключён");
    } catch (error: any) {
      if (currentGeneration === generation.current) message.error(error?.response?.data?.detail || "Не удалось изменить состояние терминала");
    } finally {
      if (currentGeneration === generation.current) setActivityPending(null);
    }
  };

  const loadPin = async (record: TerminalSettingsItem) => {
    const currentGeneration = generation.current;
    try {
      const pin = await getTerminalPin(record.id);
      if (currentGeneration === generation.current) setPins(previous => new Map(previous).set(record.id, pin));
    } catch {
      if (currentGeneration === generation.current) setPins(previous => new Map(previous).set(record.id, null));
    }
  };

  const filtered = useMemo(() => filterTerminals(terminals, devices, statusFilter), [terminals, devices, statusFilter]);
  const currentPage = Math.min(page, Math.max(1, Math.ceil(filtered.length / pageSize)));
  const visibleRows = filtered.slice((currentPage - 1) * pageSize, currentPage * pageSize);

  const canRetry = (item: TerminalSettingsItem) =>
    item.provisioning_state === "failed" ||
    item.provisioning_state === "pending" ||
    item.pin_state === "failed" ||
    item.pin_state === "pending";

  const handleNewPin = async (record: TerminalSettingsItem) => {
    if (issuingPin !== null) return;
    const currentGeneration = generation.current;
    const storageKey = `l4desk-pin-operation:${user?.org_id}:${record.id}`;
    const operation = pinOperations.current.get(record.id) ?? sessionStorage.getItem(storageKey) ?? crypto.randomUUID();
    sessionStorage.setItem(storageKey, operation);
    pinOperations.current.set(record.id, operation);
    setIssuingPin(record.id);
    try {
      const pin = await renewTerminalPin(record.id, operation);
      sessionStorage.removeItem(storageKey);
      if (currentGeneration !== generation.current) return;
      setPins(previous => new Map(previous).set(record.id, pin));
      setTerminals(previous => previous.map(item => item.id === record.id ? {...item, pin_state: pin.status} : item));
      pinOperations.current.delete(record.id);
      message.success("PIN получен");
      if (sortBy === "last_pin_issued_at") {
        setPage(1);
        void fetchTerminals(true);
      }
    } catch (error: any) {
      if (currentGeneration !== generation.current) return;
      if (error.response?.status === 409) {
        sessionStorage.removeItem(storageKey);
        pinOperations.current.delete(record.id);
        message.error("Запрос заменён. Обновите список перед получением нового PIN.");
        void fetchTerminals(true);
      } else {
        message.error("Не удалось получить PIN. Повторите запрос — операция сохранена.");
      }
    } finally {
      if (currentGeneration === generation.current) setIssuingPin(null);
    }
  };

  const columns: ColumnsType<TerminalSettingsItem> = [
    {
      title: "Терминал",
      dataIndex: "device_id",
      key: "device_id",
      width: 105,
      render: (v: number | null) => (
        <Text strong copyable={v != null ? { text: String(v) } : false} style={{ whiteSpace: "nowrap" }}>
          {v ?? "—"}
        </Text>
      ),
    },
    {
      title: "Название / адрес",
      key: "label",
      width: 310,
      render: (_, r) => (
        <div style={{ minWidth: 0 }}>
          <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
            <Text ellipsis={{ tooltip: r.note || undefined }} style={{ flex: 1, minWidth: 0 }}>{r.note || "—"}</Text>
            {canChangeTerminals && <Tooltip title="Редактировать"><Button type="text" aria-label={`Редактировать ${r.device_id}`} icon={<EditOutlined />} onClick={() => setEditingTerminal(r)} /></Tooltip>}
          </div>
          {r.address && (
            <Text type="secondary" ellipsis={{ tooltip: r.address }} style={{ display: "block", fontSize: 12 }}>
              {r.address}
            </Text>
          )}
        </div>
      ),
    },
    {
      title: "Статус",
      key: "status",
      width: 95,
      render: (_, r) => {
        const status = terminalStatus(r, devices.get(r.device_id));
        const device = devices.get(r.device_id);
        const labels = { online: "Онлайн", offline: "Оффлайн", disabled: "Откл.", unknown: "Неизвестен" };
        return <Tooltip title={device?.is_blocked ? "Соединение заблокировано IoT" : status === "unknown" ? "Данные о связи недоступны" : undefined}><Tag color={status === "online" ? "success" : "default"}>{labels[status]}</Tag></Tooltip>;
      },
    },
    {
      title: "Активация",
      key: "activation",
      width: 145,
      render: (_, r) => {
        const cert = certificatePresentation(r);
        return <Tooltip title={cert.hint}><Tag color={cert.color}>{cert.text}</Tag></Tooltip>;
      },
    },
    {
      title: "Действия",
      key: "actions",
      width: 160,
      render: (_, r) => (
        <Space size={8}>
          <Tooltip title="Консоль"><Button style={{ width: 38, height: 38 }} disabled={!r.is_active || devices.get(r.device_id)?.is_blocked} aria-label={`Консоль ${r.device_id}`} icon={<CodeOutlined style={{ fontSize: 21 }} />} onClick={() => navigate(`/console?device_id=${r.device_id}`)} /></Tooltip>
          <Tooltip title="Видео"><Button style={{ width: 38, height: 38 }} disabled={!r.is_active || devices.get(r.device_id)?.is_blocked} aria-label={`Видео ${r.device_id}`} icon={<VideoCameraOutlined style={{ fontSize: 21 }} />} onClick={() => navigate(`/video?device_id=${r.device_id}`)} /></Tooltip>
          <Popover trigger="click" title={`Подключение №${r.device_id}`} onOpenChange={open => { if (open) void loadPin(r); }} content={
            <Space direction="vertical">
              <Text>IoT: {r.provisioning_state || "pending"}</Text>
              {pinTag({ ...r, pin_state: pins.get(r.id)?.status ?? r.pin_state })}
              {r.last_pin_issued_at && <Text type="secondary">Последний PIN: {new Date(r.last_pin_issued_at).toLocaleString("ru-RU")}</Text>}
              {pins.get(r.id)?.status === "issued" && pins.get(r.id)?.pin && <Text copyable={{ text: pins.get(r.id)!.pin! }}>{pins.get(r.id)!.pin}</Text>}
              {pins.has(r.id) && !pins.get(r.id) && <Text type="secondary">PIN недоступен</Text>}
              {r.last_error && <Text type="danger">{r.last_error}</Text>}
              {canChangeTerminals && <Button icon={<KeyOutlined />} loading={issuingPin === r.id} disabled={issuingPin !== null && issuingPin !== r.id} onClick={() => void handleNewPin(r)}>Новый PIN</Button>}
              {canChangeTerminals && canRetry(r) && <Button icon={<SyncOutlined />} loading={retryingId === r.id} onClick={() => void handleRetry(r)}>Повторить подключение</Button>}
            </Space>
          }><Button type="text" aria-label={`Подключение и PIN ${r.device_id}`} icon={<KeyOutlined />} /></Popover>
        </Space>
      ),
    },
    {
      title: "",
      key: "activity",
      width: 65,
      render: (_, r) => canChangeTerminals && (
        <div style={{ paddingLeft: 12, borderLeft: "1px solid #d9d9d9" }}>
          <Popconfirm title={`${r.is_active ? "Отключить" : "Включить"} терминал №${r.device_id}?`} description={r.is_active ? "Новые сеансы консоли и видео будут недоступны." : undefined} okText={r.is_active ? "Отключить" : "Включить"} cancelText="Отмена" onConfirm={() => handleActivity(r)}>
            <Tooltip title={r.is_active ? "Отключить терминал" : "Включить терминал"}><Button danger={r.is_active} loading={activityPending === r.id} disabled={activityPending !== null} aria-label={`${r.is_active ? "Отключить" : "Включить"} ${r.device_id}`} icon={<PoweroffOutlined />} /></Tooltip>
          </Popconfirm>
        </div>
      ),
    },
    {
      title: "SN", dataIndex: "sn", key: "sn", width: 115,
      render: (sn: string) => <Text type="secondary" copyable={{ text: sn }} ellipsis={{ tooltip: sn }} style={{ display: "block", width: 86, fontFamily: "monospace", fontSize: 11 }}>{sn}</Text>,
    },
  ];

  return (
    <div style={{ width: "100%", minWidth: 0 }}>
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
          <Space align="center">
            <DesktopOutlined style={{ fontSize: 20, color: "#1677ff" }} />
            <Title level={4} style={{ margin: 0, fontSize: 16 }}>
              Терминалы
            </Title>
          </Space>
          <Space wrap>
            <Button icon={<ReloadOutlined spin={loading} />} onClick={() => void fetchTerminals()}>
              Обновить
            </Button>
            {canChangeTerminals && <Button
              type="primary"
              icon={<PlusOutlined />}
              onClick={() => setWizardOpen(true)}
            >
              Подключить терминал
            </Button>}
          </Space>
        </div>
      </Card>

      <Card size="small" style={{ marginBottom: 16 }}>
        <Segmented<TerminalFilter>
          aria-label="Фильтр состояния терминалов"
          value={statusFilter}
          onChange={value => { setStatusFilter(value); setPage(1); }}
          style={{ marginBottom: 12, maxWidth: "100%" }}
          options={[{ value: "online", label: "Онлайн" }, { value: "offline", label: "Оффлайн" }, { value: "disabled", label: "Отключённые" }, { value: "all", label: "Все" }]}
        />
        <div style={{ display: "flex", flexWrap: "wrap", gap: 12, marginBottom: 12 }}>
          <Input.Search
            aria-label="Поиск терминалов"
            placeholder="Поиск по SN, адресу, названию"
            allowClear
            value={searchInput}
            onChange={event => setSearchInput(event.target.value)}
            onSearch={value => { setSearch(value.trim()); setPage(1); }}
            style={{ flex: "1 1 230px", maxWidth: 350 }}
          />
          <Input.Search
            aria-label="Номера терминалов"
            placeholder="773, 1000009 или 1000000-1000010"
            allowClear
            value={deviceInput}
            onChange={event => setDeviceInput(event.target.value)}
            onSearch={value => { setDeviceFilter(value.trim()); setPage(1); }}
            style={{ flex: "1 1 260px", maxWidth: 390 }}
          />
          <Select
            aria-label="Сортировка терминалов"
            value={sortBy}
            onChange={value => { setSortBy(value); setPage(1); }}
            style={{ minWidth: 210, flex: "1 1 210px" }}
            options={[
              { value: "last_pin_issued_at", label: "По последнему PIN" },
              { value: "device_id", label: "По номеру" },
              { value: "created_at", label: "По дате создания" },
              { value: "sn", label: "По SN" },
              { value: "address", label: "По адресу" },
            ]}
          />
          <Select
            aria-label="Порядок сортировки"
            value={sortOrder}
            onChange={value => { setSortOrder(value); setPage(1); }}
            style={{ minWidth: 150 }}
            options={[{ value: "desc", label: "По убыванию" }, { value: "asc", label: "По возрастанию" }]}
          />
        </div>
        <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", flexWrap: "wrap", gap: 8 }}>
          <Text type="secondary">Номера можно перечислить через запятую или указать диапазон через дефис.</Text>
          <Pagination
            size="small"
            current={currentPage}
            pageSize={pageSize}
            total={filtered.length}
            showSizeChanger
            pageSizeOptions={["20", "50", "100"]}
            showTotal={(total, range) => `${range[0]}–${range[1]} из ${total}`}
            onChange={(nextPage, nextSize) => { setPage(nextPage); setPageSize(nextSize); }}
          />
        </div>
      </Card>

      {presenceError && <Alert type="warning" showIcon message="Данные о связи недоступны. Обновите список или выберите «Все»." style={{ marginBottom: 12 }} />}
        <Card styles={{ body: { padding: 12 } }}>
          <Table
            rowKey="id"
            columns={columns}
            dataSource={listTenant === user?.org_id ? visibleRows : []}
            loading={loading}
            pagination={false}
            size="small"
            tableLayout="fixed"
            scroll={{ x: 980 }}
            locale={{ emptyText: <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description={totalCount === 0 ? "Терминалы по запросу не найдены" : statusFilter === "online" ? "Нет терминалов онлайн" : "Нет терминалов по выбранному фильтру"} /> }}
          />
        </Card>

      <TerminalSettingsEditModal
        terminal={editingTerminal}
        readOnly={!canChangeTerminals}
        onClose={() => setEditingTerminal(null)}
        onSaved={() => void fetchTerminals()}
      />

      <OnboardingWizardModal
        key={user?.org_id}
        open={wizardOpen}
        onClose={() => setWizardOpen(false)}
        onTerminalCreated={() => {
          void fetchTerminals();
        }}
        orgId={user?.org_id ?? undefined}
      />
    </div>
  );
}
