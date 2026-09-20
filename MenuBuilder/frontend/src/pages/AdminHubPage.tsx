import React, { useEffect, useState } from "react";
import {
  AuditOutlined,
  CheckCircleOutlined,
  CloseCircleOutlined,
  DollarOutlined,
  ExclamationCircleOutlined,
  FilterOutlined,
  LinkOutlined,
  PlusOutlined,
  RollbackOutlined,
  SyncOutlined,
} from "@ant-design/icons";
import {
  Alert,
  Badge,
  Button,
  Card,
  Col,
  DatePicker,
  Form,
  Input,
  InputNumber,
  Modal,
  Popconfirm,
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
  HubAuditEventItem,
  HubNotificationItem,
  HubPaymentItem,
  HubRegistrationItem,
  HubSessionItem,
  HubTenantFinanceItem,
  HubTerminalItem,
  HubUsageItem,
  ReconciliationRunResponse,
  createHubManualPayment,
  fetchHubAuditEvents,
  fetchHubCorrelationDrilldown,
  fetchHubFinanceOverview,
  fetchHubNotifications,
  fetchHubPayments,
  fetchHubRegistrations,
  fetchHubSessions,
  fetchHubTerminals,
  fetchHubUsage,
  stornoHubManualPayment,
  triggerHubReconciliation,
} from "../api/hub";

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

  // Modals state
  const [drilldownModalOpen, setDrilldownModalOpen] = useState(false);
  const [drilldownLoading, setDrilldownLoading] = useState(false);
  const [drilldownData, setDrilldownData] = useState<CorrelationDrilldownResponse | null>(null);
  const [drilldownCorrelationInput, setDrilldownCorrelationInput] = useState("");
  const [drilldownTenantIdInput, setDrilldownTenantIdInput] = useState<number | undefined>();
  const [drilldownTerminalIdInput, setDrilldownTerminalIdInput] = useState<number | undefined>();

  const [manualPayModalOpen, setManualPayModalOpen] = useState(false);
  const [manualPayForm] = Form.useForm();
  const [manualPayLoading, setManualPayLoading] = useState(false);

  const [stornoModalOpen, setStornoModalOpen] = useState(false);
  const [stornoForm] = Form.useForm();
  const [stornoTargetPaymentId, setStornoTargetPaymentId] = useState<number | null>(null);
  const [stornoLoading, setStornoLoading] = useState(false);

  const [recModalOpen, setRecModalOpen] = useState(false);
  const [recForm] = Form.useForm();
  const [recLoading, setRecLoading] = useState(false);
  const [recResult, setRecResult] = useState<ReconciliationRunResponse | null>(null);

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
  ]);

  // Drilldown handler
  const openDrilldown = async (params: {
    correlation_id?: string;
    tenant_id?: number;
    terminal_id?: number;
    session_id?: number;
    payment_id?: number;
  }) => {
    setDrilldownModalOpen(true);
    setDrilldownLoading(true);
    try {
      const res = await fetchHubCorrelationDrilldown(params);
      setDrilldownData(res);
      if (params.correlation_id) setDrilldownCorrelationInput(params.correlation_id);
      if (params.tenant_id) setDrilldownTenantIdInput(params.tenant_id);
      if (params.terminal_id) setDrilldownTerminalIdInput(params.terminal_id);
    } catch (err: unknown) {
      message.error("Не удалось построить цепочку сверки");
    } finally {
      setDrilldownLoading(false);
    }
  };

  // Manual payment submit handler
  const handleManualPaymentSubmit = async (values: any) => {
    if (values.confirmation_code !== "11") {
      message.error("Контрольный код должен быть равен '11'");
      return;
    }
    setManualPayLoading(true);
    try {
      await createHubManualPayment({
        tenant_id: values.tenant_id,
        amount_rubles: values.amount_rubles,
        received_on: values.received_on.format("YYYY-MM-DD"),
        document_number: values.document_number,
        purpose: values.purpose,
        payer: values.payer,
        comment: values.comment,
        evidence_reference: values.evidence_reference,
        confirmation_code: values.confirmation_code,
      });
      message.success("Ручной платёж успешно проведён с подтверждением 11");
      setManualPayModalOpen(false);
      manualPayForm.resetFields();
      if (activeTab === "finance") {
        loadFinanceOverview();
        loadPayments();
      }
    } catch (err: any) {
      message.error(err?.response?.data?.detail || "Ошибка создания платежа");
    } finally {
      setManualPayLoading(false);
    }
  };

  // Storno submit handler
  const handleStornoSubmit = async (values: any) => {
    if (!stornoTargetPaymentId) return;
    if (values.confirmation_code !== "11") {
      message.error("Контрольный код должен быть равен '11'");
      return;
    }
    setStornoLoading(true);
    try {
      await stornoHubManualPayment(stornoTargetPaymentId, {
        reversal_reason: values.reversal_reason,
        comment: values.comment,
        confirmation_code: values.confirmation_code,
      });
      message.success("Сторно успешно проведено с подтверждением 11");
      setStornoModalOpen(false);
      stornoForm.resetFields();
      setStornoTargetPaymentId(null);
      if (activeTab === "finance") {
        loadFinanceOverview();
        loadPayments();
      }
    } catch (err: any) {
      message.error(err?.response?.data?.detail || "Ошибка сторнирования платежа");
    } finally {
      setStornoLoading(false);
    }
  };

  // Reconciliation run handler
  const handleReconciliationRun = async (values: any) => {
    setRecLoading(true);
    try {
      const res = await triggerHubReconciliation({
        period_start: values.period[0].toISOString(),
        period_end: values.period[1].toISOString(),
        tenant_id: values.tenant_id || undefined,
        auto_rebuild_projection: values.auto_rebuild_projection || false,
      });
      setRecResult(res);
      if (res.status === "matched") {
        message.success("Сверка завершена успешно: расхождений не обнаружено!");
      } else {
        message.warning(`Сверка выявила ${res.mismatch_count} расхождений!`);
      }
    } catch (err: any) {
      message.error(err?.response?.data?.detail || "Ошибка выполнения сверки");
    } finally {
      setRecLoading(false);
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
          <Button
            size="small"
            type="primary"
            icon={<PlusOutlined />}
            onClick={() => {
              manualPayForm.setFieldsValue({ tenant_id: r.tenant_id });
              setManualPayModalOpen(true);
            }}
          >
            Платёж 11
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
          {r.source === "manual" && !r.reference.startsWith("STORNO-") && (
            <Button
              size="small"
              danger
              icon={<RollbackOutlined />}
              onClick={() => {
                setStornoTargetPaymentId(r.id);
                stornoForm.resetFields();
                setStornoModalOpen(true);
              }}
            >
              Сторно 11
            </Button>
          )}
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

  return (
    <div style={{ padding: 24, maxWidth: 1400, margin: "0 auto" }}>
      {/* Top Header */}
      <Row justify="space-between" align="middle" style={{ marginBottom: 20 }}>
        <Col>
          <Title level={2} style={{ margin: 0 }}>
            <AuditOutlined style={{ marginRight: 8, color: "#1677ff" }} />
            Хаб и финансовая сверка (L4Desk Hub)
          </Title>
          <Text type="secondary">
            Единый суперпользовательский пульт сквозного контроля: registrations → terminals → provisioning → sessions → usage → ledger → balance
          </Text>
        </Col>
        <Col>
          <Space>
            <Button
              type="primary"
              icon={<SyncOutlined />}
              onClick={() => {
                recForm.setFieldsValue({
                  period: [dayjs().subtract(30, "days"), dayjs()],
                  auto_rebuild_projection: false,
                });
                setRecResult(null);
                setRecModalOpen(true);
              }}
            >
              Запустить фин. сверку
            </Button>
            <Button
              icon={<DollarOutlined />}
              onClick={() => {
                manualPayForm.resetFields();
                setManualPayModalOpen(true);
              }}
            >
              Ручной платёж (11)
            </Button>
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
            label: "Лицензии, финансы и сверка",
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
                              <Button
                                type="primary"
                                icon={<PlusOutlined />}
                                onClick={() => {
                                  manualPayForm.resetFields();
                                  setManualPayModalOpen(true);
                                }}
                              >
                                Новый ручной платёж (11)
                              </Button>
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
            <Col span={10}>
              <Input
                placeholder="Correlation ID"
                value={drilldownCorrelationInput}
                onChange={(e) => setDrilldownCorrelationInput(e.target.value)}
              />
            </Col>
            <Col span={6}>
              <InputNumber
                placeholder="Tenant ID"
                style={{ width: "100%" }}
                value={drilldownTenantIdInput}
                onChange={(v) => setDrilldownTenantIdInput(v || undefined)}
              />
            </Col>
            <Col span={5}>
              <InputNumber
                placeholder="Terminal ID"
                style={{ width: "100%" }}
                value={drilldownTerminalIdInput}
                onChange={(v) => setDrilldownTerminalIdInput(v || undefined)}
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

      {/* Manual Payment Modal (with Superuser Code 11 Confirmation) */}
      <Modal
        title="Ручная регистрация банковской оплаты юрлица (Код подтверждения: 11)"
        open={manualPayModalOpen}
        onCancel={() => setManualPayModalOpen(false)}
        footer={null}
        width={650}
      >
        <Alert
          type="info"
          message="Безопасная суперпользовательская операция"
          description="Регистрация оплаты b2b создаёт двойную запись в subledger и немедленно фиксирует anchor-день расчётного цикла. Операция необратима напрямую и требует подтверждения кодом '11'."
          style={{ marginBottom: 16 }}
        />
        <Form form={manualPayForm} layout="vertical" onFinish={handleManualPaymentSubmit}>
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item
                name="tenant_id"
                label="ID организации (Tenant)"
                rules={[{ required: true, message: "Укажите ID организации" }]}
              >
                <InputNumber style={{ width: "100%" }} min={1} placeholder="Например: 10" />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item
                name="amount_rubles"
                label="Сумма оплаты (целые рубли)"
                rules={[{ required: true, message: "Укажите сумму в рублях" }]}
              >
                <InputNumber style={{ width: "100%" }} min={1} placeholder="Например: 10000" />
              </Form.Item>
            </Col>
          </Row>

          <Row gutter={16}>
            <Col span={12}>
              <Form.Item
                name="received_on"
                label="Дата выписки / поступления"
                rules={[{ required: true, message: "Выберите дату поступления" }]}
                initialValue={dayjs()}
              >
                <DatePicker style={{ width: "100%" }} />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item
                name="document_number"
                label="Номер платёжного поручения"
                rules={[{ required: true, message: "Укажите номер документа" }]}
              >
                <Input placeholder="ПП № 1024 от банка" />
              </Form.Item>
            </Col>
          </Row>

          <Form.Item
            name="payer"
            label="Плательщик (наименование юрлица / ИНН)"
            rules={[{ required: true, message: "Укажите наименование плательщика" }]}
          >
            <Input placeholder="ООО 'Северсталь-Авто' ИНН 7701234567" />
          </Form.Item>

          <Form.Item
            name="purpose"
            label="Назначение платежа"
            rules={[{ required: true, message: "Укажите назначение платежа" }]}
            initialValue="Оплата лицензии L4Desk по счёту"
          >
            <Input placeholder="Оплата по счёту № 45 за терминалы" />
          </Form.Item>

          <Form.Item name="comment" label="Внутренний комментарий">
            <Input.TextArea rows={2} placeholder="Поступило через р/с Сбербанк" />
          </Form.Item>

          <Form.Item
            name="confirmation_code"
            label={
              <Space>
                <strong style={{ color: "#cf1322" }}>Контрольный код подтверждения:</strong>
                <Tag color="red">Введите "11"</Tag>
              </Space>
            }
            rules={[
              { required: true, message: "Введите '11' для подтверждения действия" },
              {
                validator: (_, value) =>
                  value === "11"
                    ? Promise.resolve()
                    : Promise.reject(new Error("Необходимо ввести строго '11'")),
              },
            ]}
          >
            <Input placeholder="11" maxLength={2} style={{ width: 120, fontSize: 16, fontWeight: "bold" }} />
          </Form.Item>

          <Row justify="end" gutter={8}>
            <Col>
              <Button onClick={() => setManualPayModalOpen(false)}>Отмена</Button>
            </Col>
            <Col>
              <Button type="primary" htmlType="submit" loading={manualPayLoading} icon={<CheckCircleOutlined />}>
                Подтвердить и провести платёж (11)
              </Button>
            </Col>
          </Row>
        </Form>
      </Modal>

      {/* Storno Modal (Requires Superuser Code 11 Confirmation) */}
      <Modal
        title={`Сторно платежа #${stornoTargetPaymentId} (Код подтверждения: 11)`}
        open={stornoModalOpen}
        onCancel={() => setStornoModalOpen(false)}
        footer={null}
        width={550}
      >
        <Alert
          type="warning"
          message="Внимание: Финансовое сторнирование"
          description="Сторнирование создаёт зеркальную корректировочную проводку Reversal в subledger. Сумма списывается с депозита организации. Требуется подтверждение кодом '11'."
          style={{ marginBottom: 16 }}
        />
        <Form form={stornoForm} layout="vertical" onFinish={handleStornoSubmit}>
          <Form.Item
            name="reversal_reason"
            label="Причина сторнирования"
            rules={[{ required: true, message: "Укажите причину сторнирования" }]}
          >
            <Input placeholder="Ошибочное зачисление / возврат средств клиенту" />
          </Form.Item>

          <Form.Item name="comment" label="Комментарий">
            <Input.TextArea rows={2} placeholder="Акт возврата платежа №..." />
          </Form.Item>

          <Form.Item
            name="confirmation_code"
            label={
              <Space>
                <strong style={{ color: "#cf1322" }}>Контрольный код подтверждения:</strong>
                <Tag color="red">Введите "11"</Tag>
              </Space>
            }
            rules={[
              { required: true, message: "Введите '11' для подтверждения действия" },
              {
                validator: (_, value) =>
                  value === "11"
                    ? Promise.resolve()
                    : Promise.reject(new Error("Необходимо ввести строго '11'")),
              },
            ]}
          >
            <Input placeholder="11" maxLength={2} style={{ width: 120, fontSize: 16, fontWeight: "bold" }} />
          </Form.Item>

          <Row justify="end" gutter={8}>
            <Col>
              <Button onClick={() => setStornoModalOpen(false)}>Отмена</Button>
            </Col>
            <Col>
              <Button type="primary" danger htmlType="submit" loading={stornoLoading} icon={<RollbackOutlined />}>
                Сторнировать с кодом 11
              </Button>
            </Col>
          </Row>
        </Form>
      </Modal>

      {/* Reconciliation Modal */}
      <Modal
        title="Запуск финансовой сверки (Subledger Reconciliation)"
        open={recModalOpen}
        onCancel={() => setRecModalOpen(false)}
        footer={null}
        width={750}
      >
        <Form form={recForm} layout="vertical" onFinish={handleReconciliationRun}>
          <Form.Item
            name="period"
            label="Период сверки"
            rules={[{ required: true, message: "Выберите диапазон дат" }]}
          >
            <DatePicker.RangePicker showTime style={{ width: "100%" }} />
          </Form.Item>

          <Form.Item name="tenant_id" label="Организация (оставьте пустым для сверки всех организаций)">
            <InputNumber style={{ width: "100%" }} placeholder="Все организации" min={1} />
          </Form.Item>

          <Form.Item name="auto_rebuild_projection" valuePropName="checked">
            <Switch />
            <span style={{ marginLeft: 8 }}>
              Автоматически перестроить проекцию баланса при обнаружении расхождения
            </span>
          </Form.Item>

          <Form.Item>
            <Button type="primary" htmlType="submit" loading={recLoading} icon={<SyncOutlined />}>
              Выполнить сверку
            </Button>
          </Form.Item>
        </Form>

        {recResult && (
          <div style={{ marginTop: 20 }}>
            {recResult.status === "matched" ? (
              <Alert
                type="success"
                showIcon
                icon={<CheckCircleOutlined />}
                message="Сверка успешно завершена (MATCHED)"
                description={`Период сверен: ${recResult.debit_kopecks} коп debits = ${recResult.credit_kopecks} коп credits. Расхождений: 0.`}
              />
            ) : (
              <div>
                <Alert
                  type="error"
                  showIcon
                  icon={<CloseCircleOutlined />}
                  message={`Обнаружены нестыковки в периоде: ${recResult.mismatch_count} (MISMATCH)`}
                  description={`Разница в балансах: ${recResult.balance_difference_kopecks} коп. Подробности см. ниже.`}
                  style={{ marginBottom: 12 }}
                />
                <Card size="small" title="Детали нестыковок">
                  <pre style={{ margin: 0, fontSize: 11, maxHeight: 200, overflow: "auto" }}>
                    {JSON.stringify(recResult.details, null, 2)}
                  </pre>
                </Card>
              </div>
            )}
          </div>
        )}
      </Modal>
    </div>
  );
}
