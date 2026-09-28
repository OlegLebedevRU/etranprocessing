import { useCallback, useEffect, useRef, useState } from "react";
import {
  Alert,
  Button,
  Card,
  Empty,
  Input,
  Pagination,
  Select,
  Space,
  Table,
  Tag,
  Typography,
  message,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import {
  CheckCircleOutlined,
  ClockCircleOutlined,
  CodeOutlined,
  DesktopOutlined,
  DisconnectOutlined,
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
  type TerminalPin,
  type TerminalSettingsItem,
} from "../../api/settings";
import { useSession } from "../../session/SessionContext";
import OnboardingWizardModal from "../../components/OnboardingWizardModal";
import { getDevices, type DeviceListItem } from "../../api/devices";

const { Text, Title, Paragraph } = Typography;

function readinessTag(device?: DeviceListItem) {
  if (!device?.connection) {
    return <Tag>Неизвестен</Tag>;
  }
  if (device.status === "blocked") return <Tag color="warning">Заблокирован</Tag>;
  if (device.status === "online") {
    return (
      <Tag icon={<CheckCircleOutlined />} color="success">
        Online
      </Tag>
    );
  }
  return (
    <Tag icon={<DisconnectOutlined />} color="default">
      Offline
    </Tag>
  );
}

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
  const [sortBy, setSortBy] = useState("last_pin_issued_at");
  const [sortOrder, setSortOrder] = useState<"asc" | "desc">("desc");
  const [listTenant, setListTenant] = useState<number | null | undefined>(user?.org_id);
  const [wizardOpen, setWizardOpen] = useState(false);
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
        page,
        page_size: pageSize,
      });
      if (currentGeneration !== generation.current) return;
      setListTenant(user?.org_id);
      setTerminals(data.items || []);
      setTotalCount(data.total_count);
      setLoading(false);
      const loadPins = (async () => {
      // Renewal is provider-owned; the onboarding pin_state may still be consumed.
      const pendingPins = data.items;
      const nextPins = new Map<number, TerminalPin | null>();
      // Bound provider concurrency even on a large page.
      for (let offset = 0; offset < pendingPins.length; offset += 5) {
        await Promise.all(pendingPins.slice(offset, offset + 5).map(async item => {
          try { nextPins.set(item.id, await getTerminalPin(item.id)); }
          catch { nextPins.set(item.id, null); }
        }));
      }
      if (currentGeneration !== generation.current) return;
      setPins(nextPins);
      })();
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
        if (currentGeneration === generation.current) setDevices(current);
      } catch {
        if (currentGeneration === generation.current) setDevices(new Map());
      }
      await loadPins;
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
  }, [user?.org_id, search, deviceFilter, sortBy, sortOrder, page, pageSize]);

  useEffect(() => {
    generation.current += 1;
    fetching.current = false;
    setTerminals([]);
    setTotalCount(0);
    setPins(new Map());
    setDevices(new Map());
    setIssuingPin(null);
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
        if (page === 1) void fetchTerminals(true);
        else setPage(1);
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
      width: 135,
      render: (v: number | null) => (
        <Text strong copyable={v != null ? { text: String(v) } : false} style={{ whiteSpace: "nowrap" }}>
          {v ?? "—"}
        </Text>
      ),
    },
    {
      title: "SN",
      dataIndex: "sn",
      key: "sn",
      width: 170,
      render: (sn: string) => (
        <Text
          type="secondary"
          copyable={{ text: sn }}
          ellipsis={{ tooltip: sn }}
          style={{ display: "block", width: 138, whiteSpace: "nowrap", fontFamily: "monospace", fontSize: 12 }}
        >
          {sn}
        </Text>
      ),
    },
    {
      title: "Название / адрес",
      key: "label",
      width: 220,
      render: (_, r) => (
        <div>
          <div>{r.note || "—"}</div>
          {r.address && (
            <Text type="secondary" style={{ fontSize: 12 }}>
              {r.address}
            </Text>
          )}
        </div>
      ),
    },
    {
      title: "Последний PIN",
      dataIndex: "last_pin_issued_at",
      key: "last_pin_issued_at",
      width: 150,
      render: (value: string | null) => value ? new Date(value).toLocaleString("ru-RU") : <Text type="secondary">—</Text>,
    },
    {
      title: "Статус",
      key: "status",
      width: 120,
      render: (_, r) => readinessTag(devices.get(r.device_id)),
    },
    {
      title: "Подключение / PIN",
      key: "prov",
      width: 180,
      render: (_, r) => (
        <Space direction="vertical" size={4}>
          <Tag color={r.provisioning_state === "ready" ? "success" : "default"}>
            IoT: {r.provisioning_state || "pending"}
          </Tag>
          {pinTag({...r, pin_state: pins.get(r.id)?.status ?? r.pin_state})}
          {pins.get(r.id)?.status === "issued" && pins.get(r.id)?.pin && (
            <Text strong copyable={{text: pins.get(r.id)!.pin!}} style={{whiteSpace: "nowrap", fontFamily: "monospace"}}>
              {pins.get(r.id)!.pin}
            </Text>
          )}
          {r.pin_state === "issued" && pins.has(r.id) && !pins.get(r.id) && <Text type="secondary">PIN недоступен</Text>}
        </Space>
      ),
    },
    {
      title: "Действия",
      key: "actions",
      width: 280,
      render: (_, r) => (
        <Space wrap>
          <Button size="small" icon={<KeyOutlined />} loading={issuingPin === r.id} disabled={issuingPin !== null && issuingPin !== r.id} onClick={() => void handleNewPin(r)}>
            Новый PIN
          </Button>
          <Button
            size="small"
            icon={<CodeOutlined />}
            onClick={() => navigate(`/console?device_id=${r.device_id}`)}
          >
            Консоль
          </Button>
          <Button
            size="small"
            icon={<VideoCameraOutlined />}
            onClick={() => navigate(`/video?device_id=${r.device_id}`)}
          >
            Видео
          </Button>
          {canRetry(r) && (
            <Button
              size="small"
              icon={<SyncOutlined spin={retryingId === r.id} />}
              onClick={() => handleRetry(r)}
              loading={retryingId === r.id}
            >
              Повторить
            </Button>
          )}
        </Space>
      ),
    },
  ];

  return (
    <div style={{ maxWidth: 1200, margin: "0 auto" }}>
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
            <Button
              type="primary"
              icon={<PlusOutlined />}
              onClick={() => setWizardOpen(true)}
            >
              Подключить терминал
            </Button>
          </Space>
        </div>
      </Card>

      <Alert
        type="info"
        showIcon
        style={{ marginBottom: 16 }}
        message="Подключение терминалов"
        description="Для поиска и управления используйте номер терминала. Полный SN доступен при наведении и копировании."
      />

      <Card size="small" style={{ marginBottom: 16 }}>
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
            current={page}
            pageSize={pageSize}
            total={totalCount}
            showSizeChanger
            pageSizeOptions={["20", "50", "100"]}
            showTotal={(total, range) => `${range[0]}–${range[1]} из ${total}`}
            onChange={(nextPage, nextSize) => { setPage(nextPage); setPageSize(nextSize); }}
          />
        </div>
      </Card>

      {!loading && totalCount === 0 && !search && !deviceFilter && (
        <Card style={{ textAlign: "center", padding: "48px 24px" }}>
          <Empty
            description={
              <div>
                <Title level={5}>Терминалов пока нет</Title>
                <Paragraph type="secondary" style={{ maxWidth: 460, margin: "0 auto 16px" }}>
                  Создайте первый терминал через мастер подключения.
                </Paragraph>
              </div>
            }
          >
            <Button
              type="primary"
              size="large"
              icon={<PlusOutlined />}
              onClick={() => setWizardOpen(true)}
            >
              Подключить терминал
            </Button>
          </Empty>
        </Card>
      )}

      {(totalCount > 0 || loading || Boolean(search) || Boolean(deviceFilter)) && (
        <Card>
          <Table
            rowKey="id"
            columns={columns}
            dataSource={listTenant === user?.org_id ? terminals : []}
            loading={loading}
            pagination={false}
            scroll={{ x: 1255 }}
            locale={{ emptyText: "Терминалы по запросу не найдены" }}
          />
        </Card>
      )}

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
