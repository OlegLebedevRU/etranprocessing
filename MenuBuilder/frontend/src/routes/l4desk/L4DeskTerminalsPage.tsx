import { useCallback, useEffect, useRef, useState } from "react";
import {
  Alert,
  Button,
  Card,
  Modal,
  Dropdown,
  Grid,
  Empty,
  Tooltip,
  Input,
  Pagination,

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
  CodeOutlined,
  DesktopOutlined,
  EditOutlined,
  FolderOutlined,
  InfoCircleOutlined,
  MoreOutlined,
  PoweroffOutlined,
  PlusOutlined,
  ReloadOutlined,
  SafetyCertificateOutlined,
  SyncOutlined,
  VideoCameraOutlined,
} from "@ant-design/icons";
import { useNavigate } from "react-router";
import {
  listTerminalsSettings,
  retryTerminalOnboarding,
  setL4DeskActivity,
  type TerminalSettingsItem,
} from "../../api/settings";
import { useSession } from "../../session/SessionContext";
import TerminalCertificateDrawer from "../../components/TerminalCertificateDrawer";
import OnboardingWizardModal from "../../components/OnboardingWizardModal";
import TerminalSummary from "../../components/TerminalSummary";
import TerminalSettingsEditModal from "../../components/TerminalSettingsEditModal";
import { certificatePresentation, terminalStatus, type TerminalFilter } from "../../utils/terminalPresentation";

import { subscriptionLabels } from "../../utils/subscriptionPresentation";

const { Text, Title } = Typography;

export default function L4DeskTerminalsPage() {
  const { user } = useSession();
  const isSuperuser = !!user?.is_superuser || user?.role_id === 1;
  const canChangeTerminals = isSuperuser || user?.role_id === 3 || user?.role_id === 5;
  const canChangeActivity = !isSuperuser && (user?.role_id === 3 || user?.role_id === 5);
  const canRenewCertificate = !!user?.is_superuser || [1, 3, 5].includes(user?.role_id ?? 0);
  const navigate = useNavigate();
  const [loading, setLoading] = useState(true);
  const [canCreate, setCanCreate] = useState(false);
  const screens = Grid.useBreakpoint();
  const [summary, setSummary] = useState<TerminalSettingsItem | null>(null);
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
  const [renewalTerminal, setRenewalTerminal] = useState<TerminalSettingsItem | null>(null);
  const [retryingId, setRetryingId] = useState<number | null>(null);
  const fetching = useRef(false);
  const generation = useRef(0);
  const tenantGeneration = useRef(0);

  const abort = useRef<AbortController | null>(null);
  const fetchTerminals = useCallback(async (silent = false) => {
    if (fetching.current) return null;
    const currentGeneration = generation.current;
    fetching.current = true;
    const controller = new AbortController(); abort.current = controller;
    if (!silent) setLoading(true);
    try {
      const data = await listTerminalsSettings({ org_id: user?.org_id || undefined,
        l4desk: true, search: search || undefined, device_filter: deviceFilter || undefined,
        sort_by: sortBy, sort_order: sortOrder, status_filter: statusFilter, page, page_size: pageSize }, controller.signal);
      if (currentGeneration !== generation.current) return false;
      setListTenant(user?.org_id); setTerminals(data.items || []); setTotalCount(data.total_count);
      setCanCreate(data.can_create === true); setPresenceError(data.presence_unavailable === true);
      const lastPage = Math.max(1, Math.ceil(data.total_count / pageSize));
      if (page > lastPage) setPage(lastPage);
      return !data.presence_unavailable;
    } catch (error: any) {
      if (currentGeneration === generation.current && !controller.signal.aborted) {
        setPresenceError(true);
        if (!silent) message.error(typeof error?.response?.data?.detail === "string" ? error.response.data.detail : "Не удалось загрузить список терминалов");
      }
      return false;
    } finally {
      if (currentGeneration === generation.current) { setLoading(false); fetching.current = false; }
    }
  }, [user?.org_id, search, deviceFilter, sortBy, sortOrder, statusFilter, page, pageSize]);

  useEffect(() => {
    generation.current++; fetching.current = false; abort.current?.abort();
    setTerminals([]); setTotalCount(0); setCanCreate(false);
    let disposed = false; let timer: number | undefined; let failures = 0;
    const run = async (silent = true) => {
      if (document.visibilityState !== "visible") return;
      const ok = await fetchTerminals(silent);
      if (disposed || ok === null) return;
      failures = ok ? 0 : Math.min(3, failures + 1);
      timer = window.setTimeout(run, 30_000 * 2 ** failures + Math.random() * 3000);
    };
    const visible = () => { if (document.visibilityState === "visible") { window.clearTimeout(timer); void run(); } };
    void run(false); document.addEventListener("visibilitychange", visible);
    return () => { disposed = true; generation.current++; abort.current?.abort(); window.clearTimeout(timer); document.removeEventListener("visibilitychange", visible); };
  }, [fetchTerminals]);
  const refreshCurrent = useRef(fetchTerminals); refreshCurrent.current = fetchTerminals;
  useEffect(() => {
    tenantGeneration.current++;
    setSummary(null); setEditingTerminal(null); setRenewalTerminal(null); setWizardOpen(false);
    setActivityPending(null); setRetryingId(null);
    return () => { tenantGeneration.current++; };
  }, [user?.org_id]);

  const handleRetry = async (record: TerminalSettingsItem) => {
    const currentGeneration = tenantGeneration.current;
    setRetryingId(record.id);
    try {
      await retryTerminalOnboarding(record.id);
      if (currentGeneration !== tenantGeneration.current) return;
      message.success(`Повторное подключение терминала ${record.device_id ?? ""} запущено`);
      void refreshCurrent.current(true);
    } catch (err: any) {
      if (currentGeneration !== tenantGeneration.current) return;
      message.error(err.response?.data?.detail || "Ошибка повторного provisioning");
    } finally {
      if (currentGeneration === tenantGeneration.current) setRetryingId(null);
    }
  };

  const handleActivity = async (record: TerminalSettingsItem) => {
    if (activityPending !== null || !record.is_active || !canChangeActivity) return;
    const currentGeneration = tenantGeneration.current;
    setActivityPending(record.id);
    try {
      const updated = await setL4DeskActivity(record.id, !!record.user_disabled);
      if (currentGeneration !== tenantGeneration.current) return;
      setTerminals(previous => previous.map(item => item.id === record.id ? { ...item, user_disabled: updated.user_disabled } : item));
      message.success(updated.user_disabled ? "Будущие использования терминала отключены" : "Терминал включён");
      void refreshCurrent.current(true);
    } catch (error: any) {
      if (currentGeneration === tenantGeneration.current) message.error(error?.response?.data?.detail || "Не удалось изменить состояние терминала");
    } finally {
      if (currentGeneration === tenantGeneration.current) setActivityPending(null);
    }
  };

  const currentPage = page;
  const visibleRows = terminals;

  const canRetry = (item: TerminalSettingsItem) =>
    item.provisioning_state === "failed" ||
    item.provisioning_state === "pending" ||
    item.pin_state === "failed" ||
    item.pin_state === "pending";

  const identity = (r: TerminalSettingsItem) => <div style={{ minWidth: 0, overflowWrap: "anywhere" }}>
    <Text strong copyable={{ text: String(r.device_id) }}>{r.device_id}</Text>
    <div><Text ellipsis={{ tooltip: r.note || undefined }}>{r.note || "—"}</Text></div>
    {r.address && <Text type="secondary" ellipsis={{ tooltip: r.address }}>{r.address}</Text>}
    <div><Text type="secondary" copyable={{ text: r.sn }} style={{ fontSize: 11 }}>{r.sn}</Text></div>
  </div>;
  const status = (r: TerminalSettingsItem) => {
    const value = terminalStatus(r);
    const cert = certificatePresentation(r);
    return <Space orientation="vertical" size={2}>
      {!r.is_active && <Tooltip title="Доступ отключён администратором. Пользователь не может изменить это ограничение."><Tag color="error">Отключено администратором</Tag></Tooltip>}
      {r.user_disabled && <Tooltip title="Запрещены будущие использования в L4Desk. Оплаченный срок сохранён."><Tag color="warning">Отключено пользователем</Tag></Tooltip>}
      {r.iot_blocked && <Tooltip title="Соединение заблокировано сервисом IoT"><Tag color="error">Заблокировано IoT</Tag></Tooltip>}
      {r.is_active && !r.user_disabled && !r.iot_blocked && <Tooltip title={value === "unknown" ? "Сведения о связи этого устройства не подтверждены" : undefined}>
        <Tag color={value === "online" ? "success" : "default"}>{({online: "Онлайн", offline: "Оффлайн", disabled: "Отключено", unknown: "Связь неизвестна"})[value]}</Tag>
      </Tooltip>}
      <Tooltip title={cert.hint}><Tag color={cert.color}>{cert.text}</Tag></Tooltip>
    </Space>;
  };
  const activation = (r: TerminalSettingsItem) => <Space orientation="vertical" size={2}>
    {r.subscription ? <Tooltip title={r.subscription.reason}><Button type="link" size="small" style={{ padding: 0 }} onClick={() => navigate("/licenses")}>{subscriptionLabels[r.subscription.state]}</Button></Tooltip> : <Text type="secondary">Подписка неизвестна</Text>}
    <Tooltip title={r.inventory_published_at ? `${Date.now() - Date.parse(r.inventory_published_at) > 86400_000 ? "Сведения старше суток. " : ""}Опубликовано терминалом: ${new Date(r.inventory_published_at).toLocaleString("ru-RU")}. Последняя подтверждённая версия.` : "Установленная версия ещё не подтверждена"}>
      <Text type="secondary">L4 Tools: {r.suite_version || "—"}</Text>
    </Tooltip>
  </Space>;
  const actions = (r: TerminalSettingsItem) => {
    const disabled = !r.is_active || !!r.user_disabled || !!r.iot_blocked || r.subscription?.allowed !== true;
    return <Space size={4} wrap>
      <Tooltip title="Консоль"><Button disabled={disabled} aria-label={`Консоль ${r.device_id}`} icon={<CodeOutlined />} onClick={() => navigate(`/console?device_id=${r.device_id}`)} /></Tooltip>
      <Tooltip title="Экран терминала"><Button disabled={disabled} aria-label={`Экран терминала ${r.device_id}`} icon={<VideoCameraOutlined />} onClick={() => navigate(`/video?device_id=${r.device_id}`)} /></Tooltip>
      <Tooltip title="Файловый менеджер"><Button disabled={disabled} aria-label={`Файловый менеджер ${r.device_id}`} icon={<FolderOutlined />} onClick={() => navigate(`/files?device_id=${r.device_id}`)} /></Tooltip>
      <Tooltip title="Сводка"><Button aria-label={`Сводка ${r.device_id}`} icon={<InfoCircleOutlined />} onClick={() => setSummary(r)} /></Tooltip>
      <Dropdown trigger={["click"]} menu={{ items: [
        ...(canChangeTerminals ? [{ key: "edit", label: "Редактировать", icon: <EditOutlined />, onClick: () => setEditingTerminal(r) }] : []),
        ...(canRenewCertificate || canChangeTerminals ? [{ key: "certificate", label: "Сертификат и PIN", icon: <SafetyCertificateOutlined />, onClick: () => setRenewalTerminal(r) }] : []),
        ...(canChangeActivity && canRetry(r) ? [{ key: "retry", label: "Повторить подключение", icon: <SyncOutlined />, disabled: retryingId !== null, onClick: () => void handleRetry(r) }] : []),
        ...(canChangeActivity ? [{ type: "divider" as const }, { key: "activity", label: r.user_disabled ? "Включить терминал" : "Отключить терминал", icon: <PoweroffOutlined />, danger: !r.user_disabled, disabled: activityPending !== null || !r.is_active,
          onClick: () => { Modal.confirm({ title: `${r.user_disabled ? "Включить" : "Отключить"} терминал ${r.device_id}?`, content: r.user_disabled ? "Будущие использования снова будут разрешены при действующей подписке." : "Будущие использования, включая MQTT, RTP и файловый менеджер, будут запрещены после применения политики. Текущие соединения завершаются естественно. Оплаченный срок сохраняется.", okText: r.user_disabled ? "Включить" : "Отключить", cancelText: "Отмена", onOk: () => handleActivity(r) }); } }] : []),
      ] }}><Button aria-label={`Другие действия ${r.device_id}`} icon={<MoreOutlined />} /></Dropdown>
    </Space>;
  };
  const columns: ColumnsType<TerminalSettingsItem> = [
    { title: "Терминал / адрес", key: "terminal", render: (_, r) => identity(r) },
    { title: "Доступ и связь", key: "status", width: 210, render: (_, r) => status(r) },
    { title: "Подписка / версия", key: "activation", width: 170, render: (_, r) => activation(r) },
    { title: "Действия", key: "actions", width: 190, render: (_, r) => actions(r) },
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
              Устройства
            </Title>
          </Space>
          <Space wrap>
            <Button icon={<ReloadOutlined spin={loading} />} onClick={() => void fetchTerminals()}>
              Обновить
            </Button>
            {canChangeActivity && <Button
              type="primary"
              icon={<PlusOutlined />}
              disabled={!canCreate}
              onClick={() => setWizardOpen(true)}
            >
              Подключить терминал
            </Button>}
          </Space>
        </div>
      </Card>
      {!canCreate && !loading && <Alert style={{marginBottom: 16}} type="info" title="Созданы три терминала" description="Для добавления следующих нужна активная подписка дополнительного терминала." action={<Button onClick={() => navigate("/licenses")}>Открыть подписки</Button>} />}

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
            total={totalCount}
            showSizeChanger
            pageSizeOptions={["20", "50", "100"]}
            showTotal={(total, range) => `${range[0]}–${range[1]} из ${total}`}
            onChange={(nextPage, nextSize) => { setPage(nextPage); setPageSize(nextSize); }}
          />
        </div>
      </Card>

      {presenceError && <Alert type="warning" showIcon message="Данные о связи недоступны. Обновите список или выберите «Все»." style={{ marginBottom: 12 }} />}
        <Card styles={{ body: { padding: 12 } }}>
          {screens.xl ? <Table
            rowKey="id"
            columns={columns}
            dataSource={listTenant === user?.org_id ? visibleRows : []}
            loading={loading}
            pagination={false}
            size="small"
            tableLayout="fixed"
            locale={{ emptyText: <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description={totalCount === 0 ? "Терминалы по запросу не найдены" : statusFilter === "online" ? "Нет терминалов онлайн" : "Нет терминалов по выбранному фильтру"} /> }}
          /> : <Space orientation="vertical" style={{ width: "100%" }}>
            {listTenant === user?.org_id && visibleRows.map(r => <Card key={r.id} size="small">
              <div style={{ display: "flex", flexWrap: "wrap", gap: 16, justifyContent: "space-between" }}>{identity(r)}{status(r)}{activation(r)}</div>
              <div style={{ marginTop: 12 }}>{actions(r)}</div>
            </Card>)}
            {!loading && !visibleRows.length && <Empty description="Терминалы по запросу не найдены" />}
          </Space>}
        </Card>

      <TerminalSettingsEditModal
        terminal={editingTerminal}
        readOnly={!canChangeTerminals}
        onClose={() => setEditingTerminal(null)}
        onSaved={() => void refreshCurrent.current(true)}
      />

      {summary && listTenant === user?.org_id && user?.org_id && <TerminalSummary key={`${user.org_id}:${summary.id}`} terminal={summary} orgId={user.org_id} canEdit={canChangeTerminals} onClose={() => setSummary(null)} />}
      {renewalTerminal && listTenant === user?.org_id && user?.org_id && <TerminalCertificateDrawer
        key={`${user.org_id}:${renewalTerminal.id}`} terminal={renewalTerminal} orgId={user.org_id}
        canRenew={canRenewCertificate} canIssuePin={canChangeTerminals}
        onClose={() => setRenewalTerminal(null)} onPinIssued={() => void refreshCurrent.current(true)} />}


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
