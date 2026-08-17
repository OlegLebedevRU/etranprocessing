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
  ReloadOutlined,
  SearchOutlined,
} from "@ant-design/icons";
import type { ColumnsType } from "antd/es/table";
import {
  getInkass,
  type InkassRecord,
  getPayments,
  type PaymentRecord,
} from "../api/reports";

const { Sider, Content } = Layout;

const REPORTS = [
  { key: "inkass", icon: <FileTextOutlined />, label: "Инкассация" },
  { key: "payments", icon: <DollarOutlined />, label: "Платежи" },
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

  // --- Inkass columns ---
  const inkassColumns: ColumnsType<InkassRecord> = [
    {
      title: "ID",
      dataIndex: "device_id",
      width: 70,
      sorter: (a, b) => a.device_id - b.device_id,
    },
    {
      title: "SN",
      dataIndex: "sn",
      width: 100,
      ellipsis: true,
      render: (v: string) => (
        <span title={v}>{v ? v.substring(0, 10) + "…" : "—"}</span>
      ),
    },
    {
      title: "Дата инкассации",
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
      render: (v: number) => fmtMoney(v),
      sorter: (a, b) => a.total_sum - b.total_sum,
    },
    {
      title: "Купюры",
      width: 80,
      align: "right",
      render: (_: unknown, r) => fmtInt(r.total_note_count),
    },
    {
      title: "Монеты",
      width: 80,
      align: "right",
      render: (_: unknown, r) => fmtInt(r.total_coin_count),
    },
    {
      title: "Инкассатор",
      dataIndex: "inkassator",
      width: 120,
      ellipsis: true,
    },
    {
      title: "Кассета",
      dataIndex: "cassette_num",
      width: 90,
      ellipsis: true,
    },
    {
      title: "ID транзакции",
      dataIndex: "paym_ext_id",
      width: 160,
      ellipsis: true,
    },
    {
      title: "ID инкассации",
      dataIndex: "inkass_ext_id",
      width: 160,
      ellipsis: true,
    },
    {
      title: "Операций",
      dataIndex: "transact_count",
      width: 80,
      align: "right",
      render: fmtInt,
    },
    {
      title: "Ср-во время",
      dataIndex: "server_datetime",
      width: 150,
    },
  ];

  // --- Payments columns ---
  const paymentsColumns: ColumnsType<PaymentRecord> = [
    {
      title: "ID",
      dataIndex: "paym_id",
      width: 90,
      sorter: (a, b) => a.paym_id - b.paym_id,
    },
    {
      title: "Дата",
      dataIndex: "paym_datetime",
      width: 150,
      sorter: (a, b) => a.paym_datetime.localeCompare(b.paym_datetime),
      defaultSortOrder: "descend",
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
      title: "ExtID",
      dataIndex: "paym_ext_id",
      width: 160,
      ellipsis: true,
    },
    {
      title: "TSP",
      dataIndex: "paym_tsp_code",
      width: 70,
      sorter: (a, b) => a.paym_tsp_code - b.paym_tsp_code,
    },
    {
      title: "Терминал",
      dataIndex: "device_id",
      width: 80,
      sorter: (a, b) => a.device_id - b.device_id,
    },
    {
      title: "SN",
      dataIndex: "sn",
      width: 100,
      ellipsis: true,
      render: (v: string) => (
        <span title={v}>{v ? v.substring(0, 10) + "…" : "—"}</span>
      ),
    },
    {
      title: "Состояние",
      dataIndex: "paym_state_label",
      width: 110,
      render: (label: string, r) => (
        <Tag color={PAYM_STATE_COLORS[r.paym_state] || "default"}>{label}</Tag>
      ),
    },
    {
      title: "Тип оплаты",
      dataIndex: "pay_type_label",
      width: 100,
    },
  ];

  return (
    <Layout style={{ background: "transparent" }}>
      <Sider
        width={160}
        theme="light"
        style={{
          borderRight: "1px solid #f0f0f0",
          borderRadius: 8,
          marginRight: 8,
        }}
      >
        <div
          style={{
            padding: "12px 16px 8px",
            fontWeight: 600,
            fontSize: 14,
            borderBottom: "1px solid #f0f0f0",
            }}
        >
          Отчёты
        </div>
        <Menu
          mode="inline"
          selectedKeys={[activeReport]}
          onClick={({ key }) => setActiveReport(key)}
          items={REPORTS}
          style={{ border: "none" }}
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
                    <Space
                      direction="vertical"
                      size={4}
                      style={{ width: "100%" }}
                    >
                      <div style={{ fontWeight: 500, marginBottom: 4 }}>
                        Купюры
                      </div>
                      <Space wrap size={[8, 4]}>
                        {NOTE_LABELS.map((label, i) =>
                          r.notes[i] ? (
                            <Tag key={i}>
                              {label} ₽: {r.notes[i]} шт
                            </Tag>
                          ) : null
                        )}
                        {r.total_note_sum ? (
                          <Tag color="blue">
                            Итого: {fmtMoney(r.total_note_sum)}
                          </Tag>
                        ) : null}
                      </Space>
                      {r.coins.some((c) => c > 0) && (
                        <>
                          <div style={{ fontWeight: 500, marginTop: 8 }}>
                            Монеты
                          </div>
                          <Space wrap size={[8, 4]}>
                            {COIN_LABELS.map((label, i) =>
                              r.coins[i] ? (
                                <Tag key={i}>
                                  {label} ₽: {r.coins[i]} шт
                                </Tag>
                              ) : null
                            )}
                            {r.total_coin_sum ? (
                              <Tag color="blue">
                                Итого: {fmtMoney(r.total_coin_sum)}
                              </Tag>
                            ) : null}
                          </Space>
                        </>
                      )}
                      <div style={{ marginTop: 8, color: "#666", fontSize: 12 }}>
                        Расч. сумма: {fmtMoney(r.cnt_inkass_sum)} |
                        Всего инкассаций: {r.cnt_inkass} | Всего операций:{" "}
                        {r.cnt_transact} | Накопит. итог:{" "}
                        {fmtMoney(r.cnt_total_sum)}
                      </div>
                    </Space>
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
      </Content>
    </Layout>
  );
}
