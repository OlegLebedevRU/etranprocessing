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
  Grid,
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
import { useSession } from "../session/SessionContext";
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
const { useBreakpoint } = Grid;

const STATUS_TABS = [
  { key: "all", label: "Все" },
  { key: "attention", label: "Требуют внимания" },
  { key: "active", label: "Активные" },
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
    !t.renewal_enabled ||
    !t.terminal_is_active ||
    t.billing_status === "disabled" ||
    t.billing_status === "deactivation_scheduled" ||
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
 * Whether the license line should be pre-checked by default:
 * Depends on default_selection_mode / billing_mode.
 */
function shouldAutoSelectLicense(
  t: BillingTerminal,
  advancePeriods: number,
  defaultSelectionMode: string = "all_due",
  billingMode: string = "standard",
): boolean {
  if (isTerminalDisabled(t)) return false;

  const mode =
    defaultSelectionMode ||
    (billingMode === "post_factum" ? "only_lapsed" : "all_due");

  if (mode === "only_lapsed") {
    if (isLicenseLapsed(t)) return true;
    if (advancePeriods > 0) {
      const horizonDays = advancePeriods * (t.billing_period_months || 1) * 30;
      return daysUntilLicenseExpiry(t) <= horizonDays;
    }
    return false;
  }

  if (mode === "all") {
    return true;
  }

  // default: "all_due"
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
  if (t.billing_mode === "cert_linked") return 0;
  const periods = (isLicenseLapsed(t) ? 1 : 0) + advancePeriods;
  return periods * t.period_price_minor;
}

function parseAllowedPeriods(str?: string | null): number[] | null {
  if (!str) return null;
  const trimmed = str.trim();
  if (!trimmed || trimmed === "*") return null;
  const nums = trimmed
    .split(",")
    .map((s) => parseInt(s.trim(), 10))
    .filter((n) => !isNaN(n) && n > 0);
  return nums.length > 0 ? nums : null;
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
  const { user } = useSession();
  const isRole4 = user?.role_id === 4;

  const screens = useBreakpoint();
  const isMobile = !screens.md;

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

  // Auto-select license lines: depends on default_selection_mode / billing_mode.
  // Certificate selections are independent and left untouched here.
  useEffect(() => {
    setSelection((prev) => {
      let changed = false;
      const next: Record<number, Selection> = { ...prev };
      for (const t of terminals) {
        const auto = shouldAutoSelectLicense(
          t,
          advancePeriods,
          summary?.default_selection_mode || "all_due",
          summary?.billing_mode || "standard",
        );
        const current = next[t.terminal_id] ?? { license: false, cert: false };
        if (current.license !== auto) {
          next[t.terminal_id] = { ...current, license: auto };
          changed = true;
        }
      }
      return changed ? next : prev;
    });
  }, [terminals, advancePeriods, summary]);

  const handleDeactivate = async () => {
    if (!deactivateModal.terminal) return;
    try {
      setConfirmLoading(true);
      await deactivateTerminal(deactivateModal.terminal.terminal_id);
      message.success("Терминал отключён");
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
      message.success("Терминал включён");
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
          !isTerminalDisabled(t) &&
          (t.billing_status === "overdue" ||
            t.billing_status === "due_soon" ||
            t.billing_status === "no_license")
        );
      case "active":
        return (
          !isTerminalDisabled(t) &&
          (t.billing_status === "active" || t.billing_status === "due_soon")
        );
      case "disabled":
        return isTerminalDisabled(t);
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
  const allowed = parseAllowedPeriods(summary?.allowed_billing_periods);
  const minPeriods = summary?.min_billing_periods || 1;
  const isPostFactum = summary?.billing_mode === "post_factum";
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

  const advanceOptions = useMemo(() => {
    if (allowed && allowed.length > 0) {
      const opts = [];
      if (allowed.includes(1) || isPostFactum || (hasDebt && !hasUrgentDueSoon)) {
        opts.push({ label: "Только задолженность", value: 0 });
      }
      for (const m of allowed) {
        opts.push({ label: monthsLabel(m), value: m });
      }
      return opts;
    }

    if (minPeriods > 1) {
      return [
        { label: monthsLabel(minPeriods * periodMonths), value: minPeriods },
        { label: monthsLabel(minPeriods * periodMonths * 2), value: minPeriods * 2 },
        { label: monthsLabel(Math.max(12, minPeriods * 3)), value: Math.max(12, minPeriods * 3) },
      ];
    }

    const showDebtOnly = isPostFactum || (hasDebt && !hasUrgentDueSoon);
    return [
      ...(showDebtOnly ? [{ label: "Только задолженность", value: 0 }] : []),
      { label: monthsLabel(periodMonths), value: 1 },
      { label: monthsLabel(periodMonths * 2), value: 2 },
      { label: monthsLabel(periodMonths * 3), value: 3 },
      { label: monthsLabel(periodMonths * 6), value: 6 },
      { label: monthsLabel(periodMonths * 12), value: 12 },
    ];
  }, [allowed, minPeriods, isPostFactum, hasDebt, hasUrgentDueSoon, periodMonths]);

  // Keep advancePeriods synchronized with valid options when settings/options change
  useEffect(() => {
    if (!loading && advanceOptions.length > 0) {
      const validValues = advanceOptions.map((o) => o.value);
      if (!validValues.includes(advancePeriods)) {
        if (summary?.billing_mode === "post_factum" && validValues.includes(0)) {
          setAdvancePeriods(0);
        } else if (summary?.min_billing_periods && validValues.includes(summary.min_billing_periods)) {
          setAdvancePeriods(summary.min_billing_periods);
        } else {
          setAdvancePeriods(advanceOptions[0].value);
        }
      }
    }
  }, [loading, advanceOptions, advancePeriods, summary]);


  // Counters
  const overdueCount = terminals.filter(
    (t) => !isTerminalDisabled(t) && t.billing_status === "overdue",
  ).length;
  const activeCount = terminals.filter(
    (t) =>
      !isTerminalDisabled(t) &&
      (t.billing_status === "active" || t.billing_status === "due_soon"),
  ).length;
  const disabledCount = terminals.filter((t) => isTerminalDisabled(t)).length;

  const columns: ColumnsType<BillingTerminal> = [
    {
      title: "Терминал",
      dataIndex: "device_id",
      key: "device_id",
      width: isMobile ? 85 : 105,
      fixed: isMobile ? "left" : undefined,
      sorter: (a, b) => a.device_id - b.device_id,
      render: (val: number) => (
        <span style={{ whiteSpace: "nowrap" }}>
          <Text strong style={{ fontSize: 13 }}>#{val}</Text>
        </span>
      ),
    },
    {
      title: "Тип",
      dataIndex: "terminal_type_name",
      key: "terminal_type",
      width: 130,
      render: (v: string | null, r: BillingTerminal) => {
        if (r.terminal_type_id !== undefined && r.terminal_type_id !== null) {
          return (
            <div style={{ fontSize: 12, wordBreak: "break-word", lineHeight: 1.3 }}>
              {v ? `${r.terminal_type_id}: ${v}` : `${r.terminal_type_id}`}
            </div>
          );
        }
        return <div style={{ fontSize: 12, wordBreak: "break-word" }}>{v || "—"}</div>;
      },
    },
    {
      title: "Адрес",
      dataIndex: "address",
      key: "address",
      width: 220,
      render: (v: string | null) =>
        v ? (
          <div style={{ fontSize: 12, wordBreak: "break-word", whiteSpace: "normal", lineHeight: 1.35 }}>
            {v}
          </div>
        ) : (
          <Text type="secondary" style={{ fontSize: 12 }}>
            —
          </Text>
        ),
    },
    {
      title: "Статус",
      dataIndex: "billing_status",
      key: "status",
      width: 140,
      render: (status: string) => (
        <Tag
          color={billingStatusColor(status)}
          style={{
            whiteSpace: "normal",
            wordBreak: "break-word",
            height: "auto",
            padding: "2px 8px",
            lineHeight: 1.3,
            textAlign: "center",
            display: "inline-block",
            fontSize: 12,
          }}
        >
          {billingStatusLabel(status)}
        </Tag>
      ),
    },
    {
      title: "Лицензия",
      key: "license",
      width: 220,
      render: (_: unknown, r: BillingTerminal) => {
        if (isRole4 || isTerminalDisabled(r)) {
          return (
            <span style={{ whiteSpace: "nowrap" }}>
              <Text type={isLicenseLapsed(r) ? "danger" : "secondary"}>
                {r.license_expires_at
                  ? formatDate(r.license_expires_at)
                  : "нет лицензии"}
              </Text>
            </span>
          );
        }

        if (r.billing_mode === "cert_linked") {
          return (
            <Space direction="vertical" size={0} style={{ whiteSpace: "nowrap" }}>
              <span style={{ whiteSpace: "nowrap" }}>
                <Text>
                  {r.license_expires_at
                    ? formatDate(r.license_expires_at)
                    : "по сертификату"}
                </Text>
              </span>
              <span style={{ whiteSpace: "nowrap" }}>
                <Text type="secondary" style={{ fontSize: 12 }}>
                  0 ₽ (в сертификате)
                </Text>
              </span>
            </Space>
          );
        }

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
              <span style={{ whiteSpace: "nowrap" }}>
                <Text type={lapsed ? "danger" : undefined}>
                  {r.license_expires_at
                    ? formatDate(r.license_expires_at)
                    : "нет лицензии"}
                </Text>
              </span>
              {payable && (
                <div style={{ fontSize: 12, color: "#64748b", lineHeight: 1.3 }}>
                  <span style={{ whiteSpace: "nowrap" }}>
                    {formatMoneyMinor(licenseAmountMinor(r, advancePeriods))}
                  </span>
                  {checked && (
                    <span style={{ whiteSpace: "nowrap" }}>
                      {` → ${formatDate(projectedExpiry(r, advancePeriods))}`}
                    </span>
                  )}
                </div>
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
        if (isRole4 || isTerminalDisabled(r)) {
          return !r.cert_serial ? (
            <span style={{ whiteSpace: "nowrap" }}><Text type="secondary">не выпущен</Text></span>
          ) : !r.cert_not_valid_after ? (
            <span style={{ whiteSpace: "nowrap" }}><Text type="secondary">выпущен (дата неизвестна)</Text></span>
          ) : (
            <span style={{ whiteSpace: "nowrap" }}>
              <Text
                type={
                  new Date(r.cert_not_valid_after) < new Date()
                    ? "danger"
                    : "secondary"
                }
              >
                {formatDate(r.cert_not_valid_after)}
              </Text>
            </span>
          );
        }

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
              <Space direction="vertical" size={0} style={{ whiteSpace: "nowrap" }}>
                <span style={{ whiteSpace: "nowrap" }}>
                  <Text type="secondary" style={{ fontSize: 12 }}>
                    Текущий до{" "}
                    {r.cert_not_valid_after
                      ? formatDate(r.cert_not_valid_after)
                      : "—"}
                  </Text>
                </span>
                <span style={{ whiteSpace: "nowrap" }}><Text type="warning">Новый ждёт установки</Text></span>
                <span style={{ whiteSpace: "nowrap" }}>
                  <Text type="success" style={{ fontSize: 12 }}>
                    Оплачено
                  </Text>
                </span>
              </Space>
              {pinButton}
            </Space>
          );
        }

        const label = !r.cert_serial ? (
          <span style={{ whiteSpace: "nowrap" }}><Text type="warning">не выпущен</Text></span>
        ) : !r.cert_not_valid_after ? (
          <span style={{ whiteSpace: "nowrap" }}><Text type="secondary">выпущен (дата неизвестна)</Text></span>
        ) : (
          <span style={{ whiteSpace: "nowrap" }}>
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
          </span>
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
                <span style={{ whiteSpace: "nowrap" }}>
                  <Text type="secondary" style={{ fontSize: 12 }}>
                    {formatMoneyMinor(r.cert_pin_price_minor)}
                  </Text>
                </span>
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
      width: 140,
      render: (v: number, r: BillingTerminal) => {
        if (r.billing_mode === "cert_linked") {
          return (
            <span style={{ whiteSpace: "nowrap" }}>
              <Text type="secondary" style={{ fontSize: 12 }}>
                0 ₽ (в сертификате)
              </Text>
            </span>
          );
        }
        return (
          <span style={{ whiteSpace: "nowrap", fontSize: 12 }}>
            {formatMoneyMinor(v)}/{formatBillingPeriod(r.billing_period_months)}
          </span>
        );
      },
    },
    {
      title: "Задолженность",
      dataIndex: "overdue_amount_minor",
      key: "debt",
      align: "right",
      width: 130,
      render: (v: number, r: BillingTerminal) =>
        !isTerminalDisabled(r) && v > 0 ? (
          <span style={{ whiteSpace: "nowrap" }}>
            <Text type="danger" strong>
              {formatDebt(v)}
            </Text>
          </span>
        ) : (
          <span style={{ whiteSpace: "nowrap" }}>
            <Text type="secondary">{formatMoneyMinor(0)}</Text>
          </span>
        ),
    },
    {
      title: "Действия",
      key: "actions",
      width: 160,
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
            <Tooltip
              title={
                r.license_expires_at
                  ? `Включить без оплаты (лицензия действует до ${formatDate(r.license_expires_at)})`
                  : undefined
              }
            >
              <Button
                size="small"
                type="primary"
                onClick={() => handleCancelDeactivation(r.terminal_id)}
              >
                Включить
              </Button>
            </Tooltip>
          )}
        </Space>
      ),
    },
    {
      title: "SN",
      dataIndex: "sn",
      key: "sn",
      width: 130,
      render: (v: string) => (
        <Tooltip title={v}>
          <span style={{ whiteSpace: "nowrap" }}>
            <Text code style={{ fontSize: 11 }}>
              {v}
            </Text>
          </span>
        </Tooltip>
      ),
    },
  ];

  const displayedColumns = useMemo(() => {
    if (isRole4) {
      return columns.filter((c) => c.key !== "actions");
    }
    return columns;
  }, [isRole4, columns]);

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
        subtitle={
          isRole4
            ? "Просмотр сроков действия лицензий и сертификатов терминалов (только чтение)"
            : "Оплата лицензий и сертификатов терминалов"
        }
        extra={
          <>
            {summary?.billing_mode === "post_factum" && (
              <Tag color="orange">Пост-оплата (post_factum)</Tag>
            )}
            {summary?.billing_mode === "cert_linked" && (
              <Tag color="geekblue">По сертификату (cert_linked)</Tag>
            )}
            <Tag color="red">Просрочено: {overdueCount}</Tag>
            <Tag color="green">Активных: {activeCount}</Tag>
            <Tag>Отключённых: {disabledCount}</Tag>
          </>
        }
      />
      {/* Summary cards with responsive auto-fit grid */}
      <div
        style={{
          display: "grid",
          gridTemplateColumns: "repeat(auto-fit, minmax(160px, 1fr))",
          gap: 10,
          marginBottom: 12,
        }}
      >
        {!isRole4 && (
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
              <div style={{ fontSize: 20, fontWeight: 700 }}>
                {formatMoneyMinor(totalMinor)}
              </div>
              <Text type="secondary" style={{ fontSize: 11 }}>
                лицензии: {licenseCount} · сертификаты: {certCount}
              </Text>
              <Button
                type="primary"
                block
                size="small"
                disabled={cartLines.length === 0}
                onClick={() => setCheckoutOpen(true)}
              >
                {totalMinor > 0
                  ? `Оплатить (${formatMoneyMinor(totalMinor)})`
                  : `Оформить (${cartLines.length} поз.)`}
              </Button>
            </Space>
          </Card>
        )}

        <Card size="small">
          <Space direction="vertical" size={2} style={{ width: "100%" }}>
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
            <Text type="secondary" style={{ fontSize: 11 }}>
              {summary?.overdue_terminal_count || 0} терм.
            </Text>
          </Space>
        </Card>

        {summary?.forecast.map((f) => (
          <Card size="small" key={f.month}>
            <Space direction="vertical" size={2} style={{ width: "100%" }}>
              <Text type="secondary" style={{ fontSize: 12 }}>
                <CalendarOutlined /> {formatForecastMonth(f.month)}
              </Text>
              <div style={{ fontSize: 20, fontWeight: 600 }}>
                {formatMoneyMinor(f.amount_minor)}
              </div>
              <Text type="secondary" style={{ fontSize: 11 }}>
                {f.terminal_count} терм.
              </Text>
            </Space>
          </Card>
        ))}
      </div>

      {/* Terminal table with tabs */}
      <Card
        size="small"
        title="Терминалы"
        extra={
          <Space wrap style={{ width: isMobile ? "100%" : "auto" }}>
            {!isRole4 && (
              <>
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
              </>
            )}
            <Input
              placeholder="Поиск..."
              prefix={<SearchOutlined />}
              value={search}
              onChange={(e) => setSearch(e.target.value)}
              style={{ width: isMobile ? "100%" : 180 }}
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
          className="compact-table"
          dataSource={filteredTerminals}
          columns={displayedColumns}
          rowKey="terminal_id"
          size="small"
          tableLayout="auto"
          pagination={{
            defaultPageSize: 50,
            pageSize: 50,
            showSizeChanger: true,
            pageSizeOptions: ["10", "20", "50", "100"],
            size: "small",
            simple: isMobile,
            showTotal: (total, range) =>
              `${range[0]}-${range[1]} из ${total} терм.`,
          }}
          scroll={{ x: 1250 }}
        />
      </Card>

      {/* Deactivation confirmation modal */}
      <Modal
        open={deactivateModal.open}
        title="Отключение терминала"
        onCancel={() => setDeactivateModal({ open: false, terminal: null })}
        onOk={handleDeactivate}
        confirmLoading={confirmLoading}
        okText="Отключить"
        cancelText="Отмена"
        okButtonProps={{ danger: true }}
      >
        {deactivationTerminal && (
          <p>
            {isExpiredDeactivation
              ? "Терминал не имеет действующей лицензии. После отключения он не будет учитываться в расчетах и перейдет в раздел Отключенные."
              : `Терминал перейдет в раздел Отключенные и не будет учитываться в расчетах сумм. Действующая лицензия и сертификат (до ${formatDate(deactivationTerminal.license_expires_at)}) позволят включить его без оплаты до даты истечения.`}
          </p>
        )}
      </Modal>

      {/* Combined checkout modal */}
      <CheckoutModal
        open={checkoutOpen}
        lines={cartLines}
        advancePeriods={advancePeriods}
        billingMode={summary?.billing_mode}
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
