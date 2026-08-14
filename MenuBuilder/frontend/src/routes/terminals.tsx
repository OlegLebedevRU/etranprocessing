import { useEffect, useState, useCallback } from "react";
import {
  Button,
  Card,
  message,
  Modal,
  Select,
  Space,
  Table,
  Tag,
  Typography,
} from "antd";
import {
  LinkOutlined,
  DisconnectOutlined,
  EditOutlined,
  FolderOutlined,
  AppstoreOutlined,
} from "@ant-design/icons";
import {
  getTerminals,
  createOrUpdateBinding,
  deleteBinding,
  TerminalInfo,
} from "../api/terminal-bindings";
import { getMenuVariants, MenuVariant } from "../api/menu-variants";
import { getStats } from "../api/stats";

const { Text } = Typography;

interface VariantStats {
  groups: number;
  services: number;
}

export default function TerminalsPage() {
  const [terminals, setTerminals] = useState<TerminalInfo[]>([]);
  const [total, setTotal] = useState(0);
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(20);
  const [loading, setLoading] = useState(true);
  const [variants, setVariants] = useState<MenuVariant[]>([]);
  const [statsMap, setStatsMap] = useState<Record<number, VariantStats>>({});

  // Modal
  const [modalOpen, setModalOpen] = useState(false);
  const [modalTerminal, setModalTerminal] = useState<TerminalInfo | null>(null);
  const [modalVariantId, setModalVariantId] = useState<number | null>(null);
  const [saving, setSaving] = useState(false);

  const load = useCallback(async () => {
    setLoading(true);
    try {
      const [termRes, varRes] = await Promise.all([getTerminals(page, pageSize), getMenuVariants()]);
      setTerminals(termRes.data.items);
      setTotal(termRes.data.total);
      setVariants(varRes.data);

      // Load stats for variants that are bound to terminals (deduplicated)
      const boundVariantIds = [...new Set(termRes.data.items.filter(t => t.menu_variant_id).map(t => t.menu_variant_id!))];
      const newStats: Record<number, VariantStats> = {};
      await Promise.all(
        boundVariantIds.map(async (vid) => {
          if (statsMap[vid]) { newStats[vid] = statsMap[vid]; return; }
          try {
            const s = await getStats(vid);
            newStats[vid] = { groups: s.data.groups, services: s.data.services };
          } catch { /* ignore */ }
        })
      );
      setStatsMap((prev) => ({ ...prev, ...newStats }));
    } catch {
      message.error("Ошибка загрузки");
    } finally {
      setLoading(false);
    }
  }, [page, pageSize]);

  useEffect(() => { load(); }, [load]);

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

  const columns = [
    {
      title: "ID",
      dataIndex: "device_id",
      key: "device_id",
      width: 52,
      render: (v: number) => <Text strong style={{ fontSize: 12 }}>{v}</Text>,
    },
    {
      title: "SN",
      dataIndex: "sn",
      key: "sn",
      ellipsis: true,
      render: (v: string) => <Text code style={{ fontSize: 10 }}>{v}</Text>,
    },
    {
      title: "Org",
      dataIndex: "org_id",
      key: "org_id",
      width: 40,
      align: "center" as const,
      render: (v: number) => <Text style={{ fontSize: 12 }}>{v}</Text>,
    },
    {
      title: "",
      dataIndex: "is_active",
      key: "active",
      width: 24,
      render: (v: boolean) => (
        <span style={{ color: v ? "#52c41a" : "#ff4d4f", fontSize: 14 }}>●</span>
      ),
    },
    {
      title: "Меню",
      key: "variant",
      render: (_: any, record: TerminalInfo) => {
        if (!record.menu_variant_name) return <Text type="secondary" style={{ fontSize: 11 }}>—</Text>;
        const stats = record.menu_variant_id ? statsMap[record.menu_variant_id] : null;
        return (
          <Space size={4} style={{ flexWrap: "nowrap" }}>
            <Tag color="blue" style={{ margin: 0, fontSize: 11 }}>{record.menu_variant_name}</Tag>
            {stats && (
              <Text type="secondary" style={{ fontSize: 10, whiteSpace: "nowrap" }}>
                <FolderOutlined style={{ fontSize: 10 }} />{stats.groups}
                <span style={{ margin: "0 2px" }}>·</span>
                <AppstoreOutlined style={{ fontSize: 10 }} />{stats.services}
              </Text>
            )}
          </Space>
        );
      },
    },
    {
      title: "",
      key: "actions",
      width: 52,
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
      <Card styles={{ body: { padding: 0 } }}>
        <Table
          dataSource={terminals}
          columns={columns}
          rowKey="terminal_id"
          loading={loading}
          size="small"
          pagination={{
            current: page,
            pageSize,
            total,
            showSizeChanger: true,
            pageSizeOptions: ["10", "20", "50", "100"],
            size: "small",
            showTotal: (t) => <Text type="secondary" style={{ fontSize: 11 }}>{t} терм.</Text>,
            onChange: (p, ps) => { setPage(p); setPageSize(ps); },
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
      >
        {modalTerminal && (
          <div style={{ marginBottom: 10, fontSize: 12 }}>
            <p style={{ margin: "2px 0" }}><Text type="secondary">SN:</Text> <Text code style={{ fontSize: 11 }}>{modalTerminal.sn}</Text></p>
            <p style={{ margin: "2px 0" }}><Text type="secondary">Org:</Text> {modalTerminal.org_id}</p>
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
          options={variants.map((v) => ({ value: v.id, label: v.name }))}
          showSearch
          optionFilterProp="label"
        />
      </Modal>
    </>
  );
}
