import { useState, useEffect, useCallback, useMemo } from "react";
import {
  Card,
  Row,
  Col,
  Typography,
  Button,
  Tag,
  Progress,
  Alert,
  Tabs,
  Table,
  Modal,
  Form,
  InputNumber,
  Radio,
  Space,
  Badge,
  Spin,
  Statistic,
  message,
  Tooltip,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import {
  DollarOutlined,
  CheckCircleOutlined,
  WarningOutlined,
  StopOutlined,
  ClockCircleOutlined,
  CreditCardOutlined,
  ReloadOutlined,
  InfoCircleOutlined,
  ArrowUpOutlined,
  HistoryOutlined,
  CalendarOutlined,
  CalculatorOutlined,
} from "@ant-design/icons";
import {
  getTenantBalance,
  getTenantEntitlement,
  getTenantBillingProfile,
  listTenantDailyUsage,
  listTenantMonthlyCharges,
  listTenantTransactions,
  getCurrentTariff,
  createTopUpPayment,
  type FinBalance,
  type FinEntitlementStatus,
  type FinBillingProfile,
  type FinUsageDaily,
  type FinTerminalMonthlyCharge,
  type FinLedgerTransaction,
  type FinTariff,
} from "../../api/finance";
import { useSession } from "../../session/SessionContext";

const { Title, Text, Paragraph } = Typography;

export default function LicensesPage() {
  const { user } = useSession();
  const [loading, setLoading] = useState(true);
  const [balance, setBalance] = useState<FinBalance | null>(null);
  const [entitlement, setEntitlement] = useState<FinEntitlementStatus | null>(null);
  const [profile, setProfile] = useState<FinBillingProfile | null>(null);
  const [tariff, setTariff] = useState<FinTariff | null>(null);

  // Detail tables
  const [dailyUsage, setDailyUsage] = useState<FinUsageDaily[]>([]);
  const [monthlyCharges, setMonthlyCharges] = useState<FinTerminalMonthlyCharge[]>([]);
  const [transactions, setTransactions] = useState<FinLedgerTransaction[]>([]);
  const [tablesLoading, setTablesLoading] = useState(false);

  // Top-up Modal
  const [topUpModalOpen, setTopUpModalOpen] = useState(false);
  const [topUpAmount, setTopUpAmount] = useState<number>(1000);
  const [topUpSubmitting, setTopUpSubmitting] = useState(false);

  const fetchOverview = useCallback(async () => {
    setLoading(true);
    try {
      const [balRes, entRes, profRes, tarRes] = await Promise.all([
        getTenantBalance().catch(() => null),
        getTenantEntitlement().catch(() => null),
        getTenantBillingProfile().catch(() => null),
        getCurrentTariff().catch(() => null),
      ]);
      setBalance(balRes);
      setEntitlement(entRes);
      setProfile(profRes);
      setTariff(tarRes);
    } catch {
      // quiet fallback
    } finally {
      setLoading(false);
    }
  }, []);

  const fetchTables = useCallback(async () => {
    setTablesLoading(true);
    try {
      const [usageRes, chargesRes, txRes] = await Promise.all([
        listTenantDailyUsage({ limit: 50 }).catch(() => []),
        listTenantMonthlyCharges(undefined, 50).catch(() => []),
        listTenantTransactions(50, 0).catch(() => []),
      ]);
      setDailyUsage(usageRes);
      setMonthlyCharges(chargesRes);
      setTransactions(txRes);
    } catch {
      // quiet fallback
    } finally {
      setTablesLoading(false);
    }
  }, []);

  useEffect(() => {
    void fetchOverview();
    void fetchTables();
  }, [fetchOverview, fetchTables]);

  const handleTopUpSubmit = async () => {
    if (!topUpAmount || topUpAmount < 100) {
      message.warning("Минимальная сумма пополнения — 100 ₽");
      return;
    }
    setTopUpSubmitting(true);
    try {
      const payment = await createTopUpPayment({
        amount_rubles: topUpAmount,
        return_url: window.location.href,
      });
      message.success("Платёж создан. Перенаправление на оплату...");
      setTopUpModalOpen(false);
      if (payment.confirmation_url) {
        window.location.href = payment.confirmation_url;
      } else {
        void fetchOverview();
      }
    } catch (err: any) {
      message.error(err.response?.data?.detail || "Ошибка при создании платежа");
    } finally {
      setTopUpSubmitting(false);
    }
  };

  // Calculations for today's pooled usage (120 minutes free)
  const freeQuotaSec = entitlement?.free_quota_seconds || 7200; // 120 mins
  const todayUsedSec = entitlement?.today_usage_seconds || 0;
  const usedMinutes = Math.floor(todayUsedSec / 60);
  const freeLimitMinutes = Math.floor(freeQuotaSec / 60);
  const percentUsed = Math.min(100, Math.round((todayUsedSec / freeQuotaSec) * 100));
  const hasPositiveBalance = (balance?.balance_rubles || 0) > 0;
  const isGrace = entitlement?.entitlement === "grace";
  const isBlocked = entitlement?.entitlement === "blocked";

  // Remaining grace calculation
  const remainingGraceText = useMemo(() => {
    if (!isGrace || !entitlement?.grace_deadline) return null;
    const deadlineMs = new Date(entitlement.grace_deadline).getTime();
    const diffMs = deadlineMs - Date.now();
    if (diffMs <= 0) return "Grace-период истёк";
    const hours = Math.floor(diffMs / (1000 * 60 * 60));
    const days = Math.floor(hours / 24);
    const remHours = hours % 24;
    return days > 0 ? `${days} дн. ${remHours} ч.` : `${hours} ч.`;
  }, [isGrace, entitlement?.grace_deadline]);

  // Next renewal date
  const nextRenewalDate = useMemo(() => {
    const rawDate = entitlement?.cycle_ends_at;
    if (!rawDate) return "По окончании цикла";
    try {
      return new Date(rawDate).toLocaleDateString("ru-RU", {
        day: "numeric",
        month: "long",
        year: "numeric",
      });
    } catch {
      return rawDate;
    }
  }, [entitlement?.cycle_ends_at]);

  // Daily Usage table columns
  const usageColumns: ColumnsType<FinUsageDaily> = [
    {
      title: "Дата",
      dataIndex: "local_date",
      key: "local_date",
      render: (val: string) => <Text strong>{val}</Text>,
    },
    {
      title: "Терминал",
      dataIndex: "terminal_id",
      key: "terminal_id",
      render: (id: number) => <Tag color="blue">#{id}</Tag>,
    },
    {
      title: "Консоль",
      dataIndex: "console_seconds",
      key: "console_seconds",
      render: (sec: number) => `${Math.round(sec / 60)} мин`,
    },
    {
      title: "Видео",
      dataIndex: "video_seconds",
      key: "video_seconds",
      render: (sec: number) => `${Math.round(sec / 60)} мин`,
    },
    {
      title: "Всего время",
      dataIndex: "total_seconds",
      key: "total_seconds",
      render: (sec: number) => (
        <Text strong style={{ color: sec > 7200 ? "#cf1322" : undefined }}>
          {Math.round(sec / 60)} мин
        </Text>
      ),
    },
    {
      title: "Бесплатно",
      dataIndex: "free_seconds",
      key: "free_seconds",
      render: (sec: number) => `${Math.round(sec / 60)} мин`,
    },
    {
      title: "Платно",
      dataIndex: "chargeable_seconds",
      key: "chargeable_seconds",
      render: (sec: number) => (
        <span style={{ color: sec > 0 ? "#d46b08" : undefined }}>
          {Math.round(sec / 60)} мин
        </span>
      ),
    },
    {
      title: "Сумма списания",
      dataIndex: "charged_amount_kopecks",
      key: "charged_amount_kopecks",
      render: (kop: number) => (
        <Text strong style={{ color: kop > 0 ? "#cf1322" : "#389e0d" }}>
          {kop > 0 ? `-${(kop / 100).toFixed(2)} ₽` : "0 ₽"}
        </Text>
      ),
    },
    {
      title: "Статус",
      dataIndex: "status",
      key: "status",
      render: (st: string) => (
        <Tag color={st === "posted" ? "success" : "processing"}>
          {st === "posted" ? "Проведено" : "Открыто"}
        </Tag>
      ),
    },
  ];

  // Monthly charges columns
  const monthlyColumns: ColumnsType<FinTerminalMonthlyCharge> = [
    {
      title: "Терминал",
      dataIndex: "terminal_id",
      key: "terminal_id",
      render: (id: number) => <Tag color="blue">#{id}</Tag>,
    },
    {
      title: "Платёжный цикл",
      dataIndex: "billing_cycle_id",
      key: "billing_cycle_id",
      render: (cid: number) => `Цикл #${cid}`,
    },
    {
      title: "Сумма тарифа",
      dataIndex: "amount_kopecks",
      key: "amount_kopecks",
      render: (kop: number) => (
        <Text strong style={{ color: kop === 0 ? "#52c41a" : undefined }}>
          {kop === 0 ? "0 ₽ (Льготный первый терминал)" : `${(kop / 100).toFixed(2)} ₽`}
        </Text>
      ),
    },
    {
      title: "Дата проведения",
      dataIndex: "posted_at",
      key: "posted_at",
      render: (dt: string) => (dt ? new Date(dt).toLocaleString("ru-RU") : "—"),
    },
    {
      title: "Статус",
      dataIndex: "status",
      key: "status",
      render: (st: string) => (
        <Tag color={st === "posted" ? "success" : "default"}>
          {st === "posted" ? "Списано" : st}
        </Tag>
      ),
    },
  ];

  // Transactions columns
  const transactionColumns: ColumnsType<FinLedgerTransaction> = [
    {
      title: "№ / Дата",
      dataIndex: "posted_at",
      key: "posted_at",
      render: (dt: string, rec: FinLedgerTransaction) => (
        <div>
          <Text strong>#{rec.id}</Text>
          <div style={{ fontSize: 12, color: "#8c8c8c" }}>
            {dt ? new Date(dt).toLocaleString("ru-RU") : "—"}
          </div>
        </div>
      ),
    },
    {
      title: "Тип операции",
      dataIndex: "kind",
      key: "kind",
      render: (kind: string) => {
        const labels: Record<string, string> = {
          initial_grant: "Начальное начисление",
          monthly_subscription: "Ежемесячная абонплата",
          usage_charge: "Поминутная тарификация",
          topup_yookassa: "Пополнение через ЮKassa",
          topup_manual: "Банковский перевод",
          reversal: "Сторно / Корректировка",
        };
        return <Tag color="purple">{labels[kind] || kind}</Tag>;
      },
    },
    {
      title: "Сумма",
      key: "amount",
      render: (_: any, rec: FinLedgerTransaction) => {
        if (rec.credit_kopecks > 0) {
          return (
            <Text strong style={{ color: "#389e0d" }}>
              +{(rec.credit_kopecks / 100).toFixed(2)} ₽
            </Text>
          );
        }
        return (
          <Text strong style={{ color: "#cf1322" }}>
            -{(rec.debit_kopecks / 100).toFixed(2)} ₽
          </Text>
        );
      },
    },
    {
      title: "Детали расчёта",
      dataIndex: "calculation_snapshot",
      key: "calculation_snapshot",
      render: (snap: any) => {
        if (!snap) return <Text type="secondary">—</Text>;
        if (snap.chargeable_seconds !== undefined) {
          return (
            <Text type="secondary" style={{ fontSize: 12 }}>
              {Math.round(snap.chargeable_seconds / 60)} платных мин по тарифу{" "}
              {snap.hourly_rate_rubles || 90} ₽/час
            </Text>
          );
        }
        if (snap.description) {
          return <Text type="secondary">{snap.description}</Text>;
        }
        return <Text type="secondary">{JSON.stringify(snap)}</Text>;
      },
    },
    {
      title: "Статус",
      dataIndex: "status",
      key: "status",
      render: (st: string) => (
        <Tag color={st === "posted" ? "success" : "default"}>
          {st === "posted" ? "Проведено" : st}
        </Tag>
      ),
    },
  ];

  return (
    <div style={{ maxWidth: 1400, margin: "0 auto", paddingBottom: 40 }}>
      {/* Header bar */}
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
          <Title level={3} style={{ margin: 0 }}>
            Лицензии и баланс
          </Title>
          <Text type="secondary" style={{ fontSize: 14 }}>
            Коммерческие условия, квоты потребления и финансовый журнал операций L4Desk
          </Text>
        </div>

        <Space>
          <Button icon={<ReloadOutlined spin={loading} />} onClick={fetchOverview}>
            Обновить
          </Button>
          <Button
            type="primary"
            icon={<DollarOutlined />}
            size="large"
            onClick={() => setTopUpModalOpen(true)}
          >
            Пополнить баланс
          </Button>
        </Space>
      </div>

      {/* Critical Status Alerts */}
      {isGrace && (
        <Alert
          type="warning"
          showIcon
          icon={<WarningOutlined />}
          style={{ marginBottom: 16, borderRadius: 8 }}
          message={
            <Text strong style={{ fontSize: 14 }}>
              Действует льготный grace-период оплаты (осталось: {remainingGraceText || "несколько дней"})
            </Text>
          }
          description="Платёжный цикл завершился, баланс организации недостаточен для автоматического продления. Пополните баланс лицевого счёта, чтобы избежать приостановки обслуживания."
          action={
            <Button
              type="primary"
              size="small"
              icon={<DollarOutlined />}
              onClick={() => setTopUpModalOpen(true)}
            >
              Пополнить сейчас
            </Button>
          }
        />
      )}

      {isBlocked && (
        <Alert
          type="error"
          showIcon
          icon={<StopOutlined />}
          style={{ marginBottom: 16, borderRadius: 8 }}
          message={
            <Text strong style={{ fontSize: 14 }}>
              Обслуживание приостановлено: задолженность по лицевому счёту
            </Text>
          }
          description="Запуск новых удалённых сессий консоли и видеонаблюдения заблокирован. Для разблокировки пополните баланс на сумму продления подписки."
          action={
            <Button
              type="primary"
              danger
              size="small"
              icon={<DollarOutlined />}
              onClick={() => setTopUpModalOpen(true)}
            >
              Погасить задолженность
            </Button>
          }
        />
      )}

      {/* Overview Cards Row */}
      <Row gutter={[16, 16]} style={{ marginBottom: 24 }}>
        {/* Card 1: Balance */}
        <Col xs={24} md={8}>
          <Card
            style={{ height: "100%", borderRadius: 8 }}
            styles={{ body: { padding: 20 } }}
          >
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start" }}>
              <Text type="secondary" style={{ fontSize: 13, textTransform: "uppercase", letterSpacing: 0.5 }}>
                Баланс счёта
              </Text>
              <DollarOutlined style={{ fontSize: 22, color: "#1677ff" }} />
            </div>

            <div style={{ margin: "12px 0 8px 0" }}>
              <span style={{ fontSize: 32, fontWeight: 700, color: hasPositiveBalance ? "#389e0d" : "#262626" }}>
                {balance ? `${balance.balance_rubles.toLocaleString("ru-RU", { minimumFractionDigits: 2, maximumFractionDigits: 2 })} ₽` : "0,00 ₽"}
              </span>
            </div>

            {hasPositiveBalance ? (
              <Tag color="success" style={{ fontSize: 12, padding: "2px 8px", whiteSpace: "normal" }}>
                ✓ Положительный баланс явно разрешает платное продолжение сессий после 120 минут
              </Tag>
            ) : (
              <Tag color="warning" style={{ fontSize: 12, padding: "2px 8px", whiteSpace: "normal" }}>
                Нулевой баланс: продолжение сессий сверх 120 мин/день ограничено
              </Tag>
            )}

            <div style={{ marginTop: 16 }}>
              <Button
                type="dashed"
                block
                icon={<ArrowUpOutlined />}
                onClick={() => setTopUpModalOpen(true)}
              >
                Пополнить баланс
              </Button>
            </div>
          </Card>
        </Col>

        {/* Card 2: Entitlement & Cycle Renewal */}
        <Col xs={24} md={8}>
          <Card
            style={{ height: "100%", borderRadius: 8 }}
            styles={{ body: { padding: 20 } }}
          >
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start" }}>
              <Text type="secondary" style={{ fontSize: 13, textTransform: "uppercase", letterSpacing: 0.5 }}>
                Статус подписки
              </Text>
              <CalendarOutlined style={{ fontSize: 22, color: "#52c41a" }} />
            </div>

            <div style={{ margin: "12px 0 8px 0" }}>
              {entitlement?.entitlement === "active" && (
                <Tag color="success" style={{ fontSize: 14, padding: "4px 10px", fontWeight: 600 }}>
                  ● Подписка активна
                </Tag>
              )}
              {entitlement?.entitlement === "free" && (
                <Tag color="processing" style={{ fontSize: 14, padding: "4px 10px", fontWeight: 600 }}>
                  ● Бесплатный тариф
                </Tag>
              )}
              {entitlement?.entitlement === "grace" && (
                <Tag color="warning" style={{ fontSize: 14, padding: "4px 10px", fontWeight: 600 }}>
                  ● Grace-период
                </Tag>
              )}
              {entitlement?.entitlement === "blocked" && (
                <Tag color="error" style={{ fontSize: 14, padding: "4px 10px", fontWeight: 600 }}>
                  ● Заблокировано
                </Tag>
              )}
            </div>

            <div style={{ fontSize: 13, color: "#595959", marginTop: 8 }}>
              Следующее продление: <strong>{nextRenewalDate}</strong>
            </div>

            <div style={{ fontSize: 12, color: "#8c8c8c", marginTop: 4 }}>
              Платёжный якорь: <strong>{profile?.billing_anchor_day || 1}-е число месяца</strong>
            </div>

            <div style={{ marginTop: 14, fontSize: 12, color: "#595959" }}>
              Тариф: Первый терминал — <strong>0 ₽/мес</strong>. Дополнительные —{" "}
              <strong>{((tariff?.additional_terminal_monthly_kopecks || 49000) / 100).toFixed(0)} ₽/мес</strong>.
            </div>
          </Card>
        </Col>

        {/* Card 3: Today Pooled Free Quota (120 mins) */}
        <Col xs={24} md={8}>
          <Card
            style={{ height: "100%", borderRadius: 8 }}
            styles={{ body: { padding: 20 } }}
          >
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start" }}>
              <Text type="secondary" style={{ fontSize: 13, textTransform: "uppercase", letterSpacing: 0.5 }}>
                Бесплатная квота сегодня
              </Text>
              <ClockCircleOutlined style={{ fontSize: 22, color: "#faad14" }} />
            </div>

            <div style={{ margin: "12px 0 6px 0" }}>
              <span style={{ fontSize: 26, fontWeight: 700 }}>
                {usedMinutes} / {freeLimitMinutes} мин
              </span>
              <span style={{ fontSize: 13, color: "#8c8c8c", marginLeft: 8 }}>
                ({percentUsed}%)
              </span>
            </div>

            <Progress
              percent={percentUsed}
              status={percentUsed >= 100 ? "exception" : percentUsed > 75 ? "active" : "normal"}
              strokeColor={percentUsed >= 100 ? "#ff4d4f" : percentUsed > 75 ? "#faad14" : "#1677ff"}
            />

            <div style={{ fontSize: 12, color: "#595959", marginTop: 8 }}>
              Суммарная квота на видео и консоль. Сверх 120 мин/день тарифицируется по{" "}
              <strong>{((tariff?.usage_hourly_kopecks || 9000) / 6000).toFixed(2)} ₽/мин</strong>{" "}
              (1,50 ₽/мин).
            </div>
          </Card>
        </Col>
      </Row>

      {/* Transparent Detailed Records Tabs */}
      <Card style={{ borderRadius: 8 }}>
        <Tabs
          defaultActiveKey="usage"
          items={[
            {
              key: "usage",
              label: (
                <span>
                  <CalculatorOutlined /> Потребление по дням ({dailyUsage.length})
                </span>
              ),
              children: (
                <Table
                  dataSource={dailyUsage}
                  columns={usageColumns}
                  rowKey="id"
                  loading={tablesLoading}
                  pagination={{ pageSize: 10 }}
                  size="small"
                />
              ),
            },
            {
              key: "charges",
              label: (
                <span>
                  <CalendarOutlined /> Ежемесячные списания ({monthlyCharges.length})
                </span>
              ),
              children: (
                <Table
                  dataSource={monthlyCharges}
                  columns={monthlyColumns}
                  rowKey="id"
                  loading={tablesLoading}
                  pagination={{ pageSize: 10 }}
                  size="small"
                />
              ),
            },
            {
              key: "transactions",
              label: (
                <span>
                  <HistoryOutlined /> Финансовые проводки ({transactions.length})
                </span>
              ),
              children: (
                <Table
                  dataSource={transactions}
                  columns={transactionColumns}
                  rowKey="id"
                  loading={tablesLoading}
                  pagination={{ pageSize: 10 }}
                  size="small"
                />
              ),
            },
          ]}
        />
      </Card>

      {/* Top-up Modal */}
      <Modal
        title={
          <Space>
            <CreditCardOutlined style={{ color: "#1677ff" }} />
            <span>Пополнение баланса организации</span>
          </Space>
        }
        open={topUpModalOpen}
        onCancel={() => setTopUpModalOpen(false)}
        footer={null}
        destroyOnClose
      >
        <div style={{ marginTop: 12 }}>
          <Paragraph type="secondary" style={{ fontSize: 13 }}>
            Пополнение баланса позволяет подключать дополнительные терминалы и продолжать сессии
            управления сверх бесплатного лимита 120 минут в день.
          </Paragraph>

          <Form layout="vertical" onFinish={handleTopUpSubmit}>
            <Form.Item label="Выберите сумму пополнения">
              <Radio.Group
                value={topUpAmount}
                onChange={(e) => setTopUpAmount(e.target.value)}
                style={{ width: "100%", marginBottom: 12 }}
              >
                <Space direction="vertical" style={{ width: "100%" }}>
                  <Radio value={500}>500 ₽</Radio>
                  <Radio value={1000}>1 000 ₽ (Рекомендуется)</Radio>
                  <Radio value={3000}>3 000 ₽ (С запасом на 3 месяца)</Radio>
                  <Radio value={5000}>5 000 ₽</Radio>
                </Space>
              </Radio.Group>
            </Form.Item>

            <Form.Item label="Или введите другую сумму (в рублях)">
              <InputNumber
                style={{ width: "100%" }}
                min={100}
                max={500000}
                step={100}
                value={topUpAmount}
                onChange={(val) => setTopUpAmount(val || 100)}
                addonAfter="₽"
              />
            </Form.Item>

            <Alert
              type="info"
              showIcon
              style={{ marginBottom: 16 }}
              message="Безопасная оплата через ЮKassa"
              description="После нажатия кнопки вы будете перенаправлены на защищённую платёжную страницу ЮKassa для ввода данных банковской карты или СБП."
            />

            <div style={{ textAlign: "right" }}>
              <Space>
                <Button onClick={() => setTopUpModalOpen(false)}>Отмена</Button>
                <Button
                  type="primary"
                  htmlType="submit"
                  size="large"
                  loading={topUpSubmitting}
                  icon={<DollarOutlined />}
                >
                  Оплатить {topUpAmount} ₽
                </Button>
              </Space>
            </div>
          </Form>
        </div>
      </Modal>
    </div>
  );
}
