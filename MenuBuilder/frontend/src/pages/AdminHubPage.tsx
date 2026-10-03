import React, { useEffect, useState } from "react";
import {
  AuditOutlined,
  CheckCircleOutlined,
  CloseCircleOutlined,
  CloudServerOutlined,
  DollarOutlined,
  ExclamationCircleOutlined,
  FileTextOutlined,
  FilterOutlined,
  LinkOutlined,
  SyncOutlined,
  UploadOutlined,
} from "@ant-design/icons";
import {
  Alert,
  Badge,
  Button,
  Card,
  Col,
  Input,
  InputNumber,
  Modal,
  Row,
  Select,
  Space,
  Statistic,
  Switch,
  Table,
  Tabs,
  Tag,
  Typography,
  message,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import dayjs from "dayjs";

import {
  CorrelationDrilldownResponse,
  HubArchiveBatchItem,
  HubAuditEventItem,
  HubNotificationItem,
  HubPaymentItem,
  HubRegistrationItem,
  HubSessionItem,
  HubTenantFinanceItem,
  HubTerminalItem,
  HubUsageItem,
  fetchHubArchives,
  fetchHubAuditEvents,
  fetchHubCorrelationDrilldown,
  fetchHubFinanceOverview,
  fetchHubNotifications,
  fetchHubPayments,
  fetchHubRegistrations,
  fetchHubSessions,
  fetchHubTerminals,
  fetchHubUsage,
  importHubArchiveManifest,
} from "../api/hub";

import AdminSubscriptionsPanel from "../components/AdminSubscriptionsPanel";

const { Title, Text } = Typography;

export default function AdminHubPage() {
  const [activeTab, setActiveTab] = useState<string>("registrations");

  // Tab 1: Registrations
  const [registrations, setRegistrations] = useState<HubRegistrationItem[]>([]);
  const [regTotal, setRegTotal] = useState(0);
  const [regLoading, setRegLoading] = useState(false);
  const [regPage, setRegPage] = useState(1);
  const [regFilterStatus, setRegFilterStatus] = useState<string | undefined>();
  const [regFilterEmail, setRegFilterEmail] = useState<string>("");
  const [regOnlyErrors, setRegOnlyErrors] = useState(false);

  // Tab 2: Terminals
  const [terminals, setTerminals] = useState<HubTerminalItem[]>([]);
  const [termTotal, setTermTotal] = useState(0);
  const [termLoading, setTermLoading] = useState(false);
  const [termPage, setTermPage] = useState(1);
  const [termFilterSn, setTermFilterSn] = useState("");
  const [termFilterProv, setTermFilterProv] = useState<string | undefined>();
  const [termFilterFree, setTermFilterFree] = useState<boolean | undefined>();
  const [termOnlyErrors, setTermOnlyErrors] = useState(false);

  // Tab 3: Sessions & Usage
  const [subSessionTab, setSubSessionTab] = useState<string>("sessions");
  const [sessions, setSessions] = useState<HubSessionItem[]>([]);
  const [sessionTotal, setSessionTotal] = useState(0);
  const [sessionLoading, setSessionLoading] = useState(false);
  const [sessionPage, setSessionPage] = useState(1);
  const [sessionOnlyErrors, setSessionOnlyErrors] = useState(false);

  const [usageList, setUsageList] = useState<HubUsageItem[]>([]);
  const [usageTotal, setUsageTotal] = useState(0);
  const [usageLoading, setUsageLoading] = useState(false);
  const [usagePage, setUsagePage] = useState(1);
  const [usageOnlyUnreconciled, setUsageOnlyUnreconciled] = useState(false);

  // Tab 4: Finance & Payments
  const [subFinanceTab, setSubFinanceTab] = useState<string>("overview");
  const [financeOverview, setFinanceOverview] = useState<{
    total_tenants: number;
    active_tenants: number;
    grace_tenants: number;
    blocked_tenants: number;
    total_balance_rubles: number;
    tenants: HubTenantFinanceItem[];
    total: number;
  }>({
    total_tenants: 0,
    active_tenants: 0,
    grace_tenants: 0,
    blocked_tenants: 0,
    total_balance_rubles: 0,
    tenants: [],
    total: 0,
  });
  const [financeLoading, setFinanceLoading] = useState(false);
  const [financePage, setFinancePage] = useState(1);
  const [financeEntitlementFilter, setFinanceEntitlementFilter] = useState<string | undefined>();

  const [payments, setPayments] = useState<HubPaymentItem[]>([]);
  const [payTotal, setPayTotal] = useState(0);
  const [payLoading, setPayLoading] = useState(false);
  const [payPage, setPayPage] = useState(1);
  const [paySourceFilter, setPaySourceFilter] = useState<string | undefined>();

  // Tab 5: Notifications & Audits
  const [subNotifTab, setSubNotifTab] = useState<string>("notifications");
  const [notifications, setNotifications] = useState<HubNotificationItem[]>([]);
  const [notifTotal, setNotifTotal] = useState(0);
  const [notifLoading, setNotifLoading] = useState(false);
  const [notifPage, setNotifPage] = useState(1);
  const [notifOnlyErrors, setNotifOnlyErrors] = useState(false);

  const [auditEvents, setAuditEvents] = useState<HubAuditEventItem[]>([]);
  const [auditTotal, setAuditTotal] = useState(0);
  const [auditLoading, setAuditLoading] = useState(false);
  const [auditPage, setAuditPage] = useState(1);
  const [auditOnlyErrors, setAuditOnlyErrors] = useState(false);

  // Tab 6: Archives & Retention
  const [archives, setArchives] = useState<HubArchiveBatchItem[]>([]);
  const [archTotal, setArchTotal] = useState(0);
  const [archLoading, setArchLoading] = useState(false);
  const [archPage, setArchPage] = useState(1);
  const [archFilterOwner, setArchFilterOwner] = useState<string | undefined>();
  const [archFilterState, setArchFilterState] = useState<string | undefined>();
  const [archFilterMonth, setArchFilterMonth] = useState<string>("");
  const [archOnlyErrors, setArchOnlyErrors] = useState(false);
  const [selectedArchive, setSelectedArchive] = useState<HubArchiveBatchItem | null>(null);
  const [archDetailModalOpen, setArchDetailModalOpen] = useState(false);
  const [archImportModalOpen, setArchImportModalOpen] = useState(false);
  const [archImportJson, setArchImportJson] = useState("");
  const [archImportLoading, setArchImportLoading] = useState(false);

  // Modals state
  const [drilldownModalOpen, setDrilldownModalOpen] = useState(false);
  const [drilldownLoading, setDrilldownLoading] = useState(false);
  const [drilldownData, setDrilldownData] = useState<CorrelationDrilldownResponse | null>(null);
  const [drilldownCorrelationInput, setDrilldownCorrelationInput] = useState("");
  const [drilldownTenantIdInput, setDrilldownTenantIdInput] = useState<number | undefined>();
  const [drilldownTerminalIdInput, setDrilldownTerminalIdInput] = useState<number | undefined>();
  const [drilldownArchiveBatchInput, setDrilldownArchiveBatchInput] = useState("");

  // Loaders
  const loadRegistrations = async () => {
    setRegLoading(true);
    try {
      const res = await fetchHubRegistrations({
        status: regFilterStatus,
        email: regFilterEmail || undefined,
        only_errors: regOnlyErrors,
        page: regPage,
        page_size: 20,
      });
      setRegistrations(res.items);
      setRegTotal(res.total);
    } catch (err: unknown) {
      message.error("Ошибка загрузки регистраций");
    } finally {
      setRegLoading(false);
    }
  };

  const loadTerminals = async () => {
    setTermLoading(true);
    try {
      const res = await fetchHubTerminals({
        sn: termFilterSn || undefined,
        provisioning_state: termFilterProv,
        is_free: termFilterFree,
        only_errors: termOnlyErrors,
        page: termPage,
        page_size: 20,
      });
      setTerminals(res.items);
      setTermTotal(res.total);
    } catch (err: unknown) {
      message.error("Ошибка загрузки терминалов");
    } finally {
      setTermLoading(false);
    }
  };

  const loadSessions = async () => {
    setSessionLoading(true);
    try {
      const res = await fetchHubSessions({
        only_errors: sessionOnlyErrors,
        page: sessionPage,
        page_size: 20,
      });
      setSessions(res.items);
      setSessionTotal(res.total);
    } catch (err: unknown) {
      message.error("Ошибка загрузки сессий");
    } finally {
      setSessionLoading(false);
    }
  };

  const loadUsage = async () => {
    setUsageLoading(true);
    try {
      const res = await fetchHubUsage({
        only_unreconciled: usageOnlyUnreconciled,
        page: usagePage,
        page_size: 20,
      });
      setUsageList(res.items);
      setUsageTotal(res.total);
    } catch (err: unknown) {
      message.error("Ошибка загрузки потребления");
    } finally {
      setUsageLoading(false);
    }
  };

  const loadFinanceOverview = async () => {
    setFinanceLoading(true);
    try {
      const res = await fetchHubFinanceOverview({
        active_grace_blocked: financeEntitlementFilter,
        page: financePage,
        page_size: 20,
      });
      setFinanceOverview(res);
    } catch (err: unknown) {
      message.error("Ошибка загрузки финансового обзора");
    } finally {
      setFinanceLoading(false);
    }
  };

  const loadPayments = async () => {
    setPayLoading(true);
    try {
      const res = await fetchHubPayments({
        payment_source: paySourceFilter,
        page: payPage,
        page_size: 20,
      });
      setPayments(res.items);
      setPayTotal(res.total);
    } catch (err: unknown) {
      message.error("Ошибка загрузки платежей");
    } finally {
      setPayLoading(false);
    }
  };

  const loadNotifications = async () => {
    setNotifLoading(true);
    try {
      const res = await fetchHubNotifications({
        only_errors: notifOnlyErrors,
        page: notifPage,
        page_size: 20,
      });
      setNotifications(res.items);
      setNotifTotal(res.total);
    } catch (err: unknown) {
      message.error("Ошибка загрузки уведомлений");
    } finally {
      setNotifLoading(false);
    }
  };

  const loadAuditEvents = async () => {
    setAuditLoading(true);
    try {
      const res = await fetchHubAuditEvents({
        only_errors: auditOnlyErrors,
        page: auditPage,
        page_size: 20,
      });
      setAuditEvents(res.items);
      setAuditTotal(res.total);
    } catch (err: unknown) {
      message.error("Ошибка загрузки аудита");
    } finally {
      setAuditLoading(false);
    }
  };

  const loadArchives = async () => {
    setArchLoading(true);
    try {
      const res = await fetchHubArchives({
        owner_project: archFilterOwner || undefined,
        state: archFilterState || undefined,
        source_month: archFilterMonth || undefined,
        only_errors: archOnlyErrors,
        page: archPage,
        page_size: 20,
      });
      setArchives(res.items);
      setArchTotal(res.total);
    } catch (err: unknown) {
      message.error("Ошибка загрузки архивов");
    } finally {
      setArchLoading(false);
    }
  };

  const handleImportManifest = async () => {
    if (!archImportJson.trim()) return;
    setArchImportLoading(true);
    try {
      const parsed = JSON.parse(archImportJson);
      await importHubArchiveManifest({ manifest: parsed });
      message.success("Манифест успешно импортирован");
      setArchImportModalOpen(false);
      setArchImportJson("");
      loadArchives();
    } catch (err: any) {
      message.error(err?.response?.data?.detail || "Ошибка валидации или импорта манифеста");
    } finally {
      setArchImportLoading(false);
    }
  };

  // Trigger loads on tab changes
  useEffect(() => {
    if (activeTab === "registrations") {
      loadRegistrations();
    } else if (activeTab === "terminals") {
      loadTerminals();
    } else if (activeTab === "sessions") {
      if (subSessionTab === "sessions") loadSessions();
      else loadUsage();
    } else if (activeTab === "finance") {
      if (subFinanceTab === "overview") loadFinanceOverview();
      else loadPayments();
    } else if (activeTab === "notifications") {
      if (subNotifTab === "notifications") loadNotifications();
      else loadAuditEvents();
    } else if (activeTab === "archives") {
      loadArchives();
    }
  }, [
    activeTab,
    subSessionTab,
    subFinanceTab,
    subNotifTab,
    regPage,
    regFilterStatus,
    regOnlyErrors,
    termPage,
    termFilterProv,
    termFilterFree,
    termOnlyErrors,
    sessionPage,
    sessionOnlyErrors,
    usagePage,
    usageOnlyUnreconciled,
    financePage,
    financeEntitlementFilter,
    payPage,
    paySourceFilter,
    notifPage,
    notifOnlyErrors,
    auditPage,
    auditOnlyErrors,
    archPage,
    archFilterOwner,
    archFilterState,
    archOnlyErrors,
  ]);

  // Drilldown handler
  const openDrilldown = async (params: {
    correlation_id?: string;
    tenant_id?: number;
    terminal_id?: number;
    session_id?: number;
    payment_id?: number;
    archive_batch_id?: string;
  }) => {
    setDrilldownModalOpen(true);
    setDrilldownLoading(true);
    try {
      const res = await fetchHubCorrelationDrilldown(params);
      setDrilldownData(res);
      if (params.correlation_id) setDrilldownCorrelationInput(params.correlation_id);
      if (params.tenant_id) setDrilldownTenantIdInput(params.tenant_id);
      if (params.terminal_id) setDrilldownTerminalIdInput(params.terminal_id);
      if (params.archive_batch_id) setDrilldownArchiveBatchInput(params.archive_batch_id);
    } catch (err: unknown) {
      message.error("Не удалось построить цепочку сверки");
    } finally {
      setDrilldownLoading(false);
    }
  };


  // Table Columns
  const regColumns: ColumnsType<HubRegistrationItem> = [
    { title: "ID", dataIndex: "id", width: 70 },
    {
      title: "Организация (Tenant)",
      dataIndex: "tenant_id",
      render: (tId: number | null, r) => (
        <span>
          {r.tenant_name ? `${r.tenant_name} (#${tId})` : tId ? `#${tId}` : "Не активирован"}
        </span>
      ),
    },
    { title: "Email", dataIndex: "email_normalized" },
    {
      title: "Статус",
      dataIndex: "status",
      render: (st: string) => {
        if (st === "consumed") return <Tag color="green">Активирован</Tag>;
        if (st === "expired") return <Tag color="red">Истёк</Tag>;
        return <Tag color="orange">Ожидает</Tag>;
      },
    },
    {
      title: "1-я оплата",
      dataIndex: "is_first_paid",
      render: (paid: boolean) => (paid ? <Tag color="blue">Да</Tag> : <Tag>Нет</Tag>),
    },
    {
      title: "Создан",
      dataIndex: "created_at",
      render: (d: string) => dayjs(d).format("YYYY-MM-DD HH:mm"),
    },
    {
      title: "Correlation ID",
      dataIndex: "correlation_id",
      ellipsis: true,
    },
    {
      title: "Действия",
      render: (_, r) => (
        <Button
          size="small"
          icon={<LinkOutlined />}
          onClick={() =>
            openDrilldown({
              correlation_id: r.correlation_id,
              tenant_id: r.tenant_id || undefined,
            })
          }
        >
          Drill-down
        </Button>
      ),
    },
  ];

  const termColumns: ColumnsType<HubTerminalItem> = [
    { title: "ID", dataIndex: "terminal_id", width: 80 },
    {
      title: "Организация",
      dataIndex: "tenant_id",
      render: (tId: number, r) => r.tenant_name || `#${tId}`,
    },
    { title: "Серийный номер (SN)", dataIndex: "sn" },
    {
      title: "Тип",
      dataIndex: "is_free",
      render: (isFree: boolean) => (isFree ? <Tag color="purple">1-й (Бесплатный)</Tag> : <Tag color="blue">Платный</Tag>),
    },
    {
      title: "Подготовка",
      dataIndex: "provisioning_state",
      render: (st: string) => {
        if (st === "ready") return <Tag color="green">Ready</Tag>;
        if (st === "failed") return <Tag color="red">Failed</Tag>;
        return <Tag color="orange">{st}</Tag>;
      },
    },
    {
      title: "PIN / Сертификат",
      dataIndex: "pin_state",
      render: (st: string, r) => {
        const isOk = st === "consumed" || st === "issued";
        return (
          <Space orientation="vertical" size={2}>
            <Tag color={isOk ? "green" : st === "failed" ? "red" : "orange"}>{st}</Tag>
            {r.certificate_reference && <Text type="secondary" style={{ fontSize: 11 }}>{r.certificate_reference}</Text>}
          </Space>
        );
      },
    },
    {
      title: "Статус сети",
      dataIndex: "is_online",
      render: (onl: boolean, r) => (
        <Badge
          status={onl ? "success" : "default"}
          text={onl ? "Online" : r.last_online_at ? dayjs(r.last_online_at).format("DD.MM HH:mm") : "Offline"}
        />
      ),
    },
    {
      title: "Действия",
      render: (_, r) => (
        <Button
          size="small"
          icon={<LinkOutlined />}
          onClick={() =>
            openDrilldown({
              terminal_id: r.terminal_id,
              tenant_id: r.tenant_id,
              correlation_id: r.correlation_id,
            })
          }
        >
          Drill-down
        </Button>
      ),
    },
  ];

  const sessColumns: ColumnsType<HubSessionItem> = [
    { title: "ID", dataIndex: "id", width: 70 },
    { title: "Терминал", dataIndex: "terminal_sn", render: (sn: string, r) => sn || `#${r.terminal_id}` },
    {
      title: "Тип",
      dataIndex: "session_type",
      render: (t: string) => <Tag color={t === "video" ? "volcano" : "geekblue"}>{t.toUpperCase()}</Tag>,
    },
    {
      title: "Состояние",
      dataIndex: "state",
      render: (st: string) => {
        if (st === "active") return <Tag color="processing">Active</Tag>;
        if (st === "closed") return <Tag color="success">Closed</Tag>;
        if (st === "failed") return <Tag color="error">Failed</Tag>;
        return <Tag>{st}</Tag>;
      },
    },
    {
      title: "Длительность",
      dataIndex: "duration_seconds",
      render: (s: number) => `${Math.floor(s / 60)} мин ${s % 60} сек`,
    },
    {
      title: "Время",
      dataIndex: "requested_at",
      render: (d: string) => dayjs(d).format("YYYY-MM-DD HH:mm:ss"),
    },
    {
      title: "Действия",
      render: (_, r) => (
        <Button
          size="small"
          icon={<LinkOutlined />}
          onClick={() =>
            openDrilldown({
              session_id: r.id,
              terminal_id: r.terminal_id,
              tenant_id: r.tenant_id,
              correlation_id: r.correlation_id,
            })
          }
        >
          Drill-down
        </Button>
      ),
    },
  ];

  const usageColumns: ColumnsType<HubUsageItem> = [
    { title: "Дата", dataIndex: "local_date" },
    { title: "Терминал", dataIndex: "terminal_sn", render: (sn: string, r) => sn || `#${r.terminal_id}` },
    { title: "Видео (сек)", dataIndex: "video_seconds" },
    { title: "Консоль (сек)", dataIndex: "console_seconds" },
    { title: "Льготные (сек)", dataIndex: "free_seconds" },
    { title: "Платные (сек)", dataIndex: "billable_seconds" },
    {
      title: "Расчёт / Проводка (коп)",
      render: (_, r) => (
        <span>
          {r.calculated_kopecks} / <strong>{r.posted_kopecks}</strong> (отброс: {r.discarded_kopecks})
        </span>
      ),
    },
    {
      title: "Сверено",
      dataIndex: "is_reconciled",
      render: (ok: boolean) => (ok ? <Tag color="green">Да</Tag> : <Tag color="red">Mismatch</Tag>),
    },
    {
      title: "Действия",
      render: (_, r) => (
        <Button
          size="small"
          icon={<LinkOutlined />}
          onClick={() =>
            openDrilldown({
              terminal_id: r.terminal_id,
              tenant_id: r.tenant_id,
              correlation_id: r.correlation_id,
            })
          }
        >
          Drill-down
        </Button>
      ),
    },
  ];

  const tenantFinanceColumns: ColumnsType<HubTenantFinanceItem> = [
    { title: "ID", dataIndex: "tenant_id", width: 70 },
    { title: "Организация", dataIndex: "tenant_name", render: (n: string, r) => n || `#${r.tenant_id}` },
    {
      title: "Баланс",
      dataIndex: "balance_rubles",
      render: (r: number) => (
        <strong style={{ color: r >= 0 ? "#389e0d" : "#cf1322" }}>
          {r.toFixed(2)} ₽
        </strong>
      ),
    },
    {
      title: "Entitlement",
      dataIndex: "entitlement",
      render: (ent: string) => {
        if (ent === "active") return <Tag color="green">Active</Tag>;
        if (ent === "grace") return <Tag color="orange">Grace (3 дня)</Tag>;
        return <Tag color="red">Blocked</Tag>;
      },
    },
    { title: "Терминалов", dataIndex: "terminal_count" },
    {
      title: "Окончание цикла",
      dataIndex: "current_cycle_ends_at",
      render: (d: string) => (d ? dayjs(d).format("YYYY-MM-DD") : "—"),
    },
    {
      title: "Дедлайн блокировки",
      dataIndex: "grace_deadline",
      render: (d: string) => (d ? dayjs(d).format("YYYY-MM-DD HH:mm") : "—"),
    },
    {
      title: "Действия",
      render: (_, r) => (
        <Space>
          <Button
            size="small"
            icon={<LinkOutlined />}
            onClick={() => openDrilldown({ tenant_id: r.tenant_id })}
          >
            Drill-down
          </Button>

        </Space>
      ),
    },
  ];

  const payColumns: ColumnsType<HubPaymentItem> = [
    { title: "ID", dataIndex: "id", width: 70 },
    { title: "Организация", dataIndex: "tenant_name", render: (n: string, r) => n || `#${r.tenant_id}` },
    {
      title: "Источник",
      dataIndex: "source",
      render: (s: string) => (
        <Tag color={s === "yookassa" ? "geekblue" : s === "manual" ? "green" : "volcano"}>
          {s.toUpperCase()}
        </Tag>
      ),
    },
    {
      title: "Сумма",
      dataIndex: "amount_rubles",
      render: (r: number) => <strong>{r.toFixed(2)} ₽</strong>,
    },
    {
      title: "Статус",
      dataIndex: "status",
      render: (st: string) => (
        <Tag color={st === "succeeded" || st === "posted" ? "green" : "orange"}>{st}</Tag>
      ),
    },
    { title: "Документ / Ref", dataIndex: "reference" },
    { title: "Плательщик", dataIndex: "payer" },
    {
      title: "Создан",
      dataIndex: "created_at",
      render: (d: string) => dayjs(d).format("YYYY-MM-DD HH:mm"),
    },
    {
      title: "Действия",
      render: (_, r) => (
        <Space>
          <Button
            size="small"
            icon={<LinkOutlined />}
            onClick={() =>
              openDrilldown({
                payment_id: r.id,
                tenant_id: r.tenant_id,
                correlation_id: r.correlation_id,
              })
            }
          >
            Drill-down
          </Button>

        </Space>
      ),
    },
  ];

  const notifColumns: ColumnsType<HubNotificationItem> = [
    { title: "ID", dataIndex: "id", width: 70 },
    { title: "Организация", dataIndex: "tenant_name", render: (n: string, r) => n || `#${r.tenant_id}` },
    {
      title: "Тип уведомления",
      dataIndex: "notification_type",
      render: (t: string) => <Tag color="blue">{t}</Tag>,
    },
    {
      title: "Статус",
      dataIndex: "status",
      render: (st: string) => (
        <Tag color={st === "sent" ? "green" : st === "failed" ? "red" : "orange"}>{st}</Tag>
      ),
    },
    { title: "Попыток", dataIndex: "attempts" },
    {
      title: "Запланировано",
      dataIndex: "scheduled_at",
      render: (d: string) => dayjs(d).format("YYYY-MM-DD HH:mm"),
    },
    {
      title: "Ошибка",
      dataIndex: "last_error",
      render: (err: string) => (err ? <Text type="danger">{err}</Text> : "—"),
    },
  ];

  const auditColumns: ColumnsType<HubAuditEventItem> = [
    { title: "Время", dataIndex: "occurred_at", render: (d: string) => dayjs(d).format("YYYY-MM-DD HH:mm:ss") },
    { title: "Actor", dataIndex: "actor" },
    { title: "Событие", dataIndex: "event_type", render: (e: string) => <Tag color="purple">{e}</Tag> },
    { title: "Объект", dataIndex: "subject_type" },
    {
      title: "Результат",
      dataIndex: "outcome",
      render: (o: string) => (o === "success" ? <Tag color="green">SUCCESS</Tag> : <Tag color="red">{o}</Tag>),
    },
    {
      title: "Детали",
      dataIndex: "details",
      render: (det: any) => (det ? <Text code style={{ fontSize: 11 }}>{JSON.stringify(det)}</Text> : "—"),
    },
  ];

  const archiveColumns: ColumnsType<HubArchiveBatchItem> = [
    {
      title: "Batch ID",
      dataIndex: "archive_batch_id",
      key: "archive_batch_id",
      render: (id: string) => <Tag color="geekblue">{id}</Tag>,
    },
    {
      title: "Источник (Owner)",
      dataIndex: "owner_project",
      key: "owner_project",
      render: (owner: string) => {
        let color = "blue";
        if (owner === "l4media") color = "purple";
        if (owner === "MenuBuilder") color = "green";
        return <Tag color={color}>{owner}</Tag>;
      },
    },
    {
      title: "Месяц",
      dataIndex: "source_month",
      key: "source_month",
    },
    {
      title: "Состояние",
      dataIndex: "state",
      key: "state",
      render: (st: string) => {
        let color = "default";
        if (st === "prepared") color = "orange";
        if (st === "verified") color = "cyan";
        if (st === "purged") color = "green";
        if (st === "failed") color = "red";
        return <Tag color={color}>{st.toUpperCase()}</Tag>;
      },
    },
    {
      title: "Типы записей",
      dataIndex: "record_types",
      key: "record_types",
      render: (types: string[]) => (
        <Space wrap>
          {(types || []).map((t) => (
            <Tag key={t}>{t}</Tag>
          ))}
        </Space>
      ),
    },
    {
      title: "Записей",
      dataIndex: "row_count",
      key: "row_count",
      render: (v: number) => (v !== undefined ? v.toLocaleString() : "0"),
    },
    {
      title: "SHA-256",
      dataIndex: "checksum_sha256",
      key: "checksum_sha256",
      render: (sha: string) => (
        <Text copyable={{ text: sha }}>
          {sha ? `${sha.slice(0, 8)}...${sha.slice(-8)}` : "—"}
        </Text>
      ),
    },
    {
      title: "Канонический URI (Location)",
      dataIndex: "location_reference",
      key: "location_reference",
      render: (ref: string) => (
        <Tag color="default" title="Физический путь к диску защищён и скрыт">{ref}</Tag>
      ),
    },
    {
      title: "Срок хранения",
      dataIndex: "retain_until",
      key: "retain_until",
      render: (dt: string) => (dt ? dayjs(dt).format("YYYY-MM-DD") : "—"),
    },
    {
      title: "Нестыковки / Ошибки",
      key: "issues",
      render: (_, record) => (
        <Space wrap>
          {record.has_checksum_mismatch && <Tag color="error">CHECKSUM_MISMATCH</Tag>}
          {record.has_count_mismatch && <Tag color="error">COUNT_MISMATCH</Tag>}
          {record.issues && record.issues.filter(i => i !== "CHECKSUM_MISMATCH" && i !== "COUNT_MISMATCH").map((iss) => (
            <Tag color="volcano" key={iss}>{iss}</Tag>
          ))}
          {!record.has_checksum_mismatch && !record.has_count_mismatch && (!record.issues || record.issues.length === 0) && (
            <Tag color="success">OK</Tag>
          )}
        </Space>
      ),
    },
    {
      title: "Действия",
      key: "actions",
      render: (_, record) => (
        <Button
          size="small"
          icon={<FileTextOutlined />}
          onClick={() => {
            setSelectedArchive(record);
            setArchDetailModalOpen(true);
          }}
        >
          Манифест
        </Button>
      ),
    },
  ];

  return (
    <div style={{ padding: 24, maxWidth: 1400, margin: "0 auto" }}>
      {/* Top Header */}
      <Row justify="space-between" align="middle" style={{ marginBottom: 20 }}>
        <Col>
          <Title level={2} style={{ margin: 0 }}>
            <AuditOutlined style={{ marginRight: 8, color: "#1677ff" }} />
            Администрирование L4Desk
          </Title>
          <Text type="secondary">
            Единый суперпользовательский пульт сквозного контроля: registrations → terminals → provisioning → sessions → usage → ledger → balance
          </Text>
        </Col>
        <Col>
          <Space>


            <Button
              icon={<LinkOutlined />}
              onClick={() => {
                setDrilldownData(null);
                setDrilldownModalOpen(true);
              }}
            >
              Correlation Drill-down
            </Button>
          </Space>
        </Col>
      </Row>

      {/* Main Tabs */}
      <Tabs
        activeKey={activeTab}
        onChange={setActiveTab}
        type="card"
        items={[
          { key: "subscriptions", label: "Подписки", children: <AdminSubscriptionsPanel /> },
          {
            key: "registrations",
            label: "Регистрации",
            children: (
              <Card>
                <Row gutter={16} align="middle" style={{ marginBottom: 16 }}>
                  <Col span={6}>
                    <Input
                      placeholder="Поиск по email"
                      allowClear
                      value={regFilterEmail}
                      onChange={(e) => setRegFilterEmail(e.target.value)}
                      onPressEnter={loadRegistrations}
                    />
                  </Col>
                  <Col span={5}>
                    <Select
                      placeholder="Статус регистрации"
                      allowClear
                      style={{ width: "100%" }}
                      value={regFilterStatus}
                      onChange={setRegFilterStatus}
                      options={[
                        { label: "Все статусы", value: "" },
                        { label: "Ожидает подтверждения", value: "pending" },
                        { label: "Активирован", value: "consumed" },
                        { label: "Истёк срок", value: "expired" },
                      ]}
                    />
                  </Col>
                  <Col span={6}>
                    <Space>
                      <Switch
                        checked={regOnlyErrors}
                        onChange={setRegOnlyErrors}
                      />
                      <span>Только ошибки (неактивированные/просроченные)</span>
                    </Space>
                  </Col>
                  <Col span={7} style={{ textAlign: "right" }}>
                    <Button icon={<FilterOutlined />} type="primary" onClick={loadRegistrations}>
                      Применить
                    </Button>
                  </Col>
                </Row>
                <Table
                  dataSource={registrations}
                  columns={regColumns}
                  rowKey="id"
                  loading={regLoading}
                  pagination={{
                    current: regPage,
                    total: regTotal,
                    pageSize: 20,
                    onChange: setRegPage,
                    showTotal: (total) => `Всего: ${total}`,
                  }}
                />
              </Card>
            ),
          },
          {
            key: "terminals",
            label: "Терминалы и готовность",
            children: (
              <Card>
                <Row gutter={16} align="middle" style={{ marginBottom: 16 }}>
                  <Col span={6}>
                    <Input
                      placeholder="Поиск по серийному номеру (SN)"
                      allowClear
                      value={termFilterSn}
                      onChange={(e) => setTermFilterSn(e.target.value)}
                      onPressEnter={loadTerminals}
                    />
                  </Col>
                  <Col span={5}>
                    <Select
                      placeholder="Статус подготовки"
                      allowClear
                      style={{ width: "100%" }}
                      value={termFilterProv}
                      onChange={setTermFilterProv}
                      options={[
                        { label: "Все состояния", value: "" },
                        { label: "Ready", value: "ready" },
                        { label: "Pending", value: "pending" },
                        { label: "Failed", value: "failed" },
                      ]}
                    />
                  </Col>
                  <Col span={5}>
                    <Select
                      placeholder="Тип тарификации"
                      allowClear
                      style={{ width: "100%" }}
                      value={termFilterFree !== undefined ? String(termFilterFree) : undefined}
                      onChange={(val) => setTermFilterFree(val ? val === "true" : undefined)}
                      options={[
                        { label: "Все типы", value: "" },
                        { label: "1-й (Бесплатный)", value: "true" },
                        { label: "Платные (2-й и далее)", value: "false" },
                      ]}
                    />
                  </Col>
                  <Col span={4}>
                    <Space>
                      <Switch checked={termOnlyErrors} onChange={setTermOnlyErrors} />
                      <span>Только ошибки</span>
                    </Space>
                  </Col>
                  <Col span={4} style={{ textAlign: "right" }}>
                    <Button icon={<FilterOutlined />} type="primary" onClick={loadTerminals}>
                      Применить
                    </Button>
                  </Col>
                </Row>
                <Table
                  dataSource={terminals}
                  columns={termColumns}
                  rowKey="terminal_id"
                  loading={termLoading}
                  pagination={{
                    current: termPage,
                    total: termTotal,
                    pageSize: 20,
                    onChange: setTermPage,
                    showTotal: (total) => `Всего: ${total}`,
                  }}
                />
              </Card>
            ),
          },
          {
            key: "sessions",
            label: "Сессии и потребление",
            children: (
              <Card>
                <Tabs
                  activeKey={subSessionTab}
                  onChange={setSubSessionTab}
                  items={[
                    {
                      key: "sessions",
                      label: "Удалённые сессии (Console / Video)",
                      children: (
                        <div>
                          <Row style={{ marginBottom: 16 }}>
                            <Space>
                              <Switch checked={sessionOnlyErrors} onChange={setSessionOnlyErrors} />
                              <span>Только сбои сессий</span>
                            </Space>
                          </Row>
                          <Table
                            dataSource={sessions}
                            columns={sessColumns}
                            rowKey="id"
                            loading={sessionLoading}
                            pagination={{
                              current: sessionPage,
                              total: sessionTotal,
                              pageSize: 20,
                              onChange: setSessionPage,
                              showTotal: (total) => `Всего: ${total}`,
                            }}
                          />
                        </div>
                      ),
                    },
                    {
                      key: "usage",
                      label: "Суточный регистр потребления (Usage)",
                      children: (
                        <div>
                          <Row style={{ marginBottom: 16 }}>
                            <Space>
                              <Switch checked={usageOnlyUnreconciled} onChange={setUsageOnlyUnreconciled} />
                              <span>Только непроведённые / несогласованные</span>
                            </Space>
                          </Row>
                          <Table
                            dataSource={usageList}
                            columns={usageColumns}
                            rowKey="id"
                            loading={usageLoading}
                            pagination={{
                              current: usagePage,
                              total: usageTotal,
                              pageSize: 20,
                              onChange: setUsagePage,
                              showTotal: (total) => `Всего: ${total}`,
                            }}
                          />
                        </div>
                      ),
                    },
                  ]}
                />
              </Card>
            ),
          },
          {
            key: "finance",
            label: "История прежних финансов",
            children: (
              <Card>
                {/* Statistics Row */}
                <Row gutter={16} style={{ marginBottom: 20 }}>
                  <Col span={5}>
                    <Card size="small">
                      <Statistic title="Всего организаций" value={financeOverview.total_tenants} />
                    </Card>
                  </Col>
                  <Col span={5}>
                    <Card size="small">
                      <Statistic
                        title="Активные подписки"
                        value={financeOverview.active_tenants}
                        valueStyle={{ color: "#3f8600" }}
                      />
                    </Card>
                  </Col>
                  <Col span={4}>
                    <Card size="small">
                      <Statistic
                        title="В отсрочке (Grace)"
                        value={financeOverview.grace_tenants}
                        valueStyle={{ color: "#faad14" }}
                      />
                    </Card>
                  </Col>
                  <Col span={4}>
                    <Card size="small">
                      <Statistic
                        title="Заблокировано"
                        value={financeOverview.blocked_tenants}
                        valueStyle={{ color: "#cf1322" }}
                      />
                    </Card>
                  </Col>
                  <Col span={6}>
                    <Card size="small">
                      <Statistic
                        title="Суммарный баланс депозитов"
                        value={financeOverview.total_balance_rubles}
                        precision={2}
                        suffix="₽"
                      />
                    </Card>
                  </Col>
                </Row>

                <Tabs
                  activeKey={subFinanceTab}
                  onChange={setSubFinanceTab}
                  items={[
                    {
                      key: "overview",
                      label: "Организации и статус подписок",
                      children: (
                        <div>
                          <Row gutter={16} align="middle" style={{ marginBottom: 16 }}>
                            <Col span={6}>
                              <Select
                                placeholder="Фильтр по статусу"
                                allowClear
                                style={{ width: "100%" }}
                                value={financeEntitlementFilter}
                                onChange={setFinanceEntitlementFilter}
                                options={[
                                  { label: "Все статусы", value: "" },
                                  { label: "Active", value: "active" },
                                  { label: "Grace (3 дня)", value: "grace" },
                                  { label: "Blocked", value: "blocked" },
                                ]}
                              />
                            </Col>
                          </Row>
                          <Table
                            dataSource={financeOverview.tenants}
                            columns={tenantFinanceColumns}
                            rowKey="tenant_id"
                            loading={financeLoading}
                            pagination={{
                              current: financePage,
                              total: financeOverview.total,
                              pageSize: 20,
                              onChange: setFinancePage,
                              showTotal: (total) => `Всего: ${total}`,
                            }}
                          />
                        </div>
                      ),
                    },
                    {
                      key: "payments",
                      label: "Журнал платежей (ЮKassa и Банк)",
                      children: (
                        <div>
                          <Row gutter={16} align="middle" style={{ marginBottom: 16 }}>
                            <Col span={6}>
                              <Select
                                placeholder="Источник платежа"
                                allowClear
                                style={{ width: "100%" }}
                                value={paySourceFilter}
                                onChange={setPaySourceFilter}
                                options={[
                                  { label: "Все источники", value: "" },
                                  { label: "ЮKassa (физлица)", value: "yookassa" },
                                  { label: "Банк (ручной b2b)", value: "manual" },
                                ]}
                              />
                            </Col>
                            <Col span={18} style={{ textAlign: "right" }}>

                            </Col>
                          </Row>
                          <Table
                            dataSource={payments}
                            columns={payColumns}
                            rowKey="id"
                            loading={payLoading}
                            pagination={{
                              current: payPage,
                              total: payTotal,
                              pageSize: 20,
                              onChange: setPayPage,
                              showTotal: (total) => `Всего: ${total}`,
                            }}
                          />
                        </div>
                      ),
                    },
                  ]}
                />
              </Card>
            ),
          },
          {
            key: "notifications",
            label: "Уведомления и системные ошибки",
            children: (
              <Card>
                <Tabs
                  activeKey={subNotifTab}
                  onChange={setSubNotifTab}
                  items={[
                    {
                      key: "notifications",
                      label: "Доставка email-уведомлений",
                      children: (
                        <div>
                          <Row style={{ marginBottom: 16 }}>
                            <Space>
                              <Switch checked={notifOnlyErrors} onChange={setNotifOnlyErrors} />
                              <span>Только сбои доставки</span>
                            </Space>
                          </Row>
                          <Table
                            dataSource={notifications}
                            columns={notifColumns}
                            rowKey="id"
                            loading={notifLoading}
                            pagination={{
                              current: notifPage,
                              total: notifTotal,
                              pageSize: 20,
                              onChange: setNotifPage,
                              showTotal: (total) => `Всего: ${total}`,
                            }}
                          />
                        </div>
                      ),
                    },
                    {
                      key: "audits",
                      label: "Журнал аудита операций и ошибок",
                      children: (
                        <div>
                          <Row style={{ marginBottom: 16 }}>
                            <Space>
                              <Switch checked={auditOnlyErrors} onChange={setAuditOnlyErrors} />
                              <span>Только события ошибок</span>
                            </Space>
                          </Row>
                          <Table
                            dataSource={auditEvents}
                            columns={auditColumns}
                            rowKey="id"
                            loading={auditLoading}
                            pagination={{
                              current: auditPage,
                              total: auditTotal,
                              pageSize: 20,
                              onChange: setAuditPage,
                              showTotal: (total) => `Всего: ${total}`,
                            }}
                          />
                        </div>
                      ),
                    },
                  ]}
                />
              </Card>
            ),
          },
          {
            key: "archives",
            label: "Архивы и Retention",
            children: (
              <Card>
                <Row gutter={16} align="middle" style={{ marginBottom: 16 }}>
                  <Col span={5}>
                    <Select
                      placeholder="Источник (Owner)"
                      allowClear
                      style={{ width: "100%" }}
                      value={archFilterOwner}
                      onChange={setArchFilterOwner}
                      options={[
                        { label: "Все источники", value: "" },
                        { label: "iot-rpc-rest-app", value: "iot-rpc-rest-app" },
                        { label: "l4media", value: "l4media" },
                        { label: "MenuBuilder", value: "MenuBuilder" },
                      ]}
                    />
                  </Col>
                  <Col span={4}>
                    <Select
                      placeholder="Состояние"
                      allowClear
                      style={{ width: "100%" }}
                      value={archFilterState}
                      onChange={setArchFilterState}
                      options={[
                        { label: "Все состояния", value: "" },
                        { label: "Prepared", value: "prepared" },
                        { label: "Verified", value: "verified" },
                        { label: "Purged", value: "purged" },
                        { label: "Failed", value: "failed" },
                      ]}
                    />
                  </Col>
                  <Col span={4}>
                    <Input
                      placeholder="Месяц (YYYY-MM)"
                      allowClear
                      value={archFilterMonth}
                      onChange={(e) => setArchFilterMonth(e.target.value)}
                      onPressEnter={loadArchives}
                    />
                  </Col>
                  <Col span={6}>
                    <Space>
                      <Switch checked={archOnlyErrors} onChange={setArchOnlyErrors} />
                      <span>Только нестыковки и ошибки</span>
                    </Space>
                  </Col>
                  <Col span={5} style={{ textAlign: "right" }}>
                    <Space>
                      <Button icon={<FilterOutlined />} type="primary" onClick={loadArchives}>
                        Применить
                      </Button>
                      <Button icon={<UploadOutlined />} onClick={() => setArchImportModalOpen(true)}>
                        Импорт
                      </Button>
                    </Space>
                  </Col>
                </Row>
                <Table
                  dataSource={archives}
                  columns={archiveColumns}
                  rowKey="archive_batch_id"
                  loading={archLoading}
                  pagination={{
                    current: archPage,
                    total: archTotal,
                    pageSize: 20,
                    onChange: setArchPage,
                    showTotal: (total) => `Всего батчей: ${total}`,
                  }}
                />
              </Card>
            ),
          },
        ]}
      />

      {/* Correlation Drill-Down Modal */}
      <Modal
        title="Сквозной аудит цепочки (Correlation Drill-down)"
        open={drilldownModalOpen}
        onCancel={() => setDrilldownModalOpen(false)}
        footer={null}
        width={950}
      >
        <div style={{ marginBottom: 20 }}>
          <Row gutter={12}>
            <Col span={7}>
              <Input
                placeholder="Correlation ID"
                value={drilldownCorrelationInput}
                onChange={(e) => setDrilldownCorrelationInput(e.target.value)}
              />
            </Col>
            <Col span={4}>
              <InputNumber
                placeholder="Tenant ID"
                style={{ width: "100%" }}
                value={drilldownTenantIdInput}
                onChange={(v) => setDrilldownTenantIdInput(v || undefined)}
              />
            </Col>
            <Col span={4}>
              <InputNumber
                placeholder="Terminal ID"
                style={{ width: "100%" }}
                value={drilldownTerminalIdInput}
                onChange={(v) => setDrilldownTerminalIdInput(v || undefined)}
              />
            </Col>
            <Col span={6}>
              <Input
                placeholder="Archive Batch ID"
                style={{ width: "100%" }}
                value={drilldownArchiveBatchInput}
                onChange={(e) => setDrilldownArchiveBatchInput(e.target.value)}
              />
            </Col>
            <Col span={3}>
              <Button
                type="primary"
                loading={drilldownLoading}
                onClick={() =>
                  openDrilldown({
                    correlation_id: drilldownCorrelationInput || undefined,
                    tenant_id: drilldownTenantIdInput,
                    terminal_id: drilldownTerminalIdInput,
                    archive_batch_id: drilldownArchiveBatchInput || undefined,
                  })
                }
              >
                Найти
              </Button>
            </Col>
          </Row>
        </div>

        {drilldownData && (
          <div>
            {drilldownData.overall_status === "matched" ? (
              <Alert
                type="success"
                showIcon
                icon={<CheckCircleOutlined />}
                message="Все звенья цепочки согласованы (MATCHED)"
                description="Факты подтверждены от регистрации до финансовой проводки."
                style={{ marginBottom: 16 }}
              />
            ) : (
              <Alert
                type="error"
                showIcon
                icon={<CloseCircleOutlined />}
                message={`Обнаружено нестыковок: ${drilldownData.mismatch_codes.length} (MISMATCH)`}
                description={
                  <span>
                    Отсутствующие или нарушенные факты:{" "}
                    {drilldownData.mismatch_codes.map((c) => (
                      <Tag color="red" key={c}>
                        {c}
                      </Tag>
                    ))}
                  </span>
                }
                style={{ marginBottom: 16 }}
              />
            )}

            {/* Nodes Chain Render */}
            <Row gutter={[12, 12]}>
              {[
                { key: "registration", label: "1. Регистрация" },
                { key: "terminal", label: "2. Терминал" },
                { key: "pin_provisioning", label: "3. PIN / Сертификат" },
                { key: "online_session", label: "4. Online / Сессия" },
                { key: "usage", label: "5. Потребление (Usage)" },
                { key: "ledger_payment", label: "6. Проводка / Платёж" },
                { key: "archive", label: "7. Архивный манифест" },
              ].map((step) => {
                const node = drilldownData.nodes[step.key];
                if (!node) return null;
                const isError = !node.present || node.mismatch;
                return (
                  <Col span={12} key={step.key}>
                    <Card
                      size="small"
                      title={
                        <Space>
                          <span>{step.label}</span>
                          {node.present ? (
                            node.mismatch ? (
                              <Tag color="error">MISMATCH</Tag>
                            ) : (
                              <Tag color="success">Подтвержден</Tag>
                            )
                          ) : (
                            <Tag color="red">Отсутствует факт</Tag>
                          )}
                        </Space>
                      }
                      style={{
                        borderColor: isError ? "#ffa39e" : "#b7eb8f",
                        backgroundColor: isError ? "#fff1f0" : "#f6ffed",
                      }}
                    >
                      <p style={{ margin: "0 0 6px 0", fontSize: 13 }}>{node.details}</p>
                      {node.mismatch_code && (
                        <p style={{ margin: "0 0 6px 0" }}>
                          <Text type="danger">Код нестыковки: {node.mismatch_code}</Text>
                        </p>
                      )}
                      {!node.present && (
                        <Text type="secondary" italic style={{ fontSize: 12 }}>
                          Факт отсутствует в системе (не дорисован)
                        </Text>
                      )}
                      {node.fact && (
                        <pre style={{ margin: 0, fontSize: 11, maxHeight: 120, overflow: "auto" }}>
                          {JSON.stringify(node.fact, null, 2)}
                        </pre>
                      )}
                    </Card>
                  </Col>
                );
              })}
            </Row>
          </div>
        )}
      </Modal>

      {/* Archive Manifest Detail Modal */}
      <Modal
        title="Архивный манифест и верификация (RBAC Safe)"
        open={archDetailModalOpen}
        onCancel={() => setArchDetailModalOpen(false)}
        footer={[
          <Button key="close" onClick={() => setArchDetailModalOpen(false)}>
            Закрыть
          </Button>,
        ]}
        width={750}
      >
        {selectedArchive && (
          <div>
            <Alert
              type="info"
              message="Канонический архивный манифест"
              description={`Батч ${selectedArchive.archive_batch_id} (${selectedArchive.owner_project}). Физический путь защищён и скрыт: ${selectedArchive.location_reference}`}
              style={{ marginBottom: 16 }}
            />
            <Row gutter={[16, 12]} style={{ marginBottom: 16 }}>
              <Col span={12}>
                <Text strong>Состояние:</Text>{" "}
                <Tag
                  color={
                    selectedArchive.state === "purged"
                      ? "green"
                      : selectedArchive.state === "verified"
                      ? "cyan"
                      : selectedArchive.state === "failed"
                      ? "red"
                      : "orange"
                  }
                >
                  {selectedArchive.state.toUpperCase()}
                </Tag>
              </Col>
              <Col span={12}>
                <Text strong>Период:</Text> {selectedArchive.source_month}
              </Col>
              <Col span={12}>
                <Text strong>Всего записей:</Text>{" "}
                {selectedArchive.row_count.toLocaleString()}
              </Col>
              <Col span={12}>
                <Text strong>Срок хранения:</Text>{" "}
                {dayjs(selectedArchive.retain_until).format("YYYY-MM-DD")}
              </Col>
              <Col span={24}>
                <Text strong>SHA-256:</Text>{" "}
                <Text code copyable>{selectedArchive.checksum_sha256}</Text>
              </Col>
              <Col span={24}>
                <Text strong>Канонический URI:</Text>{" "}
                <Text code>{selectedArchive.location_reference}</Text>
              </Col>
            </Row>
            {selectedArchive.issues && selectedArchive.issues.length > 0 && (
              <Alert
                type="error"
                message="Обнаружены нестыковки манифеста"
                description={
                  <Space wrap>
                    {selectedArchive.issues.map((iss) => (
                      <Tag color="red" key={iss}>
                        {iss}
                      </Tag>
                    ))}
                  </Space>
                }
                style={{ marginBottom: 16 }}
              />
            )}
            <Card size="small" title="Манифест (JSON)">
              <pre
                style={{
                  margin: 0,
                  fontSize: 11,
                  maxHeight: 250,
                  overflow: "auto",
                }}
              >
                {JSON.stringify(
                  selectedArchive.manifest || selectedArchive,
                  null,
                  2
                )}
              </pre>
            </Card>
          </div>
        )}
      </Modal>

      {/* Import Archive Manifest Modal */}
      <Modal
        title="Импорт архивного манифеста (Archive Manifest Import)"
        open={archImportModalOpen}
        onCancel={() => setArchImportModalOpen(false)}
        onOk={handleImportManifest}
        confirmLoading={archImportLoading}
        okText="Импортировать"
        cancelText="Отмена"
        width={700}
      >
        <p>
          Вставьте canonical JSON манифеста v1.0.0 (соответствующий Archive
          Manifest Contract):
        </p>
        <Input.TextArea
          rows={12}
          value={archImportJson}
          onChange={(e) => setArchImportJson(e.target.value)}
          placeholder={`{
  "archive_manifest_version": "1.0.0",
  "archive_batch_id": "arch-iot-2026-05-b91c84f2",
  "owner_project": "iot-rpc-rest-app",
  ...
}`}
        />
      </Modal>
    </div>
  );
}
