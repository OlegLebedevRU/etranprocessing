import { useEffect, useState, useCallback } from "react";
import {
  Button,
  Card,
  message,
  Modal,
  Select,
  Space,
  Statistic,
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
  MobileOutlined,
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

interface VariantStat {
  id: number;
  name: string;
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
  const [variantStats, setVariantStats] = useState<VariantStat[]>([]);

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

      // Load stats per variant
      const stats = await Promise.all(
        varRes.data.map(async (v) => {
          try {
            const s = await getStats(v.id);
            return { id: v.id, name: v.name, groups: s.data.groups, services: s.data.services };
          } catch {
            return { id: v.id, name: v.name, groups: 0, services: 0 };
          }
        })
      );
      setVariantStats(stats);
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

  // Count bound terminals per variant
  const boundCounts = terminals.reduce<Record<number, number>>((acc, t) => {
    if (t.menu_variant_id) acc[t.menu_variant_id] = (acc[t.menu_variant_id] || 0) + 1;
    return acc;
  }, {});

  const columns = [
    {
      title: "ID",
      dataIndex: "device_id",
      key: "device_id",
      width: 70,
      render: (v: number) => <Text strong>{v}</Text>,
    },
    {
      title: "SN",
      dataIndex: "sn",
      key: "sn",
      ellipsis: true,
      render: (v: string) => <Text code style={{ fontSize: 11 }}>{v}</Text>,
    },
    {
      title: "Org",
      dataIndex: "org_id",
      key: "org_id",
      width: 50,
      align: "center" as const,
    },
    {
      title: "",
      dataIndex: "is_active",
      key: "active",
      width: 30,
      render: (v: boolean) => v ? <Tag color="success" style={{ margin: 0 }}>●</Tag> : <Tag color="error" style={{ margin: 0 }}>●</Tag>,
    },
    {
      title: "Меню",
      key: "variant",
      render: (_: any, record: TerminalInfo) =>
        record.menu_variant_name ? (
          <Tag color="blue" style={{ margin: 0 }}>{record.menu_variant_name}</Tag>
        ) : (
          <Text type="secondary" style={{ fontSize: 11 }}>—</Text>
        ),
    },
    {
      title: "",
      key: "actions",
      width: 90,
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
      {/* Variant summary cards */}
      <div style={{ display: "flex", gap: 8, marginBottom: 12, flexWrap: "wrap" }}>
        {variantStats.map((vs) => (
          <Card
            key={vs.id}
            size="small"
            style={{ flex: "1 1 160px", maxWidth: 220, borderTop: "2px solid #1677ff" }}
            styles={{ body: { padding: "8px 12px" } }}
          >
            <Text strong style={{ fontSize: 13, display: "block", marginBottom: 4 }}>{vs.name}</Text>
            <div style={{ display: "flex", gap: 12 }}>
              <span style={{ fontSize: 11, color: "#888" }}>
                <FolderOutlined /> {vs.groups} гр.
              </span>
              <span style={{ fontSize: 11, color: "#888" }}>
                <AppstoreOutlined /> {vs.services} усл.
              </span>
              <span style={{ fontSize: 11, color: "#888" }}>
                <MobileOutlined /> {boundCounts[vs.id] || 0} терм.
              </span>
            </div>
          </Card>
        ))}
      </div>

      {/* Terminals table */}
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
            showTotal: (t) => <Text type="secondary" style={{ fontSize: 12 }}>{t} терм.</Text>,
            onChange: (p, ps) => { setPage(p); setPageSize(ps); },
          }}
        />
      </Card>

      {/* Bind modal */}
      <Modal
        title={`Терминал ${modalTerminal?.device_id}`}
        open={modalOpen}
        onCancel={() => setModalOpen(false)}
        onOk={handleSave}
        confirmLoading={saving}
        okText="Прикрепить"
        cancelText="Отмена"
        width={360}
      >
        {modalTerminal && (
          <div style={{ marginBottom: 12, fontSize: 12 }}>
            <p style={{ margin: "4px 0" }}><Text type="secondary">SN:</Text> <Text code>{modalTerminal.sn}</Text></p>
            <p style={{ margin: "4px 0" }}><Text type="secondary">Org:</Text> {modalTerminal.org_id}</p>
            <p style={{ margin: "4px 0" }}>
              <Text type="secondary">Статус:</Text>{" "}
              {modalTerminal.is_active ? <Tag color="success">Активен</Tag> : <Tag color="error">Неактивен</Tag>}
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
