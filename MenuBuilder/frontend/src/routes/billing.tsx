import { useEffect, useState, useCallback, useMemo } from "react";
import {
  Card,
  Checkbox,
  Col,
  Row,
  Table,
  Tag,
  Button,
  Segmented,
  Space,
  Modal,
  Tabs,
  Input,
  Spin,
  Tooltip,
  message,
  Typography,
} from "antd";
import {
  WarningOutlined,
  CalendarOutlined,
  ReloadOutlined,
  SearchOutlined,
  SafetyCertificateOutlined,
  ShoppingCartOutlined,
} from "@ant-design/icons";
import type { ColumnsType } from "antd/es/table";
import {
  getBillingSummary,
  getBillingTerminals,
  deactivateTerminal,
  cancelDeactivation,
  type BillingSummary,
  type BillingTerminal,
} from "../api/billing";
import CertificatePinModal from "../components/CertificatePinModal";
import CheckoutModal, { type CartLine } from "../components/CheckoutModal";
import PageHeader from "../components/PageHeader";
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
  { key: "all", label: "Все" },
  { key: "attention", label: "Требуют внимания" },
  { key: "active", label: "Активные" },
  { key: "deactivation", label: "Отключение запланировано" },
  { key: "disabled", label: "Отключённые" },
];

interface Selection {
  license: boolean;
  cert: boolean;
}

const LICENSE_DUE_SOON_DAYS = 30;

/** A license that has expired or was never issued restarts from today. */
function isLicenseLapsed(t: BillingTerminal): boolean {
  return !t.license_expires_at || new Date(t.license_expires_at) <= new Date();
}

/** Days remaining until a license expires (negative once it has lapsed). */
function daysUntilLicenseExpiry(t: BillingTerminal): number {
  if (!t.license_expires_at) return -Infinity;
  return (
    (new Date(t.license_expires_at).getTime() - Date.now()) / 86400000
  );
}

/** Whether the terminal is disabled in licenses (or admin disabled) and excluded from payments. */
function isTerminalDisabled(t: BillingTerminal): boolean {
  return (
    t.billing_status === "disabled" ||
    t.billing_status === "admin_disabled" ||
    t.billing_status === "no_license"
  );
}

/** Lapsed, or expiring soon enough that it needs attention right away. */
function isLicenseUrgent(t: BillingTerminal): boolean {
  return daysUntilLicenseExpiry(t) <= LICENSE_DUE_SOON_DAYS;
}

/** Whether a license payment can be made for this terminal right now. */
function isLicensePayable(t: BillingTerminal, advancePeriods: number): boolean {
  if (isTerminalDisabled(t)) return false;
  return isLicenseLapsed(t) || advancePeriods > 0;
}

/**
 * Whether the license line should be pre-checked by default: always for
 * anything urgent (lapsed or expiring within a month), and additionally for
 * anything whose expiry falls inside the chosen advance-payment horizon.
 */
function shouldAutoSelectLicense(
  t: BillingTerminal,
  advancePeriods: number,
): boolean {
  if (isTerminalDisabled(t)) return false;
  if (isLicenseUrgent(t)) return true;
  if (advancePeriods > 0) {
    const horizonDays = advancePeriods * (t.billing_period_months || 1) * 30;
    return daysUntilLicenseExpiry(t) <= horizonDays;
  }
  return false;
}

/**
 * Whether a paid certificate PIN can be bought for this terminal.
 *
 * A pending (paid, not-yet-installed) PIN already resolves the certificate
 * renewal question, so it must not be offered again until it is used or
 * expires — paying for a PIN twice makes no sense.
 */
function isCertPayable(t: BillingTerminal): boolean {
  if (isTerminalDisabled(t)) return false;
  return (
    t.tenant_pin_creation_enabled &&
    t.cert_pin_price_minor > 0 &&
    !t.cert_pin_pending
  );
}

function licenseAmountMinor(t: BillingTerminal, advancePeriods: number): number {
  const periods = (isLicenseLapsed(t) ? 1 : 0) + advancePeriods;
  return periods * t.period_price_minor;
}

/** Date the license will run until once the selected periods are paid. */
function projectedExpiry(
  t: BillingTerminal,
  advancePeriods: number,
): string | null {
  const periods = (isLicenseLapsed(t) ? 1 : 0) + advancePeriods;
  if (periods === 0) return null;
  const anchor = isLicenseLapsed(t)
    ? new Date()
    : new Date(t.license_expires_at as string);
  const projected = new Date(anchor);
  projected.setMonth(projected.getMonth() + periods * t.billing_period_months);
  return projected.toISOString();
}


/**
 * The org's prevailing tariff period, used to label the advance-payment
 * control in months instead of abstract "+1 / +2".
 */
function dominantPeriodMonths(terminals: BillingTerminal[]): number {
  const counts = new Map<number, number>();
  for (const t of terminals) {
    const months = t.billing_period_months || 1;
    counts.set(months, (counts.get(months) ?? 0) + 1);
  }
  let best = 1;
  let bestCount = 0;
  for (const [months, count] of counts) {
    if (count > bestCount) {
      best = months;
      bestCount = count;
    }
  }
  return best;
}

/** "3 мес" / "6 мес" — short label used on the Segmented control. */
function monthsLabel(months: number): string {
  return `${months} мес`;
}

export default function BillingPage() {
  const [summary, setSummary] = useState<BillingSummary | null>(null);
  const [terminals, setTerminals] = useState<BillingTerminal[]>([]);
  const [loading, setLoading] = useState(true);
  const [search, setSearch] = useState("");
  const [activeTab, setActiveTab] = useState("all");
  const [advancePeriods, setAdvancePeriods] = useState(0);
  const [selection, setSelection] = useState<Record<number, Selection>>({});
  const [deactivateModal, setDeactivateModal] = useState<{
    open: boolean;
    terminal: BillingTerminal | null;
  }>({ open: false, terminal: null });
  const [confirmLoading, setConfirmLoading] = useState(false);
  const [pinModal, setPinModal] = useState<{
    open: boolean;
    terminal: BillingTerminal | null;
  }>({ open: false, terminal: null });
  const [checkoutOpen, setCheckoutOpen] = useState(false);

  const fetchData = useCallback(async () => {
    try {
      setLoading(true);
      const [s, t] = await Promise.all([
        getBillingSummary(),
        getBillingTerminals(),
      ]);
      setSummary(s);
      setTerminals(t);
      // Preselect certificates that are missing or about to expire; license
      // selection is derived separately below (it also depends on advancePeriods).
      setSelection((prev) =>
        Object.fromEntries(
          t.map((term) => [
            term.terminal_id,
            {
              license: Boolean(prev[term.terminal_id]?.license),
              cert: isCertPayable(term) && term.cert_expiring_soon,
            },
          ]),
        ),
      );
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

  // Auto-select license lines: anything urgent (lapsed or due within a
  // month) is always checked; anything else whose expiry falls inside the
  // chosen advance-payment horizon gets checked too. Certificate selections
  // are independent and left untouched here.
  useEffect(() => {
    setSelection((prev) => {
      let changed = false;
      const next: Record<number, Selection> = { ...prev };
      for (const t of terminals) {
        const auto = shouldAutoSelectLicense(t, advancePeriods);
        const current = next[t.terminal_id] ?? { license: false, cert: false };
        if (current.license !== auto) {
          next[t.terminal_id] = { ...current, license: auto };
          changed = true;
        }
      }
      return changed ? next : prev;
    });
  }, [terminals, advancePeriods]);

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

  const toggle = (terminalId: number, field: keyof Selection, on: boolean) => {
    setSelection((prev) => ({
      ...prev,
      [terminalId]: { ...prev[terminalId], [field]: on },
    }));
  };

  // Filter terminals by tab and search
  const filteredTerminals = terminals.filter((t) => {
    if (search) {
      const s = search.toLowerCase();
      if (!String(t.device_id).includes(s) && !t.sn.toLowerCase().includes(s)) {
        return false;
      }
    }

    switch (activeTab) {
      case "all":
        return true;
      case "attention":
        return (
          t.billing_status === "overdue" ||
          t.billing_status === "due_soon" ||
          t.billing_status === "no_license"
        );
      case "active":
        return t.billing_status === "active" || t.billing_status === "due_soon";
      case "deactivation":
        return t.billing_status === "deactivation_scheduled";
      case "disabled":
        return (
          t.billing_status === "disabled" ||
          t.billing_status === "admin_disabled" ||
          t.billing_status === "no_license"
        );
      default:
        return true;
    }
  });

  // Cart lines are built from every terminal, not just the visible tab, so
  // switching tabs never silently drops something the user already selected.
  const cartLines: CartLine[] = useMemo(() => {
    return terminals
      .map((t) => {
        const sel = selection[t.terminal_id];
        const license =
          Boolean(sel?.license) && isLicensePayable(t, advancePeriods);
        const cert = Boolean(sel?.cert) && isCertPayable(t);
        if (!license && !cert) return null;
        return {
          terminal: t,
          license,
          cert,
          licenseAmountMinor: license
            ? licenseAmountMinor(t, advancePeriods)
            : 0,
          certAmountMinor: cert ? t.cert_pin_price_minor : 0,
          newExpiresAt: license ? projectedExpiry(t, advancePeriods) : null,
        };
      })
      .filter((l): l is CartLine => l !== null);
  }, [terminals, selection, advancePeriods]);

  const totalMinor = cartLines.reduce(
    (sum, l) => sum + l.licenseAmountMinor + l.certAmountMinor,
    0,
  );
  const licenseCount = cartLines.filter((l) => l.license).length;
  const certCount = cartLines.filter((l) => l.cert).length;

  const setAllVisible = (on: boolean) => {
    setSelection((prev) => {
      const next = { ...prev };
      for (const t of filteredTerminals) {
        next[t.terminal_id] = {
          license: on && isLicensePayable(t, advancePeriods),
          cert: on && isCertPayable(t),
        };
      }
      return next;
    });
  };

  // Advance-payment control: labelled in months, derived from the org tariff.
  const periodMonths = dominantPeriodMonths(terminals);
  const hasDebt = terminals.some(
    (t) => !isTerminalDisabled(t) && isLicenseLapsed(t),
  );
  // A license expiring within a month needs at least one paid period right
  // away, which "Только задолженность" (0 periods) cannot cover — so that
  // option is only offered when nothing is that urgent.
  const hasUrgentDueSoon = terminals.some(
    (t) =>
      !isTerminalDisabled(t) &&
      !isLicenseLapsed(t) &&
      isLicenseUrgent(t),
  );
  const showDebtOnlyOption = hasDebt && !hasUrgentDueSoon;
  const advanceOptions = [
    ...(showDebtOnlyOption ? [{ label: "Только задолженность", value: 0 }] : []),
    { label: monthsLabel(periodMonths), value: 1 },
    { label: monthsLabel(periodMonths * 2), value: 2 },
  ];

  // "Только задолженность" isn't always offered, so the selection has to
  // fall back to the first real advance period whenever it disappears.
  useEffect(() => {
    if (!loading && !showDebtOnlyOption && advancePeriods === 0) {
      setAdvancePeriods(1);
    }
  }, [loading, showDebtOnlyOption, advancePeriods]);


  // Counters
  const overdueCount = terminals.filter(
    (t) => t.billing_status === "overdue",
  ).length;
  const activeCount = terminals.filter(
    (t) => t.billing_status === "active" || t.billing_status === "due_soon",
  ).length;
  const deactivationCount = terminals.filter(
    (t) => t.billing_status === "deactivation_scheduled",
  ).length;
  const disabledCount = terminals.filter(
    (t) =>
      t.billing_status === "disabled" || t.billing_status === "admin_disabled",
  ).length;

  const columns: ColumnsType<BillingTerminal> = [
    {
      title: "Терминал",
      dataIndex: "device_id",
      key: "device_id",
      width: 100,
      sorter: (a, b) => a.device_id - b.device_id,
    },
    {
      title: "Тип",
      dataIndex: "terminal_type_name",
      key: "terminal_type",
      width: 140,
      render: (v: string | null, r: BillingTerminal) => (
        <Text style={{ fontSize: 12 }}>
          {v || (r.terminal_type_id !== undefined ? `Тип ${r.terminal_type_id}` : "—")}
        </Text>
      ),
    },
    {
      title: "Адрес",
      dataIndex: "address",
      key: "address",
      width: 180,
      ellipsis: true,
      render: (v: string | null) => (
        v ? (
          <Tooltip title={v}>
            <Text style={{ fontSize: 12 }}>{v}</Text>
          </Tooltip>
        ) : (
          <Text type="secondary" style={{ fontSize: 12 }}>
            —
          </Text>
        )
      ),
    },
    {
      title: "Статус",
      dataIndex: "billing_status",
      key: "status",
      width: 150,
      render: (status: string) => (
        <Tag color={billingStatusColor(status)}>
          {billingStatusLabel(status)}
        </Tag>
      ),
    },
    {
      title: "Лицензия",
      key: "license",
      width: 215,
      render: (_: unknown, r: BillingTerminal) => {
        const payable = isLicensePayable(r, advancePeriods);
        const lapsed = isLicenseLapsed(r);
        const checked = Boolean(selection[r.terminal_id]?.license) && payable;
        const checkbox = (
          <Checkbox
            checked={checked}
            disabled={!payable}
            onChange={(e) => toggle(r.terminal_id, "license", e.target.checked)}
          >
            <Space direction="vertical" size={0}>
              <Text type={lapsed ? "danger" : undefined}>
                {r.license_expires_at
                  ? formatDate(r.license_expires_at)
                  : "нет лицензии"}
              </Text>
              {payable && (
                <Text type="secondary" style={{ fontSize: 12 }}>
                  {formatMoneyMinor(licenseAmountMinor(r, advancePeriods))}
                  {checked &&
                    ` → ${formatDate(projectedExpiry(r, advancePeriods))}`}
                </Text>
              )}
            </Space>
          </Checkbox>
        );
        return payable ? (
          checkbox
        ) : (
          <Tooltip title="Лицензия действует — выберите «Оплатить вперёд», чтобы продлить заранее">
            {checkbox}
          </Tooltip>
        );
      },
    },
    {
      title: "Сертификат",
      key: "cert",
      width: 250,
      render: (_: unknown, r: BillingTerminal) => {
        const pinButton = r.tenant_pin_creation_enabled ? (
          <Tooltip
            title={
              r.cert_pin_pending
                ? "PIN уже оплачен — открыть и посмотреть код"
                : "Запросить PIN отдельно, не добавляя в общий счёт"
            }
          >
            <Button
              size="small"
              type="text"
              icon={<SafetyCertificateOutlined />}
              onClick={() => setPinModal({ open: true, terminal: r })}
            />
          </Tooltip>
        ) : null;

        // A paid PIN awaiting installation already resolves the renewal —
        // show that instead of the (now stale) plain expiry date.
        if (r.cert_pin_pending) {
          return (
            <Space size={4} align="start">
              <Space direction="vertical" size={0}>
                <Text type="secondary" style={{ fontSize: 12 }}>
                  Текущий до{" "}
                  {r.cert_not_valid_after
                    ? formatDate(r.cert_not_valid_after)
                    : "—"}
                </Text>
                <Text type="warning">Новый ждёт установки</Text>
                <Text type="success" style={{ fontSize: 12 }}>
                  Оплачено
                </Text>
              </Space>
              {pinButton}
            </Space>
          );
        }

        const label = !r.cert_serial ? (
          <Text type="warning">не выпущен</Text>
        ) : !r.cert_not_valid_after ? (
          <Text type="secondary">выпущен (дата неизвестна)</Text>
        ) : (
          <Text
            type={
              new Date(r.cert_not_valid_after) < new Date()
                ? "danger"
                : r.cert_expiring_soon
                  ? "warning"
                  : "secondary"
            }
          >
            {formatDate(r.cert_not_valid_after)}
          </Text>
        );

        if (!isCertPayable(r))
          return (
            <Space size={4} align="start">
              {label}
              {pinButton}
            </Space>
          );

        return (
          <Space size={4} align="start">
            <Checkbox
              checked={Boolean(selection[r.terminal_id]?.cert)}
              onChange={(e) => toggle(r.terminal_id, "cert", e.target.checked)}
            >
              <Space direction="vertical" size={0}>
                {label}
                <Text type="secondary" style={{ fontSize: 12 }}>
                  {formatMoneyMinor(r.cert_pin_price_minor)}
                </Text>
              </Space>
            </Checkbox>
            {pinButton}
          </Space>
        );
      },
    },
    {
      title: "Тариф",
      dataIndex: "monthly_price_minor",
      key: "tariff",
      align: "right",
      width: 130,
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
      width: 130,
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
      title: "Действия",
      key: "actions",
      width: 190,
      render: (_: unknown, r: BillingTerminal) => (
        <Space size="small" wrap>
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
        </Space>
      ),
    },
    {
      title: "SN",
      dataIndex: "sn",
      key: "sn",
      width: 110,
      ellipsis: true,
      render: (v: string) => (
        <Tooltip title={v}>
          <Text type="secondary" style={{ fontSize: 12 }}>
            {v}
          </Text>
        </Tooltip>
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
  const isExpiredDeactivation = deactivationTerminal?.license_expires_at
    ? new Date(deactivationTerminal.license_expires_at) <= new Date()
    : true;

  return (
    <div>
      <PageHeader
        title="Лицензии"
        subtitle="Оплата лицензий и сертификатов терминалов"
        extra={
          <>
            <Tag color="red">Просрочено: {overdueCount}</Tag>
            <Tag color="green">Активных: {activeCount}</Tag>
            <Tag color="blue">Отключение: {deactivationCount}</Tag>
            <Tag>Отключённых: {disabledCount}</Tag>
          </>
        }
      />
      {/* Summary cards */}
      <Row gutter={[12, 12]} style={{ marginBottom: 12 }}>
        <Col xs={24} sm={12} md={6}>
          <Card
            size="small"
            style={{
              borderColor: totalMinor > 0 ? "#1677ff" : undefined,
              borderWidth: totalMinor > 0 ? 2 : 1,
            }}
          >
            <Space direction="vertical" size={2} style={{ width: "100%" }}>
              <Text type="secondary" style={{ fontSize: 12 }}>
                <ShoppingCartOutlined /> Сумма к оплате
              </Text>
              <div style={{ fontSize: 22, fontWeight: 700 }}>
                {formatMoneyMinor(totalMinor)}
              </div>
              <Text type="secondary" style={{ fontSize: 12 }}>
                лицензии: {licenseCount} · сертификаты: {certCount}
              </Text>
              <Button
                type="primary"
                block
                size="small"
                disabled={totalMinor <= 0}
                onClick={() => setCheckoutOpen(true)}
              >
                Перейти к оплате
              </Button>
            </Space>
          </Card>
        </Col>
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

      {/* Terminal table with tabs */}
      <Card
        size="small"
        title="Терминалы"
        extra={
          <Space wrap>
            <Text type="secondary" style={{ fontSize: 12 }}>
              Оплатить вперёд
            </Text>
            <Segmented
              size="small"
              value={advancePeriods}
              onChange={(v) => setAdvancePeriods(v as number)}
              options={advanceOptions}
            />
            <Button size="small" onClick={() => setAllVisible(true)}>
              Отметить всё
            </Button>
            <Button size="small" onClick={() => setAllVisible(false)}>
              Снять всё
            </Button>
            <Input
              placeholder="Поиск..."
              prefix={<SearchOutlined />}
              value={search}
              onChange={(e) => setSearch(e.target.value)}
              style={{ width: 180 }}
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
          pagination={{
            pageSize: 20,
            showSizeChanger: true,
            pageSizeOptions: ["10", "20", "50", "100"],
            showTotal: (total, range) =>
              `${range[0]}-${range[1]} из ${total} терм.`,
          }}
          scroll={{ x: 1200 }}
        />
      </Card>

      {/* Deactivation confirmation modal */}
      <Modal
        open={deactivateModal.open}
        title="Отключение терминала"
        onCancel={() => setDeactivateModal({ open: false, terminal: null })}
        onOk={handleDeactivate}
        confirmLoading={confirmLoading}
        okText={
          isExpiredDeactivation
            ? "Отключить терминал"
            : "Отключить после окончания лицензии"
        }
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

      {/* Combined checkout modal */}
      <CheckoutModal
        open={checkoutOpen}
        lines={cartLines}
        advancePeriods={advancePeriods}
        onClose={() => setCheckoutOpen(false)}
        onPaid={fetchData}
      />

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
