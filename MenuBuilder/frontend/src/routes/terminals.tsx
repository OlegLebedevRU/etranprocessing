import { useEffect, useState, useCallback } from "react";
import {
  Button,
  Card,
  Input,
  message,
  Modal,
  Select,
  Space,
  Table,
  Tag,
  Typography,
  Tooltip,
  Grid,
} from "antd";
import {
  LinkOutlined,
  DisconnectOutlined,
  EditOutlined,
  ReloadOutlined,
  SearchOutlined,
  CopyOutlined,
} from "@ant-design/icons";
import {
  getTerminals,
  createOrUpdateBinding,
  deleteBinding,
  TerminalInfo,
} from "../api/terminal-bindings";
import { getMenuVariants, MenuVariant } from "../api/menu-variants";
import { getStats } from "../api/stats";
import PageHeader from "../components/PageHeader";

const { Text } = Typography;
const { useBreakpoint } = Grid;

interface VariantStats {
  groups: number;
  services: number;
}

export default function TerminalsPage() {
  const screens = useBreakpoint();
  const isMobile = !screens.md;

  const [terminals, setTerminals] = useState<TerminalInfo[]>([]);
  const [total, setTotal] = useState(0);
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(50);
  const [search, setSearch] = useState("");
  const [searchInput, setSearchInput] = useState("");
  const [loading, setLoading] = useState(true);
  const [variants, setVariants] = useState<MenuVariant[]>([]);
  const [statsMap, setStatsMap] = useState<Record<number, VariantStats>>({});

  const [modalOpen, setModalOpen] = useState(false);
  const [modalTerminal, setModalTerminal] = useState<TerminalInfo | null>(null);
  const [modalVariantId, setModalVariantId] = useState<number | null>(null);
  const [saving, setSaving] = useState(false);

  const load = useCallback(async () => {
    setLoading(true);
    try {
      const [termRes, varRes] = await Promise.all([
        getTerminals(page, pageSize, search),
        getMenuVariants(),
      ]);
      setTerminals(termRes.data.items);
      setTotal(termRes.data.total);
      setVariants(varRes.data);

      const boundVariantIds = [
        ...new Set(
          termRes.data.items
            .filter((t) => t.menu_variant_id)
            .map((t) => t.menu_variant_id!)
        ),
      ];
      const newStats: Record<number, VariantStats> = {};
      await Promise.all(
        boundVariantIds.map(async (vid) => {
          if (statsMap[vid]) {
            newStats[vid] = statsMap[vid];
            return;
          }
          try {
            const s = await getStats(vid);
            newStats[vid] = {
              groups: s.data.groups,
              services: s.data.services,
            };
          } catch {
            /* ignore */
          }
        })
      );
      setStatsMap((prev) => ({ ...prev, ...newStats }));
    } catch {
      message.error("Ошибка загрузки");
    } finally {
      setLoading(false);
    }
  }, [page, pageSize, search]);

  useEffect(() => {
    load();
  }, [load]);

  const handleSearch = () => {
    setPage(1);
    setSearch(searchInput.trim());
  };

  const openModal = (terminal: TerminalInfo) => {
    setModalTerminal(terminal);
    setModalVariantId(terminal.menu_variant_id);
    setModalOpen(true);
  };

  const handleSave = async () => {
    if (!modalTerminal || !modalVariantId) { message.warning("Выберите вариант"); return; }
    setSaving(true);
    try {
      await createOrUpdateBinding({ device_id: modalTerminal.device_id, menu_variant_id: modalVariantId });
      message.success(`Терминал ${modalTerminal.device_id} привязан`);
      setModalOpen(false);
      load();
    } catch (e: any) {
      message.error(e.message);
    } finally {
      setSaving(false);
    }
  };

  const handleUnbind = async (terminal: TerminalInfo) => {
    if (!terminal.binding_id) return;
    try {
      await deleteBinding(terminal.binding_id);
      message.success("Привязка снята");
      load();
    } catch (e: any) {
      message.error(e.message);
    }
  };

  const copyToClipboard = (text: string) => {
    navigator.clipboard.writeText(text).then(() => {
      message.success("Скопировано");
    });
  };

  const columns = [
    {
      title: "ID",
      dataIndex: "device_id",
      key: "device_id",
      width: 60,
      render: (v: number) => <Text strong style={{ fontSize: 12 }}>{v}</Text>,
    },
    {
      title: "Меню",
      key: "variant",
      render: (_: any, record: TerminalInfo) => {
        if (!record.menu_variant_name) return <Text type="secondary" style={{ fontSize: 11 }}>—</Text>;
        const stats = record.menu_variant_id ? statsMap[record.menu_variant_id] : null;

        let versionTag = null;
        if (record.is_latest) {
          versionTag = (
            <Tag color="success" style={{ margin: 0, fontSize: 10 }}>
              v{record.loaded_version} (актуальная)
            </Tag>
          );
        } else if (record.loaded_version) {
          versionTag = (
            <Tag color="warning" style={{ margin: 0, fontSize: 10 }}>
              v{record.loaded_version} (на сервере v{record.current_version ?? "?"})
            </Tag>
          );
        } else {
          versionTag = (
            <Tag color="default" style={{ margin: 0, fontSize: 10 }}>
              Не загружена (на сервере v{record.current_version ?? "?"})
            </Tag>
          );
        }

        return (
          <Space size={4} direction="vertical" style={{ gap: 2 }}>
            <Space size={4} style={{ flexWrap: "wrap" }}>
              <Tag color="blue" style={{ margin: 0, fontSize: 11 }}>{record.menu_variant_name}</Tag>
              {versionTag}
            </Space>
            {stats && (
              <Text type="secondary" style={{ fontSize: 10, whiteSpace: "nowrap" }}>
                Групп: {stats.groups} / Услуг: {stats.services}
              </Text>
            )}
          </Space>
        );
      },
    },
    {
      title: "SN",
      dataIndex: "sn",
      key: "sn",
      width: isMobile ? 110 : 160,
      render: (v: string) => {
        if (!v) return <Text type="secondary">—</Text>;
        const display = isMobile && v.length > 8 ? `${v.slice(0, 4)}…${v.slice(-3)}` : v;
        return (
          <Space size={3}>
            <Tooltip title={`Серийный номер: ${v}`} mouseEnterDelay={0.35}>
              <Text code style={{ fontSize: 11 }}>{display}</Text>
            </Tooltip>
            <Button
              type="text"
              size="small"
              icon={<CopyOutlined style={{ fontSize: 10 }} />}
              style={{ width: 20, height: 20, padding: 0 }}
              onClick={() => copyToClipboard(v)}
            />
          </Space>
        );
      },
    },
    {
      title: "",
      dataIndex: "is_active",
      key: "active",
      width: 36,
      align: "center" as const,
      render: (v: boolean) => (
        <Tooltip title={v ? "Активен" : "Неактивен"} mouseEnterDelay={0.35}>
          <span style={{ color: v ? "#52c41a" : "#ff4d4f", fontSize: 14 }}>●</span>
        </Tooltip>
      ),
    },
    {
      title: "",
      key: "actions",
      width: 70,
      align: "center" as const,
      render: (_: any, record: TerminalInfo) => (
        <Space size={0}>
          <Button
            type="text"
            size="small"
            icon={record.binding_id ? <EditOutlined /> : <LinkOutlined />}
            onClick={() => openModal(record)}
          />
          {record.binding_id && (
            <Button
              type="text"
              size="small"
              danger
              icon={<DisconnectOutlined />}
              onClick={() => handleUnbind(record)}
            />
          )}
        </Space>
      ),
    },
  ];

  return (
    <>
      <PageHeader
        title="Терминалы"
        subtitle="Привязка терминалов к вариантам меню"
        extra={
          <Space wrap style={{ width: isMobile ? "100%" : "auto" }}>
            <Input
              placeholder="Поиск ID / SN / адрес"
              prefix={<SearchOutlined />}
              value={searchInput}
              onChange={(e) => setSearchInput(e.target.value)}
              onPressEnter={handleSearch}
              allowClear
              style={{ width: isMobile ? "100%" : 220 }}
              size="small"
            />
            <Button size="small" type="primary" onClick={handleSearch}>
              Найти
            </Button>
            <Button
              size="small"
              icon={<ReloadOutlined />}
              onClick={load}
              loading={loading}
            >
              Обновить
            </Button>
          </Space>
        }
      />
      <Card styles={{ body: { padding: 0 } }}>
        <Table
          className="compact-table"
          dataSource={terminals}
          columns={columns}
          rowKey="terminal_id"
          loading={loading}
          size="small"
          scroll={{ x: 500 }}
          pagination={{
            current: page,
            pageSize,
            total,
            defaultPageSize: 50,
            showSizeChanger: true,
            pageSizeOptions: ["10", "20", "50", "100"],
            size: "small",
            simple: isMobile,
            showTotal: (t, range) => (
              <Text type="secondary" style={{ fontSize: 11 }}>
                {range[0]}-{range[1]} из {t} терм.
              </Text>
            ),
            onChange: (p, ps) => {
              setPage(p);
              setPageSize(ps);
            },
          }}
        />
      </Card>

      <Modal
        title={`Терминал ${modalTerminal?.device_id}`}
        open={modalOpen}
        onCancel={() => setModalOpen(false)}
        onOk={handleSave}
        confirmLoading={saving}
        okText="Прикрепить"
        cancelText="Отмена"
        width={340}
        style={{ maxWidth: "calc(100vw - 16px)" }}
      >
        {modalTerminal && (
          <div style={{ marginBottom: 10, fontSize: 12 }}>
            <p style={{ margin: "2px 0" }}><Text type="secondary">SN:</Text> <Text code style={{ fontSize: 11 }}>{modalTerminal.sn}</Text></p>
            <p style={{ margin: "2px 0" }}>
              <Text type="secondary">Статус:</Text>{" "}
              {modalTerminal.is_active ? <Tag color="success" style={{ fontSize: 11 }}>Активен</Tag> : <Tag color="error" style={{ fontSize: 11 }}>Неактивен</Tag>}
            </p>
          </div>
        )}
        <Select
          placeholder="Вариант меню"
          value={modalVariantId}
          onChange={setModalVariantId}
          style={{ width: "100%" }}
          options={variants.map((v) => ({
            value: v.id,
            label: `${v.name} (v${v.version ?? 1})`,
          }))}
          showSearch
          optionFilterProp="label"
        />
      </Modal>
    </>
  );
}
