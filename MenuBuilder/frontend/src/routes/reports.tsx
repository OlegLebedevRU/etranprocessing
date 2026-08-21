import { useEffect, useState, useCallback } from "react";
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
  const [payItems, setPayItems] = useState<PaymentRecord[]>([]);
  const [payTotal, setPayTotal] = useState(0);
  const [payLoading, setPayLoading] = useState(false);
  const [payDateFrom, setPayDateFrom] = useState("");
  const [payDateTo, setPayDateTo] = useState("");
  const [payTermInput, setPayTermInput] = useState("");
  const [payDeviceIds, setPayDeviceIds] = useState<number[]>([]);
  const [payTspCode, setPayTspCode] = useState<number | undefined>();
  const [payState, setPayState] = useState<number>(-1);
  const [payTop, setPayTop] = useState(100);

  // --- Balance by terminal state ---
  const [btItems, setBtItems] = useState<BalanceByTerminalRecord[]>([]);
  const [btLoading, setBtLoading] = useState(false);
  const [btDateFrom, setBtDateFrom] = useState("");
  const [btDateTo, setBtDateTo] = useState("");
  const [btTermInput, setBtTermInput] = useState("");
  const [btDeviceIds, setBtDeviceIds] = useState<number[]>([]);
  const [btTspCode, setBtTspCode] = useState<number | undefined>();

  // --- Balance by TSP state ---
  const [btsItems, setBtsItems] = useState<BalanceByTspRecord[]>([]);
  const [btsLoading, setBtsLoading] = useState(false);
  const [btsDateFrom, setBtsDateFrom] = useState("");
  const [btsDateTo, setBtsDateTo] = useState("");
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
        size: 100,
      });
      setInkItems(resp.items);
      setInkTotal(resp.total);
    } finally {
      setInkLoading(false);
    }
  }, [inkDateFrom, inkDateTo, inkDeviceIds, inkPage]);

  useEffect(() => {
    if (activeReport === "inkass") fetchInkass();
  }, [fetchInkass, activeReport]);

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
        date_from: payDateFrom || undefined,
        date_to: payDateTo || undefined,
        device_ids: payDeviceIds.length ? payDeviceIds : undefined,
        tsp_code: payTspCode,
        paym_state: payState >= 0 ? payState : undefined,
        top: payTop,
      });
      setPayItems(resp.items);
      setPayTotal(resp.total);
    } finally {
      setPayLoading(false);
    }
  }, [payDateFrom, payDateTo, payDeviceIds, payTspCode, payState, payTop]);

  useEffect(() => {
    if (activeReport === "payments") fetchPayments();
  }, [fetchPayments, activeReport]);

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
        date_from: btDateFrom || undefined,
        date_to: btDateTo || undefined,
        device_ids: btDeviceIds.length ? btDeviceIds : undefined,
        tsp_code: btTspCode,
      });
      setBtItems(resp.items);
    } finally {
      setBtLoading(false);
    }
  }, [btDateFrom, btDateTo, btDeviceIds, btTspCode]);

  useEffect(() => {
    if (activeReport === "balance-terminal") fetchBalanceByTerminal();
  }, [fetchBalanceByTerminal, activeReport]);

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
        date_from: btsDateFrom || undefined,
        date_to: btsDateTo || undefined,
        device_ids: btsDeviceIds.length ? btsDeviceIds : undefined,
      });
      setBtsItems(resp.items);
    } finally {
      setBtsLoading(false);
    }
  }, [btsDateFrom, btsDateTo, btsDeviceIds]);

  useEffect(() => {
    if (activeReport === "balance-tsp") fetchBalanceByTsp();
  }, [fetchBalanceByTsp, activeReport]);

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
      width: 90,
      sorter: (a, b) => a.device_id - b.device_id,
    },
    {
      title: "Время",
      dataIndex: "inkass_datetime",
      width: 150,
      sorter: (a, b) => a.inkass_datetime.localeCompare(b.inkass_datetime),
      defaultSortOrder: "descend",
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
      width: 180,
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
      width: 180,
      ellipsis: true,
      render: (v: string | undefined, r: PaymentRecord) =>
        v || String(r.paym_tsp_code),
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
      width: 90,
      sorter: (a, b) => a.device_id - b.device_id,
    },
    {
      title: "SN",
      dataIndex: "sn",
      width: 120,
      ellipsis: true,
      render: (v: string) => (
        <span title={v}>{v ? v.substring(0, 12) + "…" : "—"}</span>
      ),
    },
    {
      title: "TSP (кол-во)",
      dataIndex: "tsp_count",
      width: 100,
      align: "right",
      sorter: (a, b) => a.tsp_count - b.tsp_count,
    },
    {
      title: "Платежей",
      dataIndex: "total_count",
      width: 100,
      align: "right",
      render: fmtInt,
      sorter: (a, b) => a.total_count - b.total_count,
    },
    {
      title: "Сумма",
      dataIndex: "total_amount",
      width: 130,
      align: "right",
      render: (v: number) => fmtMoney(v),
      sorter: (a, b) => a.total_amount - b.total_amount,
      defaultSortOrder: "descend",
    },
  ];

  // --- Balance by TSP columns ---
  const balanceTspColumns: ColumnsType<BalanceByTspRecord> = [
    {
      title: "TSP код",
      dataIndex: "tsp_code",
      width: 90,
      sorter: (a, b) => a.tsp_code - b.tsp_code,
    },
    {
      title: "Название",
      dataIndex: "tsp_name",
      width: 200,
      ellipsis: true,
    },
    {
      title: "Терминалов",
      dataIndex: "terminal_count",
      width: 100,
      align: "right",
      sorter: (a, b) => a.terminal_count - b.terminal_count,
    },
    {
      title: "Платежей",
      dataIndex: "total_count",
      width: 100,
      align: "right",
      render: fmtInt,
      sorter: (a, b) => a.total_count - b.total_count,
    },
    {
      title: "Сумма",
      dataIndex: "total_amount",
      width: 130,
      align: "right",
      render: (v: number) => fmtMoney(v),
      sorter: (a, b) => a.total_amount - b.total_amount,
      defaultSortOrder: "descend",
    },
  ];

  return (
    <Layout style={{ background: "transparent" }}>
      <Sider
        width={170}
        theme="light"
        style={{
          border: "1px solid #eceff3",
          borderRadius: 10,
          marginRight: 12,
          padding: 6,
          alignSelf: "flex-start",
          position: "sticky",
          top: 52,
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
      </Sider>
      <Content>
        {/* ====== Inkass report ====== */}
        {activeReport === "inkass" && (
          <div
            style={{ background: "#fff", borderRadius: 8, padding: 12 }}
          >
            <Space
              style={{
                marginBottom: 12,
                display: "flex",
                justifyContent: "space-between",
              }}
            >
              <Space wrap>
                <DatePicker
                  placeholder="Дата с"
                  size="small"
                  onChange={(d) =>
                    setInkDateFrom(d ? d.format("YYYY-MM-DD") : "")
                  }
                />
                <DatePicker
                  placeholder="Дата по"
                  size="small"
                  onChange={(d) =>
                    setInkDateTo(d ? d.format("YYYY-MM-DD") : "")
                  }
                />
                <Input
                  placeholder="ID терминалов через запятую"
                  size="small"
                  style={{ width: 220 }}
                  value={inkTermInput}
                  onChange={(e) => setInkTermInput(e.target.value)}
                  onPressEnter={handleInkTermSearch}
                />
                <Button
                  size="small"
                  icon={<SearchOutlined />}
                  onClick={handleInkTermSearch}
                >
                  Найти
                </Button>
              </Space>
              <Button
                size="small"
                icon={<ReloadOutlined />}
                onClick={fetchInkass}
              />
            </Space>

            <Table<InkassRecord>
              rowKey="id"
              columns={inkassColumns}
              dataSource={inkItems}
              loading={inkLoading}
              size="small"
              tableLayout="auto"
              pagination={{
                current: inkPage,
                pageSize: 100,
                total: inkTotal,
                onChange: setInkPage,
                showTotal: (t) => `Всего: ${t}`,
                showSizeChanger: false,
              }}
              scroll={{ x: 800 }}
              expandable={{
                expandedRowRender: (r) => (
                  <div style={{ padding: "8px 0" }}>
                    <div style={{ fontSize: 13, color: "#1f2937" }}>
                      <span style={{ fontWeight: 600 }}>Банкноты: </span>
                      <span>
                        10р = {r.banknotes?.n10 ?? 0}, 50р = {r.banknotes?.n50 ?? 0},{" "}
                        100р = {r.banknotes?.n100 ?? 0}, 500р = {r.banknotes?.n500 ?? 0},{" "}
                        1000р = {r.banknotes?.n1000 ?? 0}, 2000р = {r.banknotes?.n2000 ?? 0},{" "}
                        5000р = {r.banknotes?.n5000 ?? 0}
                      </span>
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
            style={{ background: "#fff", borderRadius: 8, padding: 12 }}
          >
            <Space
              style={{
                marginBottom: 12,
                display: "flex",
                justifyContent: "space-between",
              }}
            >
              <Space wrap>
                <DatePicker
                  placeholder="Дата с"
                  size="small"
                  onChange={(d) =>
                    setPayDateFrom(d ? d.format("YYYY-MM-DD") : "")
                  }
                />
                <DatePicker
                  placeholder="Дата по"
                  size="small"
                  onChange={(d) =>
                    setPayDateTo(d ? d.format("YYYY-MM-DD") : "")
                  }
                />
                <Input
                  placeholder="ID терминалов через запятую"
                  size="small"
                  style={{ width: 200 }}
                  value={payTermInput}
                  onChange={(e) => setPayTermInput(e.target.value)}
                  onPressEnter={handlePayTermSearch}
                />
                <Button
                  size="small"
                  icon={<SearchOutlined />}
                  onClick={handlePayTermSearch}
                >
                  Найти
                </Button>
                <Input
                  placeholder="TSP код"
                  size="small"
                  style={{ width: 90 }}
                  value={payTspCode ?? ""}
                  onChange={(e) => {
                    const v = e.target.value.trim();
                    setPayTspCode(v ? Number(v) : undefined);
                  }}
                />
                <Select
                  size="small"
                  style={{ width: 130 }}
                  value={payState}
                  onChange={setPayState}
                  options={PAYM_STATE_OPTIONS}
                />
                <Select
                  size="small"
                  style={{ width: 80 }}
                  value={payTop}
                  onChange={setPayTop}
                  options={TOP_OPTIONS}
                  placeholder="ТОП"
                />
              </Space>
              <Button
                size="small"
                icon={<ReloadOutlined />}
                onClick={fetchPayments}
              />
            </Space>

            <Table<PaymentRecord>
              rowKey="paym_id"
              columns={paymentsColumns}
              dataSource={payItems}
              loading={payLoading}
              size="small"
              tableLayout="auto"
              pagination={false}
              scroll={{ x: 900 }}
              expandable={{
                expandedRowRender: (r) => (
                  <div style={{ padding: "8px 0" }}>
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
            style={{ background: "#fff", borderRadius: 8, padding: 12 }}
          >
            <Space
              style={{
                marginBottom: 12,
                display: "flex",
                justifyContent: "space-between",
              }}
            >
              <Space wrap>
                <DatePicker
                  placeholder="Дата с"
                  size="small"
                  onChange={(d) =>
                    setBtDateFrom(d ? d.format("YYYY-MM-DD") : "")
                  }
                />
                <DatePicker
                  placeholder="Дата по"
                  size="small"
                  onChange={(d) =>
                    setBtDateTo(d ? d.format("YYYY-MM-DD") : "")
                  }
                />
                <Input
                  placeholder="ID терминалов через запятую"
                  size="small"
                  style={{ width: 200 }}
                  value={btTermInput}
                  onChange={(e) => setBtTermInput(e.target.value)}
                  onPressEnter={handleBtTermSearch}
                />
                <Button
                  size="small"
                  icon={<SearchOutlined />}
                  onClick={handleBtTermSearch}
                >
                  Найти
                </Button>
                <Input
                  placeholder="TSP код"
                  size="small"
                  style={{ width: 90 }}
                  value={btTspCode ?? ""}
                  onChange={(e) => {
                    const v = e.target.value.trim();
                    setBtTspCode(v ? Number(v) : undefined);
                  }}
                />
              </Space>
              <Button
                size="small"
                icon={<ReloadOutlined />}
                onClick={fetchBalanceByTerminal}
              />
            </Space>

            <Table<BalanceByTerminalRecord>
              rowKey="terminal_id"
              columns={balanceTerminalColumns}
              dataSource={btItems}
              loading={btLoading}
              size="small"
              tableLayout="auto"
              pagination={{
                pageSize: 20,
                showSizeChanger: true,
                pageSizeOptions: ["10", "20", "50", "100"],
                showTotal: (total, range) =>
                  `${range[0]}-${range[1]} из ${total} записей`,
              }}
              scroll={{ x: 500 }}
            />
          </div>
        )}

        {/* ====== Balance by TSP report ====== */}
        {activeReport === "balance-tsp" && (
          <div
            style={{ background: "#fff", borderRadius: 8, padding: 12 }}
          >
            <Space
              style={{
                marginBottom: 12,
                display: "flex",
                justifyContent: "space-between",
              }}
            >
              <Space wrap>
                <DatePicker
                  placeholder="Дата с"
                  size="small"
                  onChange={(d) =>
                    setBtsDateFrom(d ? d.format("YYYY-MM-DD") : "")
                  }
                />
                <DatePicker
                  placeholder="Дата по"
                  size="small"
                  onChange={(d) =>
                    setBtsDateTo(d ? d.format("YYYY-MM-DD") : "")
                  }
                />
                <Input
                  placeholder="ID терминалов через запятую"
                  size="small"
                  style={{ width: 200 }}
                  value={btsTermInput}
                  onChange={(e) => setBtsTermInput(e.target.value)}
                  onPressEnter={handleBtsTermSearch}
                />
                <Button
                  size="small"
                  icon={<SearchOutlined />}
                  onClick={handleBtsTermSearch}
                >
                  Найти
                </Button>
              </Space>
              <Button
                size="small"
                icon={<ReloadOutlined />}
                onClick={fetchBalanceByTsp}
              />
            </Space>

            <Table<BalanceByTspRecord>
              rowKey="tsp_code"
              columns={balanceTspColumns}
              dataSource={btsItems}
              loading={btsLoading}
              size="small"
              tableLayout="auto"
              pagination={{
                pageSize: 20,
                showSizeChanger: true,
                pageSizeOptions: ["10", "20", "50", "100"],
                showTotal: (total, range) =>
                  `${range[0]}-${range[1]} из ${total} записей`,
              }}
              scroll={{ x: 600 }}
            />
          </div>
        )}
      </Content>
    </Layout>
  );
}
