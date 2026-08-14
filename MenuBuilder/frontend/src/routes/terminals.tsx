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
import { LinkOutlined, DisconnectOutlined } from "@ant-design/icons";
import {
  getTerminals,
  createOrUpdateBinding,
  deleteBinding,
  TerminalInfo,
} from "../api/terminal-bindings";
import { getMenuVariants, MenuVariant } from "../api/menu-variants";

const { Title, Text } = Typography;

export default function TerminalsPage() {
  const [terminals, setTerminals] = useState<TerminalInfo[]>([]);
  const [total, setTotal] = useState(0);
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(20);
  const [loading, setLoading] = useState(true);
  const [variants, setVariants] = useState<MenuVariant[]>([]);

  // Modal state
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
    } catch {
      message.error("Ошибка загрузки");
    } finally {
      setLoading(false);
    }
  }, [page, pageSize]);

  useEffect(() => { load(); }, [load]);

  const openBindModal = (terminal: TerminalInfo) => {
    setModalTerminal(terminal);
    setModalVariantId(terminal.menu_variant_id);
    setModalOpen(true);
  };

  const handleSave = async () => {
    if (!modalTerminal || !modalVariantId) {
      message.warning("Выберите вариант меню");
      return;
    }
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
      message.success(`Привязка терминала ${terminal.device_id} снята`);
      load();
    } catch (e: any) {
      message.error(e.message);
    }
  };

  const columns = [
    {
      title: "Device ID",
      dataIndex: "device_id",
      key: "device_id",
      width: 100,
    },
    {
      title: "SN",
      dataIndex: "sn",
      key: "sn",
      ellipsis: true,
    },
    {
      title: "Org",
      dataIndex: "org_id",
      key: "org_id",
      width: 60,
    },
    {
      title: "Активен",
      dataIndex: "is_active",
      key: "is_active",
      width: 90,
      render: (v: boolean) => v ? <Tag color="green">Да</Tag> : <Tag color="red">Нет</Tag>,
    },
    {
      title: "Вариант меню",
      key: "variant",
      render: (_: any, record: TerminalInfo) =>
        record.menu_variant_name ? (
          <Tag color="blue">{record.menu_variant_name}</Tag>
        ) : (
          <Tag>Не привязан</Tag>
        ),
    },
    {
      title: "",
      key: "actions",
      width: 120,
      render: (_: any, record: TerminalInfo) => (
        <Space>
          <Button
            type="link"
            size="small"
            icon={<LinkOutlined />}
            onClick={() => openBindModal(record)}
          >
            {record.binding_id ? "Изменить" : "Привязать"}
          </Button>
          {record.binding_id && (
            <Button
              type="link"
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
      <div style={{ display: "flex", justifyContent: "space-between", marginBottom: 16 }}>
        <Title level={3} style={{ margin: 0 }}>
          Терминалы
        </Title>
        <Text type="secondary">{total} шт.</Text>
      </div>

      <Card>
        <Table
          dataSource={terminals}
          columns={columns}
          rowKey="terminal_id"
          loading={loading}
          pagination={{
            current: page,
            pageSize,
            total,
            showSizeChanger: true,
            pageSizeOptions: ["10", "20", "50", "100"],
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
      >
        {modalTerminal && (
          <div style={{ marginBottom: 16 }}>
            <p><Text type="secondary">SN:</Text> {modalTerminal.sn}</p>
            <p><Text type="secondary">Org:</Text> {modalTerminal.org_id}</p>
            <p>
              <Text type="secondary">Статус:</Text>{" "}
              {modalTerminal.is_active ? <Tag color="green">Активен</Tag> : <Tag color="red">Неактивен</Tag>}
            </p>
          </div>
        )}
        <div>
          <Text strong style={{ display: "block", marginBottom: 8 }}>Вариант меню:</Text>
          <Select
            placeholder="Выберите вариант"
            value={modalVariantId}
            onChange={setModalVariantId}
            style={{ width: "100%" }}
            options={variants.map((v) => ({ value: v.id, label: v.name }))}
            showSearch
            optionFilterProp="label"
          />
        </div>
      </Modal>
    </>
  );
}
