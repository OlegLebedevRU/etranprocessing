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
} from "antd";
import {
  FileTextOutlined,
  ReloadOutlined,
  SearchOutlined,
} from "@ant-design/icons";
import type { ColumnsType } from "antd/es/table";
import dayjs from "dayjs";
import { getInkass, type InkassRecord } from "../api/reports";

const { Sider, Content } = Layout;

const REPORTS = [
  { key: "inkass", icon: <FileTextOutlined />, label: "Инкассация" },
];

function fmtMoney(v: number, currency: number): string {
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

export default function ReportsPage() {
  const [activeReport, setActiveReport] = useState("inkass");
  const [items, setItems] = useState<InkassRecord[]>([]);
  const [total, setTotal] = useState(0);
  const [loading, setLoading] = useState(false);
  const [page, setPage] = useState(1);
  const [dateFrom, setDateFrom] = useState<string>("");
  const [dateTo, setDateTo] = useState<string>("");
  const [termInput, setTermInput] = useState("");
  const [deviceIds, setDeviceIds] = useState<number[]>([]);

  const fetchData = useCallback(async () => {
    setLoading(true);
    try {
      const resp = await getInkass({
        date_from: dateFrom || undefined,
        date_to: dateTo || undefined,
        device_ids: deviceIds.length ? deviceIds : undefined,
        page,
        size: 100,
      });
      setItems(resp.items);
      setTotal(resp.total);
    } finally {
      setLoading(false);
    }
  }, [dateFrom, dateTo, deviceIds, page]);

  useEffect(() => {
    fetchData();
  }, [fetchData]);

  const handleTermSearch = () => {
    const ids = termInput
      .split(",")
      .map((s) => parseInt(s.trim(), 10))
      .filter((n) => !isNaN(n));
    setDeviceIds(ids);
    setPage(1);
  };

  const columns: ColumnsType<InkassRecord> = [
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
      render: (v: number, r) => fmtMoney(v, r.currency),
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
        {activeReport === "inkass" && (
          <div
            style={{
              background: "#fff",
              borderRadius: 8,
              padding: 12,
            }}
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
                    setDateFrom(d ? d.format("YYYY-MM-DD") : "")
                  }
                />
                <DatePicker
                  placeholder="Дата по"
                  size="small"
                  onChange={(d) =>
                    setDateTo(d ? d.format("YYYY-MM-DD") : "")
                  }
                />
                <Input
                  placeholder="ID терминалов через запятую"
                  size="small"
                  style={{ width: 220 }}
                  value={termInput}
                  onChange={(e) => setTermInput(e.target.value)}
                  onPressEnter={handleTermSearch}
                />
                <Button
                  size="small"
                  icon={<SearchOutlined />}
                  onClick={handleTermSearch}
                >
                  Найти
                </Button>
              </Space>
              <Button
                size="small"
                icon={<ReloadOutlined />}
                onClick={fetchData}
              />
            </Space>

            <Table<InkassRecord>
              rowKey="id"
              columns={columns}
              dataSource={items}
              loading={loading}
              size="small"
              tableLayout="auto"
              pagination={{
                current: page,
                pageSize: 100,
                total,
                onChange: setPage,
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
                            Итого: {fmtMoney(r.total_note_sum, r.currency)}
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
                                Итого: {fmtMoney(r.total_coin_sum, r.currency)}
                              </Tag>
                            ) : null}
                          </Space>
                        </>
                      )}
                      <div style={{ marginTop: 8, color: "#666", fontSize: 12 }}>
                        Расч. сумма: {fmtMoney(r.cnt_inkass_sum, r.currency)} |
                        Всего инкассаций: {r.cnt_inkass} | Всего операций:{" "}
                        {r.cnt_transact} | Накопит. итог:{" "}
                        {fmtMoney(r.cnt_total_sum, r.currency)}
                      </div>
                    </Space>
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
