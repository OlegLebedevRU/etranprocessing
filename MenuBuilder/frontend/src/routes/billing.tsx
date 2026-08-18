import { useEffect, useState, useCallback } from "react";
import {
  Card,
  Col,
  Row,
  Table,
  Tag,
  Button,
  Space,
  Modal,
  Tabs,
  Input,
  Spin,
  message,
  Typography,
} from "antd";
import {
  DollarOutlined,
  WarningOutlined,
  CalendarOutlined,
  StopOutlined,
  ReloadOutlined,
  SearchOutlined,
  SafetyCertificateOutlined,
} from "@ant-design/icons";
import type { ColumnsType } from "antd/es/table";
import {
  getBillingSummary,
  getBillingTerminals,
  deactivateTerminal,
  cancelDeactivation,
  createCheckout,
  createReactivationCheckout,
  confirmPayment,
  type BillingSummary,
  type BillingTerminal,
} from "../api/billing";
import CertificatePinModal from "../components/CertificatePinModal";
import {
  formatMoneyMinor,
  formatDebt,
  formatDate,
  formatBillingPeriod,
  formatForecastMonth,
  billingStatusLabel,
  billingStatusColor,
} from "../utils/billing";

const { Text } = Typography;

const STATUS_TABS = [
  { key: "attention", label: "Требуют внимания" },
  { key: "active", label: "Активные" },
  { key: "deactivation", label: "Отключение запланировано" },
  { key: "disabled", label: "Отключённые" },
];

export default function BillingPage() {
  const [summary, setSummary] = useState<BillingSummary | null>(null);
  const [terminals, setTerminals] = useState<BillingTerminal[]>([]);
  const [loading, setLoading] = useState(true);
  const [search, setSearch] = useState("");
  const [activeTab, setActiveTab] = useState("attention");
  const [deactivateModal, setDeactivateModal] = useState<{
    open: boolean;
    terminal: BillingTerminal | null;
  }>({ open: false, terminal: null });
  const [confirmLoading, setConfirmLoading] = useState(false);
  const [pinModal, setPinModal] = useState<{
    open: boolean;
    terminal: BillingTerminal | null;
  }>({ open: false, terminal: null });

  const fetchData = useCallback(async () => {
    try {
      setLoading(true);
      const [s, t] = await Promise.all([
        getBillingSummary(),
        getBillingTerminals(),
      ]);
      setSummary(s);
      setTerminals(t);
    } catch (e: unknown) {
      const msg = e instanceof Error ? e.message : "Ошибка загрузки";
      message.error(msg);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    fetchData();
  }, [fetchData]);

  const handleDeactivate = async () => {
    if (!deactivateModal.terminal) return;
    try {
      setConfirmLoading(true);
      await deactivateTerminal(deactivateModal.terminal.terminal_id);
      message.success("Терминал отключён от продления");
      setDeactivateModal({ open: false, terminal: null });
      fetchData();
    } catch (e: unknown) {
      const msg = e instanceof Error ? e.message : "Ошибка";
      message.error(msg);
    } finally {
      setConfirmLoading(false);
    }
  };

  const handleCancelDeactivation = async (terminalId: number) => {
    try {
      await cancelDeactivation(terminalId);
      message.success("Отключение отменено");
      fetchData();
    } catch (e: unknown) {
      const msg = e instanceof Error ? e.message : "Ошибка";
      message.error(msg);
    }
  };

  const handlePayOverdue = async (terminal: BillingTerminal) => {
    try {
      setConfirmLoading(true);
      const checkout = await createCheckout({
        items: [{ terminal_id: terminal.terminal_id, advance_periods: 0 }],
      });
      await confirmPayment(checkout.order_id);
      message.success("Оплата прошла успешно");
      fetchData();
    } catch (e: unknown) {
      const msg = e instanceof Error ? e.message : "Ошибка оплаты";
      message.error(msg);
    } finally {
      setConfirmLoading(false);
    }
  };

  const handleReactivate = async (terminal: BillingTerminal) => {
    try {
      setConfirmLoading(true);
      const checkout = await createReactivationCheckout(
        terminal.terminal_id,
        { advance_periods: 1 },
      );
      await confirmPayment(checkout.order_id);
      message.success("Терминал подключён");
      fetchData();
    } catch (e: unknown) {
      const msg = e instanceof Error ? e.message : "Ошибка подключения";
      message.error(msg);
    } finally {
      setConfirmLoading(false);
    }
  };

  // Filter terminals by tab
  const filteredTerminals = terminals.filter((t) => {
    // Search filter
    if (search) {
      const s = search.toLowerCase();
      if (!String(t.device_id).includes(s) && !t.sn.toLowerCase().includes(s)) {
        return false;
      }
    }

    switch (activeTab) {
      case "attention":
        return t.billing_status === "overdue" || t.billing_status === "due_soon" || t.billing_status === "no_license";
      case "active":
        return t.billing_status === "active" || t.billing_status === "due_soon";
      case "deactivation":
        return t.billing_status === "deactivation_scheduled";
      case "disabled":
        return t.billing_status === "disabled" || t.billing_status === "admin_disabled" || t.billing_status === "no_license";
      default:
        return true;
    }
  });

  // Counters
  const overdueCount = terminals.filter((t) => t.billing_status === "overdue").length;
  const activeCount = terminals.filter(
    (t) => t.billing_status === "active" || t.billing_status === "due_soon"
  ).length;
  const deactivationCount = terminals.filter(
    (t) => t.billing_status === "deactivation_scheduled"
  ).length;
  const disabledCount = terminals.filter(
    (t) => t.billing_status === "disabled" || t.billing_status === "admin_disabled"
  ).length;

  const columns: ColumnsType<BillingTerminal> = [
    {
      title: "Терминал",
      dataIndex: "device_id",
      key: "device_id",
      sorter: (a, b) => a.device_id - b.device_id,
    },
    {
      title: "SN",
      dataIndex: "sn",
      key: "sn",
      ellipsis: true,
    },
    {
      title: "Статус",
      dataIndex: "billing_status",
      key: "status",
      render: (status: string) => (
        <Tag color={billingStatusColor(status)}>{billingStatusLabel(status)}</Tag>
      ),
    },
    {
      title: "Лицензия до",
      dataIndex: "license_expires_at",
      key: "expires",
      render: (v: string | null) => formatDate(v),
    },
    {
      title: "Сертификат",
      dataIndex: "cert_not_valid_after",
      key: "cert_expires",
      render: (v: string | null, r: BillingTerminal) => {
        if (!r.cert_serial) {
          return <Text type="secondary">не выпущен</Text>;
        }
        if (!v) {
          // Legacy certificate issued before this app started tracking expiry.
          return <Text type="secondary">выпущен (дата неизвестна)</Text>;
        }
        return (
          <Text type={new Date(v) < new Date() ? "danger" : "secondary"}>
            {formatDate(v)}
          </Text>
        );
      },
    },
    {
      title: "Тариф",
      dataIndex: "monthly_price_minor",
      key: "tariff",
      align: "right",
      render: (v: number, r) => (
        <span>
          {formatMoneyMinor(v)}/{formatBillingPeriod(r.billing_period_months)}
        </span>
      ),
    },
    {
      title: "Задолженность",
      dataIndex: "overdue_amount_minor",
      key: "debt",
      align: "right",
      render: (v: number) =>
        v > 0 ? (
          <Text type="danger" strong>
            {formatDebt(v)}
          </Text>
        ) : (
          <Text type="secondary">{formatMoneyMinor(0)}</Text>
        ),
    },
    {
      title: "Следующий платёж",
      key: "next",
      align: "right",
      render: (_: unknown, r: BillingTerminal) => {
        if (r.billing_status === "overdue") return "после погашения";
        if (r.next_payment_amount_minor > 0)
          return formatMoneyMinor(r.next_payment_amount_minor);
        return "—";
      },
    },
    {
      title: "Действия",
      key: "actions",
      render: (_: unknown, r: BillingTerminal) => (
        <Space size="small">
          {r.billing_status === "overdue" && r.overdue_amount_minor > 0 && (
            <Button
              size="small"
              type="primary"
              loading={confirmLoading}
              onClick={() => handlePayOverdue(r)}
            >
              Оплатить {formatDebt(r.overdue_amount_minor)}
            </Button>
          )}
          {r.can_deactivate && (
            <Button
              size="small"
              danger
              onClick={() => setDeactivateModal({ open: true, terminal: r })}
            >
              Отключить
            </Button>
          )}
          {r.can_cancel_deactivation && (
            <Button
              size="small"
              type="primary"
              onClick={() => handleCancelDeactivation(r.terminal_id)}
            >
              Отменить отключение
            </Button>
          )}
          {r.can_reactivate && (
            <Button
              size="small"
              type="default"
              loading={confirmLoading}
              onClick={() => handleReactivate(r)}
            >
              Подключить
            </Button>
          )}
          {r.tenant_pin_creation_enabled && (
            <Button
              size="small"
              icon={<SafetyCertificateOutlined />}
              onClick={() => setPinModal({ open: true, terminal: r })}
            >
              {r.cert_serial ? "Перевыпустить сертификат" : "Получить PIN"}
            </Button>
          )}
        </Space>
      ),
    },
  ];

  if (loading && !summary) {
    return (
      <div style={{ textAlign: "center", padding: 48 }}>
        <Spin size="large" />
      </div>
    );
  }

  const deactivationTerminal = deactivateModal.terminal;
  const isExpiredDeactivation =
    deactivationTerminal?.license_expires_at
      ? new Date(deactivationTerminal.license_expires_at) <= new Date()
      : true;

  return (
    <div>
      {/* Summary cards */}
      <Row gutter={[12, 12]} style={{ marginBottom: 12 }}>
        <Col xs={24} sm={12} md={6}>
          <Card size="small">
            <Space direction="vertical" size={0} style={{ width: "100%" }}>
              <Text type="secondary" style={{ fontSize: 12 }}>
                <WarningOutlined /> Задолженность
              </Text>
              <div style={{ fontSize: 20, fontWeight: 600 }}>
                {summary && summary.overdue_amount_minor > 0 ? (
                  <Text type="danger">
                    {formatDebt(summary.overdue_amount_minor)}
                  </Text>
                ) : (
                  <Text type="success">{formatMoneyMinor(0)}</Text>
                )}
              </div>
              <Text type="secondary" style={{ fontSize: 12 }}>
                {summary?.overdue_terminal_count || 0} терм.
              </Text>
            </Space>
          </Card>
        </Col>
        {summary?.forecast.map((f) => (
          <Col xs={24} sm={12} md={6} key={f.month}>
            <Card size="small">
              <Space direction="vertical" size={0} style={{ width: "100%" }}>
                <Text type="secondary" style={{ fontSize: 12 }}>
                  <CalendarOutlined /> {formatForecastMonth(f.month)}
                </Text>
                <div style={{ fontSize: 20, fontWeight: 600 }}>
                  {formatMoneyMinor(f.amount_minor)}
                </div>
                <Text type="secondary" style={{ fontSize: 12 }}>
                  {f.terminal_count} терм.
                </Text>
              </Space>
            </Card>
          </Col>
        ))}
      </Row>

      {/* Counters */}
      <Space style={{ marginBottom: 8 }} size="middle">
        <Tag color="red">Просрочено: {overdueCount}</Tag>
        <Tag color="green">Активных: {activeCount}</Tag>
        <Tag color="blue">Отключение: {deactivationCount}</Tag>
        <Tag>Отключённых: {disabledCount}</Tag>
      </Space>

      {/* Terminal table with tabs */}
      <Card
        size="small"
        title="Терминалы"
        extra={
          <Space>
            <Input
              placeholder="Поиск..."
              prefix={<SearchOutlined />}
              value={search}
              onChange={(e) => setSearch(e.target.value)}
              style={{ width: 200 }}
              size="small"
            />
            <Button
              icon={<ReloadOutlined />}
              size="small"
              onClick={fetchData}
              loading={loading}
            />
          </Space>
        }
      >
        <Tabs
          activeKey={activeTab}
          onChange={setActiveTab}
          size="small"
          items={STATUS_TABS.map((tab) => ({
            key: tab.key,
            label: tab.label,
          }))}
        />
        <Table
          dataSource={filteredTerminals}
          columns={columns}
          rowKey="terminal_id"
          size="small"
          tableLayout="auto"
          pagination={false}
          scroll={{ x: 900 }}
        />
      </Card>

      {/* Deactivation confirmation modal */}
      <Modal
        open={deactivateModal.open}
        title="Отключение терминала"
        onCancel={() => setDeactivateModal({ open: false, terminal: null })}
        onOk={handleDeactivate}
        confirmLoading={confirmLoading}
        okText={isExpiredDeactivation ? "Отключить терминал" : "Отключить после окончания лицензии"}
        cancelText="Отмена"
        okButtonProps={{ danger: true }}
      >
        {deactivationTerminal && (
          <p>
            {isExpiredDeactivation
              ? "Терминал уже не имеет действующей лицензии. После отключения его расчётная сумма будет исключена из общей задолженности и прогнозов."
              : `Терминал продолжит работать до ${formatDate(deactivationTerminal.license_expires_at)}. После окончания оплаченного периода он будет заблокирован. Новые начисления производиться не будут, а терминал будет исключён из финансового прогноза.`}
          </p>
        )}
      </Modal>

      {/* Certificate PIN issuance modal */}
      <CertificatePinModal
        open={pinModal.open}
        terminal={pinModal.terminal}
        onClose={() => setPinModal({ open: false, terminal: null })}
        onIssued={fetchData}
      />
    </div>
  );
}
