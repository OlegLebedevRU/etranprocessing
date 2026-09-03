import { useEffect, useState, useCallback, useMemo } from "react";
import dayjs from "dayjs";
import {
  Layout,
  Menu,
  Table,
  DatePicker,
  Input,
  Button,
  Space,
  Tag,
  Select,
  Grid,
  Segmented,
} from "antd";
import {
  FileTextOutlined,
  DollarOutlined,
  BarChartOutlined,
  ReloadOutlined,
  SearchOutlined,
} from "@ant-design/icons";
import type { ColumnsType } from "antd/es/table";
import {
  getInkass,
  type InkassRecord,
  getPayments,
  type PaymentRecord,
  getBalanceByTerminal,
  type BalanceByTerminalRecord,
  getBalanceByTsp,
  type BalanceByTspRecord,
} from "../api/reports";
import { getServices } from "../api/services";
import { getMe } from "../api/auth";
import {
  DEFAULT_TIMEZONE,
  formatTenantDateTime,
  getTimezoneBadgeText,
} from "../utils/timezone";

const { Sider, Content } = Layout;

const REPORTS = [
  { key: "inkass", icon: <FileTextOutlined />, label: "Инкассация" },
  { key: "payments", icon: <DollarOutlined />, label: "Платежи" },
  { key: "balance-terminal", icon: <BarChartOutlined />, label: "По терминалам" },
  { key: "balance-tsp", icon: <BarChartOutlined />, label: "По ТСП" },
];

function fmtMoney(v: number): string {
  if (!v) return "—";
  return (v / 100).toLocaleString("ru-RU", {
    minimumFractionDigits: 2,
    maximumFractionDigits: 2,
  });
}

function fmtInt(v: number): string {
  return v ? v.toLocaleString("ru-RU") : "—";
}

const NOTE_LABELS = ["1", "2", "5", "10", "50", "100", "200", "500", "1000", "5000"];
const COIN_LABELS = ["1", "2", "5", "10", "—", "—", "—", "—", "—", "—"];

const PAYM_STATE_OPTIONS = [
  { value: -1, label: "Все" },
  { value: 0, label: "Новый" },
  { value: 1, label: "В обработке" },
  { value: 2, label: "Оплачен" },
  { value: 3, label: "Не оплачен" },
  { value: 4, label: "Остановлен" },
  { value: 5, label: "Перезапуск" },
  { value: 6, label: "Карантин" },
];

const TOP_OPTIONS = [
  { value: 50, label: "50" },
  { value: 100, label: "100" },
  { value: 200, label: "200" },
  { value: 500, label: "500" },
  { value: 1000, label: "1000" },
];

const PAYM_STATE_COLORS: Record<number, string> = {
  0: "default",
  1: "processing",
  2: "success",
  3: "error",
  4: "warning",
  5: "purple",
  6: "orange",
};

export default function ReportsPage() {
  const [activeReport, setActiveReport] = useState("inkass");
  const [servicesMap, setServicesMap] = useState<Record<number, string>>({});
  // Tenant timezone context comes exclusively from /api/auth/me. Browser timezone
  // and shared localStorage caches must never be used as an authoritative source.
  const [tenantTz, setTenantTz] = useState<string>(DEFAULT_TIMEZONE);
  const [tenantReady, setTenantReady] = useState(false);

  const screens = Grid.useBreakpoint();
  const isMobile = !screens.md;
  const isXs = screens.xs;

  useEffect(() => {
    getMe()
      .then((u) => {
        if (u?.timezone) {
          setTenantTz(u.timezone);
        }
      })
      .catch(() => {})
      .finally(() => setTenantReady(true));
  }, []);

  useEffect(() => {
    getServices()
      .then((res) => {
        const map: Record<number, string> = {};
        (res.data || []).forEach((s) => {
          if (s.tsp_code && s.name) {
            map[s.tsp_code] = s.name;
          }
        });
        setServicesMap(map);
      })
      .catch(() => {});
  }, []);

  // --- Inkass state ---
  const [inkItems, setInkItems] = useState<InkassRecord[]>([]);
  const [inkTotal, setInkTotal] = useState(0);
  const [inkLoading, setInkLoading] = useState(false);
  const [inkPage, setInkPage] = useState(1);
  const [inkDateFrom, setInkDateFrom] = useState("");
  const [inkDateTo, setInkDateTo] = useState("");
  const [inkTermInput, setInkTermInput] = useState("");
  const [inkDeviceIds, setInkDeviceIds] = useState<number[]>([]);

  // --- Payments state ---
  // "Today" must be computed in the confirmed tenant timezone, never the browser's.
  const todayStr = useMemo(
    () => dayjs().tz(tenantTz).format("YYYY-MM-DD"),
    [tenantTz]
  );
  const [payItems, setPayItems] = useState<PaymentRecord[]>([]);
  const [payTotal, setPayTotal] = useState(0);
  const [payLoading, setPayLoading] = useState(false);
  const [payDateFrom, setPayDateFrom] = useState<string>("");
  const [payDateTo, setPayDateTo] = useState<string>("");
  const [payTermInput, setPayTermInput] = useState("");
  const [payDeviceIds, setPayDeviceIds] = useState<number[]>([]);
  const [payTspCode, setPayTspCode] = useState<number | undefined>();
  const [payTop, setPayTop] = useState(50);

  // --- Balance by terminal state ---
  const [btItems, setBtItems] = useState<BalanceByTerminalRecord[]>([]);
  const [btLoading, setBtLoading] = useState(false);
  const [btDateFrom, setBtDateFrom] = useState<string>("");
  const [btDateTo, setBtDateTo] = useState<string>("");
  const [btTermInput, setBtTermInput] = useState("");
  const [btDeviceIds, setBtDeviceIds] = useState<number[]>([]);

  // --- Balance by TSP state ---
  const [btsItems, setBtsItems] = useState<BalanceByTspRecord[]>([]);
  const [btsLoading, setBtsLoading] = useState(false);
  const [btsDateFrom, setBtsDateFrom] = useState<string>("");
  const [btsDateTo, setBtsDateTo] = useState<string>("");
  const [btsTermInput, setBtsTermInput] = useState("");
  const [btsDeviceIds, setBtsDeviceIds] = useState<number[]>([]);

  // --- Inkass fetch ---
  const fetchInkass = useCallback(async () => {
    setInkLoading(true);
    try {
      const resp = await getInkass({
        date_from: inkDateFrom || undefined,
        date_to: inkDateTo || undefined,
        device_ids: inkDeviceIds.length ? inkDeviceIds : undefined,
        page: inkPage,
        size: 50,
      });
      setInkItems(resp.items);
      setInkTotal(resp.total);
    } finally {
      setInkLoading(false);
    }
  }, [inkDateFrom, inkDateTo, inkDeviceIds, inkPage]);

  useEffect(() => {
    if (tenantReady && activeReport === "inkass") fetchInkass();
  }, [fetchInkass, activeReport, tenantReady]);

  const handleInkTermSearch = () => {
    const ids = inkTermInput
      .split(",")
      .map((s) => parseInt(s.trim(), 10))
      .filter((n) => !isNaN(n));
    setInkDeviceIds(ids);
    setInkPage(1);
  };

  // --- Payments fetch ---
  const fetchPayments = useCallback(async () => {
    setPayLoading(true);
    try {
      const resp = await getPayments({
        date_from: payDateFrom || todayStr,
        date_to: payDateTo || todayStr,
        device_ids: payDeviceIds.length ? payDeviceIds : undefined,
        tsp_code: payTspCode,
        top: payTop,
      });
      setPayItems(resp.items);
      setPayTotal(resp.total);
    } finally {
      setPayLoading(false);
    }
  }, [payDateFrom, payDateTo, payDeviceIds, payTspCode, payTop, todayStr]);

  useEffect(() => {
    if (tenantReady && activeReport === "payments") fetchPayments();
  }, [fetchPayments, activeReport, tenantReady]);

  const handlePayTermSearch = () => {
    const ids = payTermInput
      .split(",")
      .map((s) => parseInt(s.trim(), 10))
      .filter((n) => !isNaN(n));
    setPayDeviceIds(ids);
  };

  // --- Balance by terminal fetch ---
  const fetchBalanceByTerminal = useCallback(async () => {
    setBtLoading(true);
    try {
      const resp = await getBalanceByTerminal({
        date_from: btDateFrom || todayStr,
        date_to: btDateTo || todayStr,
        device_ids: btDeviceIds.length ? btDeviceIds : undefined,
      });
      setBtItems(resp.items);
    } finally {
      setBtLoading(false);
    }
  }, [btDateFrom, btDateTo, btDeviceIds, todayStr]);

  useEffect(() => {
    if (tenantReady && activeReport === "balance-terminal") fetchBalanceByTerminal();
  }, [fetchBalanceByTerminal, activeReport, tenantReady]);

  const handleBtTermSearch = () => {
    const ids = btTermInput
      .split(",")
      .map((s) => parseInt(s.trim(), 10))
      .filter((n) => !isNaN(n));
    setBtDeviceIds(ids);
  };

  // --- Balance by TSP fetch ---
  const fetchBalanceByTsp = useCallback(async () => {
    setBtsLoading(true);
    try {
      const resp = await getBalanceByTsp({
        date_from: btsDateFrom || todayStr,
        date_to: btsDateTo || todayStr,
        device_ids: btsDeviceIds.length ? btsDeviceIds : undefined,
      });
      setBtsItems(resp.items);
    } finally {
      setBtsLoading(false);
    }
  }, [btsDateFrom, btsDateTo, btsDeviceIds, todayStr]);

  useEffect(() => {
    if (tenantReady && activeReport === "balance-tsp") fetchBalanceByTsp();
  }, [fetchBalanceByTsp, activeReport, tenantReady]);

  const handleBtsTermSearch = () => {
    const ids = btsTermInput
      .split(",")
      .map((s) => parseInt(s.trim(), 10))
      .filter((n) => !isNaN(n));
    setBtsDeviceIds(ids);
  };

  // --- Inkass columns ---
  const inkassColumns: ColumnsType<InkassRecord> = [
    {
      title: "Терминал",
      dataIndex: "device_id",
      fixed: "left" as const,
      width: 90,
      sorter: (a, b) => a.device_id - b.device_id,
    },
    {
      title: "Время",
      dataIndex: "inkass_datetime",
      width: 150,
      sorter: (a, b) => a.inkass_datetime.localeCompare(b.inkass_datetime),
      defaultSortOrder: "descend",
      render: (v: string) => formatTenantDateTime(v, tenantTz),
    },
    {
      title: "Сумма",
      dataIndex: "total_sum",
      width: 110,
      align: "right",
      render: (v: number) => (v ? v.toLocaleString("ru-RU") + " ₽" : "—"),
      sorter: (a, b) => a.total_sum - b.total_sum,
    },
    {
      title: "Расчетная сумма",
      dataIndex: "calculated_sum",
      width: 130,
      align: "right",
      render: (v: number) => (v ? v.toLocaleString("ru-RU") + " ₽" : "0 ₽"),
      sorter: (a, b) => a.calculated_sum - b.calculated_sum,
    },
    {
      title: "Всего банкнот",
      dataIndex: "total_note_count",
      width: 110,
      align: "right",
      render: fmtInt,
    },
    {
      title: "Количество операций",
      dataIndex: "transact_count",
      width: 140,
      align: "right",
      render: fmtInt,
    },
    {
      title: "ID Транзакции",
      dataIndex: "paym_ext_id",
      width: 170,
      ellipsis: true,
    },
    {
      title: "ID Инкассации",
      dataIndex: "inkass_ext_id",
      width: 170,
      ellipsis: true,
    },
    {
      title: "Номер отчета",
      dataIndex: "report_number",
      width: 110,
      ellipsis: true,
      render: (v: string | number) => String(v || "—"),
    },
  ];

  // --- Payments columns ---
  const paymentsColumns: ColumnsType<PaymentRecord> = [
    {
      title: "Транзакция",
      dataIndex: "paym_ext_id",
      fixed: "left" as const,
      width: 160,
      ellipsis: true,
    },
    {
      title: "Терминал",
      dataIndex: "device_id",
      width: 90,
      sorter: (a, b) => a.device_id - b.device_id,
    },
    {
      title: "ТСП",
      dataIndex: "tsp_name",
      width: 240,
      ellipsis: true,
      render: (v: string | undefined, r: PaymentRecord) => {
        if (!r.paym_tsp_code) {
          return v || "—";
        }
        return (
          <span style={{ display: "inline-flex", alignItems: "center", gap: 4, maxWidth: "100%" }}>
            <span style={{ fontWeight: 600 }}>{r.paym_tsp_code}</span>
            {r.menu_version ? (
              <>
                <span style={{ color: "#bfbfbf" }}>|</span>
                <Tag color="blue" style={{ margin: 0, padding: "0 4px", fontSize: 11, lineHeight: "18px" }}>
                  {`v${r.menu_version}`}
                </Tag>
              </>
            ) : null}
            <span style={{ overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}>
              : {v || "—"}
            </span>
          </span>
        );
      },
    },
    {
      title: "Сумма",
      dataIndex: "paym_amount",
      width: 110,
      align: "right",
      render: (v: number) => fmtMoney(v),
      sorter: (a, b) => a.paym_amount - b.paym_amount,
    },
    {
      title: "Время",
      dataIndex: "paym_datetime",
      width: 150,
      sorter: (a, b) => a.paym_datetime.localeCompare(b.paym_datetime),
      defaultSortOrder: "descend",
      render: (v: string) => formatTenantDateTime(v, tenantTz),
    },
    {
      title: "Состояние",
      dataIndex: "paym_state_label",
      width: 120,
      render: (label: string, r) => (
        <Tag color={PAYM_STATE_COLORS[r.paym_state] || "default"}>{label}</Tag>
      ),
    },
    {
      title: "Тип",
      dataIndex: "pay_type_label",
      width: 100,
    },
  ];

  // --- Balance by terminal columns ---
  const balanceTerminalColumns: ColumnsType<BalanceByTerminalRecord> = [
    {
      title: "Терминал",
      dataIndex: "device_id",
      fixed: "left" as const,
      width: 110,
      sorter: (a, b) => a.device_id - b.device_id,
    },
    {
      title: "Сумма",
      dataIndex: "total_amount",
      width: 160,
      align: "right",
      render: (v: number) => fmtMoney(v),
      sorter: (a, b) => a.total_amount - b.total_amount,
      defaultSortOrder: "descend",
    },
    {
      title: "Платежей",
      dataIndex: "total_count",
      width: 140,
      align: "right",
      render: fmtInt,
      sorter: (a, b) => a.total_count - b.total_count,
    },
  ];

  // --- Balance by TSP columns ---
  const balanceTspColumns: ColumnsType<BalanceByTspRecord> = [
    {
      title: "ТСП/Версия меню",
      dataIndex: "tsp_code",
      fixed: "left" as const,
      width: 165,
      sorter: (a, b) => a.tsp_code - b.tsp_code || (a.version || 0) - (b.version || 0),
      render: (code: number, r: BalanceByTspRecord) => (
        <span style={{ display: "inline-flex", alignItems: "center", gap: 6 }}>
          <span style={{ fontWeight: 600 }}>{code}</span>
          <span style={{ color: "#bfbfbf" }}>|</span>
          {r.version ? (
            <Tag color="blue" style={{ margin: 0, padding: "0 4px", fontSize: 11, lineHeight: "18px" }}>
              {`v${r.version}`}
            </Tag>
          ) : (
            <Tag color="default" style={{ margin: 0, padding: "0 4px", fontSize: 11, lineHeight: "18px" }}>
              —
            </Tag>
          )}
        </span>
      ),
    },
    {
      title: "Название",
      dataIndex: "tsp_name",
      width: 240,
      ellipsis: true,
      render: (v: string, r: BalanceByTspRecord) => v || `ТСП ${r.tsp_code}`,
    },
    {
      title: "Сумма",
      dataIndex: "total_amount",
      width: 140,
      align: "right",
      render: (v: number) => fmtMoney(v),
      sorter: (a, b) => a.total_amount - b.total_amount,
      defaultSortOrder: "descend",
    },
    {
      title: "Платежей",
      dataIndex: "total_count",
      width: 110,
      align: "right",
      render: fmtInt,
      sorter: (a, b) => a.total_count - b.total_count,
    },
    {
      title: "Терминалов",
      dataIndex: "terminal_count",
      width: 110,
      align: "right",
      sorter: (a, b) => a.terminal_count - b.terminal_count,
    },
  ];

  return (
    <div>
      {/* Mobile/Tablet report switcher */}
      {isMobile && (
        <div style={{ marginBottom: 12, overflowX: "auto", paddingBottom: 2 }}>
          <Segmented
            block={isXs}
            size="middle"
            value={activeReport}
            onChange={(v) => setActiveReport(String(v))}
            options={REPORTS.map((r) => ({
              value: r.key,
              label: (
                <span style={{ display: "inline-flex", alignItems: "center", gap: 6, padding: "2px 4px" }}>
                  {r.icon}
                  <span>{r.label}</span>
                </span>
              ),
            }))}
          />
        </div>
      )}

      <div style={{ display: "flex", gap: 12, alignItems: "flex-start" }}>
        {/* Desktop Sider */}
        {!isMobile && (
          <div
            style={{
              width: 170,
              flex: "0 0 170px",
              background: "#fff",
              border: "1px solid #eceff3",
              borderRadius: 10,
              padding: 6,
              alignSelf: "flex-start",
              position: "sticky",
              top: 64,
              maxHeight: "calc(100vh - 80px)",
              overflowY: "auto",
            }}
          >
            <div
              style={{
                fontSize: 11,
                fontWeight: 600,
                letterSpacing: 0.6,
                textTransform: "uppercase",
                color: "#94a3b8",
                padding: "6px 10px 8px",
              }}
            >
              Отчёты
            </div>
            <Menu
              mode="inline"
              selectedKeys={[activeReport]}
              onClick={({ key }) => setActiveReport(key)}
              items={REPORTS}
              style={{ border: "none", background: "transparent" }}
            />
          </div>
        )}

        <div style={{ flex: 1, minWidth: 0, width: "100%" }}>
          {/* ====== Inkass report ====== */}
          {activeReport === "inkass" && (
            <div
              style={{ background: "#fff", borderRadius: 8, padding: isXs ? 8 : 12 }}
            >
              <div
                style={{
                  marginBottom: 12,
                  display: "flex",
                  justifyContent: "space-between",
                  alignItems: "center",
                  flexWrap: "wrap",
                  gap: 8,
                }}
              >
                <div
                  style={{
                    display: "flex",
                    flexWrap: "wrap",
                    gap: 8,
                    alignItems: "center",
                    flex: 1,
                    minWidth: isXs ? "100%" : 320,
                  }}
                >
                  <div style={{ display: "flex", gap: 6, flex: isXs ? "1 1 100%" : undefined }}>
                    <DatePicker
                      placeholder="Дата с"
                      size="small"
                      style={{ flex: 1 }}
                      onChange={(d) =>
                        setInkDateFrom(d ? d.format("YYYY-MM-DD") : "")
                      }
                    />
                    <DatePicker
                      placeholder="Дата по"
                      size="small"
                      style={{ flex: 1 }}
                      onChange={(d) =>
                        setInkDateTo(d ? d.format("YYYY-MM-DD") : "")
                      }
                    />
                  </div>
                  <div style={{ display: "flex", gap: 6, flex: isXs ? "1 1 100%" : undefined }}>
                    <Input
                      placeholder="ID терминалов (через запятую)"
                      size="small"
                      style={{ flex: 1, minWidth: 140, maxWidth: isXs ? undefined : 220 }}
                      value={inkTermInput}
                      onChange={(e) => setInkTermInput(e.target.value)}
                      onPressEnter={handleInkTermSearch}
                      allowClear
                    />
                    <Button
                      size="small"
                      type="primary"
                      icon={<SearchOutlined />}
                      onClick={handleInkTermSearch}
                    >
                      {!isXs && "Найти"}
                    </Button>
                  </div>
                </div>

                <div style={{ display: "flex", alignItems: "center", gap: 8, marginLeft: isXs ? 0 : "auto" }}>
                  <Tag color="blue" style={{ margin: 0, fontSize: isXs ? 11 : 12 }}>
                    {isXs ? getTimezoneBadgeText(tenantTz) : `Часовой пояс: ${getTimezoneBadgeText(tenantTz)}`}
                  </Tag>
                  <Button
                    size="small"
                    icon={<ReloadOutlined />}
                    onClick={fetchInkass}
                  />
                </div>
              </div>

              <Table<InkassRecord>
                rowKey="id"
                columns={inkassColumns}
                dataSource={inkItems}
                loading={inkLoading}
                size="small"
                className="reports-table"
                tableLayout="fixed"
                pagination={{
                  current: inkPage,
                  pageSize: 50,
                  total: inkTotal,
                  onChange: setInkPage,
                  simple: isXs,
                  showTotal: (t, range) =>
                    isXs ? `${range[0]}-${range[1]}/${t}` : `Всего: ${t}`,
                  showSizeChanger: !isXs,
                  pageSizeOptions: ["10", "20", "50", "100"],
                }}
                scroll={{ x: 850 }}
                expandable={{
                  expandedRowRender: (r) => (
                    <div style={{ padding: "6px 0" }}>
                      <div style={{ fontSize: 12, color: "#1f2937", marginBottom: 4, fontWeight: 600 }}>
                        Банкноты:
                      </div>
                      <div style={{ display: "flex", flexWrap: "wrap", gap: 6 }}>
                        <Tag>10 ₽: {r.banknotes?.n10 ?? 0}</Tag>
                        <Tag>50 ₽: {r.banknotes?.n50 ?? 0}</Tag>
                        <Tag>100 ₽: {r.banknotes?.n100 ?? 0}</Tag>
                        <Tag>500 ₽: {r.banknotes?.n500 ?? 0}</Tag>
                        <Tag>1 000 ₽: {r.banknotes?.n1000 ?? 0}</Tag>
                        <Tag>2 000 ₽: {r.banknotes?.n2000 ?? 0}</Tag>
                        <Tag>5 000 ₽: {r.banknotes?.n5000 ?? 0}</Tag>
                      </div>
                    </div>
                  ),
                }}
              />
            </div>
          )}

          {/* ====== Payments report ====== */}
          {activeReport === "payments" && (
            <div
              style={{ background: "#fff", borderRadius: 8, padding: isXs ? 8 : 12 }}
            >
              <div
                style={{
                  marginBottom: 12,
                  display: "flex",
                  justifyContent: "space-between",
                  alignItems: "center",
                  flexWrap: "wrap",
                  gap: 8,
                }}
              >
                <div
                  style={{
                    display: "flex",
                    flexWrap: "wrap",
                    gap: 8,
                    alignItems: "center",
                    flex: 1,
                    minWidth: isXs ? "100%" : 320,
                  }}
                >
                  <div style={{ display: "flex", gap: 6, flex: isXs ? "1 1 100%" : undefined }}>
                    <DatePicker
                      placeholder="Дата с"
                      size="small"
                      style={{ flex: 1 }}
                      value={dayjs(payDateFrom || todayStr)}
                      allowClear={false}
                      onChange={(d) =>
                        setPayDateFrom(d ? d.format("YYYY-MM-DD") : todayStr)
                      }
                    />
                    <DatePicker
                      placeholder="Дата по"
                      size="small"
                      style={{ flex: 1 }}
                      value={dayjs(payDateTo || todayStr)}
                      allowClear={false}
                      onChange={(d) =>
                        setPayDateTo(d ? d.format("YYYY-MM-DD") : todayStr)
                      }
                    />
                  </div>
                  <div style={{ display: "flex", gap: 6, flex: isXs ? "1 1 100%" : undefined, flexWrap: "wrap" }}>
                    <Input
                      placeholder="ID терминалов"
                      size="small"
                      style={{ flex: 1, minWidth: 110, maxWidth: isXs ? undefined : 180 }}
                      value={payTermInput}
                      onChange={(e) => setPayTermInput(e.target.value)}
                      onPressEnter={handlePayTermSearch}
                      allowClear
                    />
                    <Button
                      size="small"
                      type="primary"
                      icon={<SearchOutlined />}
                      onClick={handlePayTermSearch}
                    >
                      {!isXs && "Найти"}
                    </Button>
                    <Input
                      placeholder="TSP"
                      size="small"
                      style={{ width: isXs ? 75 : 90 }}
                      value={payTspCode ?? ""}
                      onChange={(e) => {
                        const v = e.target.value.trim();
                        setPayTspCode(v ? Number(v) : undefined);
                      }}
                    />
                    <Select
                      size="small"
                      style={{ width: 75 }}
                      value={payTop}
                      onChange={setPayTop}
                      options={TOP_OPTIONS}
                      placeholder="ТОП"
                    />
                  </div>
                </div>

                <div style={{ display: "flex", alignItems: "center", gap: 8, marginLeft: isXs ? 0 : "auto" }}>
                  <Tag color="blue" style={{ margin: 0, fontSize: isXs ? 11 : 12 }}>
                    {isXs ? getTimezoneBadgeText(tenantTz) : `Часовой пояс: ${getTimezoneBadgeText(tenantTz)}`}
                  </Tag>
                  <Button
                    size="small"
                    icon={<ReloadOutlined />}
                    onClick={fetchPayments}
                  />
                </div>
              </div>

              <Table<PaymentRecord>
                rowKey="paym_id"
                columns={paymentsColumns}
                dataSource={payItems}
                loading={payLoading}
                size="small"
                className="reports-table"
                tableLayout="fixed"
                pagination={false}
                scroll={{ x: 920 }}
                expandable={{
                  expandedRowRender: (r) => (
                    <div style={{ padding: "8px 0", overflowX: "auto", maxWidth: "100%" }}>
                      {r.params.length === 0 ? (
                        <span style={{ color: "#999", fontSize: 12 }}>
                          Нет параметров
                        </span>
                      ) : (
                        <Space
                          direction="vertical"
                          size={2}
                          style={{ width: "100%" }}
                        >
                          <div style={{ fontWeight: 500, marginBottom: 4, fontSize: 12 }}>
                            Параметры платежа
                          </div>
                          <table
                            style={{
                              fontSize: 12,
                              borderCollapse: "collapse",
                              minWidth: 320,
                            }}
                          >
                            <thead>
                              <tr>
                                <th
                                  style={{
                                    textAlign: "left",
                                    paddingRight: 16,
                                    color: "#888",
                                    fontWeight: 500,
                                  }}
                                >
                                  Код
                                </th>
                                <th
                                  style={{
                                    textAlign: "left",
                                    paddingRight: 16,
                                    color: "#888",
                                    fontWeight: 500,
                                  }}
                                >
                                  Описание
                                </th>
                                <th
                                  style={{
                                    textAlign: "left",
                                    color: "#888",
                                    fontWeight: 500,
                                  }}
                                >
                                  Значение
                                </th>
                              </tr>
                            </thead>
                            <tbody>
                              {r.params.map((p, i) => (
                                <tr key={i}>
                                <td style={{ paddingRight: 16 }}>{p.code}</td>
                                <td style={{ paddingRight: 16 }}>
                                  {p.description || "—"}
                                </td>
                                <td>{p.value || "—"}</td>
                              </tr>
                            ))}
                          </tbody>
                        </table>
                      </Space>
                    )}
                  </div>
                ),
              }}
            />
          </div>
        )}

          {/* ====== Balance by terminal report ====== */}
          {activeReport === "balance-terminal" && (
            <div
              style={{ background: "#fff", borderRadius: 8, padding: isXs ? 8 : 12 }}
            >
              <div
                style={{
                  marginBottom: 12,
                  display: "flex",
                  justifyContent: "space-between",
                  alignItems: "center",
                  flexWrap: "wrap",
                  gap: 8,
                }}
              >
                <div
                  style={{
                    display: "flex",
                    flexWrap: "wrap",
                    gap: 8,
                    alignItems: "center",
                    flex: 1,
                    minWidth: isXs ? "100%" : 320,
                  }}
                >
                  <div style={{ display: "flex", gap: 6, flex: isXs ? "1 1 100%" : undefined }}>
                    <DatePicker
                      placeholder="Дата с"
                      size="small"
                      style={{ flex: 1 }}
                      value={dayjs(btDateFrom || todayStr)}
                      allowClear={false}
                      onChange={(d) =>
                        setBtDateFrom(d ? d.format("YYYY-MM-DD") : todayStr)
                      }
                    />
                    <DatePicker
                      placeholder="Дата по"
                      size="small"
                      style={{ flex: 1 }}
                      value={dayjs(btDateTo || todayStr)}
                      allowClear={false}
                      onChange={(d) =>
                        setBtDateTo(d ? d.format("YYYY-MM-DD") : todayStr)
                      }
                    />
                  </div>
                  <div style={{ display: "flex", gap: 6, flex: isXs ? "1 1 100%" : undefined }}>
                    <Input
                      placeholder="ID терминалов"
                      size="small"
                      style={{ flex: 1, minWidth: 120, maxWidth: isXs ? undefined : 200 }}
                      value={btTermInput}
                      onChange={(e) => setBtTermInput(e.target.value)}
                      onPressEnter={handleBtTermSearch}
                      allowClear
                    />
                    <Button
                      size="small"
                      type="primary"
                      icon={<SearchOutlined />}
                      onClick={handleBtTermSearch}
                    >
                      {!isXs && "Найти"}
                    </Button>
                  </div>
                </div>

                <div style={{ display: "flex", alignItems: "center", gap: 8, marginLeft: isXs ? 0 : "auto" }}>
                  <Tag color="blue" style={{ margin: 0, fontSize: isXs ? 11 : 12 }}>
                    {isXs ? getTimezoneBadgeText(tenantTz) : `Часовой пояс: ${getTimezoneBadgeText(tenantTz)}`}
                  </Tag>
                  <Button
                    size="small"
                    icon={<ReloadOutlined />}
                    onClick={fetchBalanceByTerminal}
                  />
                </div>
              </div>

              <div style={{ maxWidth: "100%" }}>
                <Table<BalanceByTerminalRecord>
                  rowKey="device_id"
                  columns={balanceTerminalColumns}
                  dataSource={btItems}
                  loading={btLoading}
                  size="small"
                  className="reports-table"
                  tableLayout="fixed"
                  scroll={{ x: 450 }}
                  pagination={{
                    defaultPageSize: 50,
                    pageSize: 50,
                    simple: isXs,
                    showSizeChanger: !isXs,
                    pageSizeOptions: ["10", "20", "50", "100"],
                    showTotal: (total, range) =>
                      isXs ? `${range[0]}-${range[1]}/${total}` : `${range[0]}-${range[1]} из ${total} записей`,
                  }}
                />
              </div>
            </div>
          )}

          {/* ====== Balance by TSP report ====== */}
          {activeReport === "balance-tsp" && (
            <div
              style={{ background: "#fff", borderRadius: 8, padding: isXs ? 8 : 12 }}
            >
              <div
                style={{
                  marginBottom: 12,
                  display: "flex",
                  justifyContent: "space-between",
                  alignItems: "center",
                  flexWrap: "wrap",
                  gap: 8,
                }}
              >
                <div
                  style={{
                    display: "flex",
                    flexWrap: "wrap",
                    gap: 8,
                    alignItems: "center",
                    flex: 1,
                    minWidth: isXs ? "100%" : 320,
                  }}
                >
                  <div style={{ display: "flex", gap: 6, flex: isXs ? "1 1 100%" : undefined }}>
                    <DatePicker
                      placeholder="Дата с"
                      size="small"
                      style={{ flex: 1 }}
                      value={dayjs(btsDateFrom || todayStr)}
                      allowClear={false}
                      onChange={(d) =>
                        setBtsDateFrom(d ? d.format("YYYY-MM-DD") : todayStr)
                      }
                    />
                    <DatePicker
                      placeholder="Дата по"
                      size="small"
                      style={{ flex: 1 }}
                      value={dayjs(btsDateTo || todayStr)}
                      allowClear={false}
                      onChange={(d) =>
                        setBtsDateTo(d ? d.format("YYYY-MM-DD") : todayStr)
                      }
                    />
                  </div>
                  <div style={{ display: "flex", gap: 6, flex: isXs ? "1 1 100%" : undefined }}>
                    <Input
                      placeholder="ID терминалов"
                      size="small"
                      style={{ flex: 1, minWidth: 120, maxWidth: isXs ? undefined : 200 }}
                      value={btsTermInput}
                      onChange={(e) => setBtsTermInput(e.target.value)}
                      onPressEnter={handleBtsTermSearch}
                      allowClear
                    />
                    <Button
                      size="small"
                      type="primary"
                      icon={<SearchOutlined />}
                      onClick={handleBtsTermSearch}
                    >
                      {!isXs && "Найти"}
                    </Button>
                  </div>
                </div>

                <div style={{ display: "flex", alignItems: "center", gap: 8, marginLeft: isXs ? 0 : "auto" }}>
                  <Tag color="blue" style={{ margin: 0, fontSize: isXs ? 11 : 12 }}>
                    {isXs ? getTimezoneBadgeText(tenantTz) : `Часовой пояс: ${getTimezoneBadgeText(tenantTz)}`}
                  </Tag>
                  <Button
                    size="small"
                    icon={<ReloadOutlined />}
                    onClick={fetchBalanceByTsp}
                  />
                </div>
              </div>

              <div style={{ maxWidth: "100%" }}>
                <Table<BalanceByTspRecord>
                  rowKey={(r) => `${r.tsp_code}_${r.version ?? 0}`}
                  columns={balanceTspColumns}
                  dataSource={btsItems}
                  loading={btsLoading}
                  size="small"
                  className="reports-table"
                  tableLayout="fixed"
                  scroll={{ x: 720 }}
                  pagination={{
                    defaultPageSize: 50,
                    pageSize: 50,
                    simple: isXs,
                    showSizeChanger: !isXs,
                    pageSizeOptions: ["10", "20", "50", "100"],
                    showTotal: (total, range) =>
                      isXs ? `${range[0]}-${range[1]}/${total}` : `${range[0]}-${range[1]} из ${total} записей`,
                  }}
                />
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
