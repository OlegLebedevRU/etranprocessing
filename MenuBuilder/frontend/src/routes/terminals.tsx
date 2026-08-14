import { useEffect, useState, useCallback } from "react";
import {
  Button,
  Card,
  Empty,
  InputNumber,
  message,
  Popconfirm,
  Select,
  Space,
  Spin,
  Table,
  Typography,
} from "antd";
import { LinkOutlined, DeleteOutlined, PlusOutlined } from "@ant-design/icons";
import {
  getTerminalBindings,
  createOrUpdateBinding,
  deleteBinding,
  TerminalBinding,
} from "../api/terminal-bindings";
import { getMenuVariants, MenuVariant } from "../api/menu-variants";

const { Title, Text } = Typography;

export default function TerminalsPage() {
  const [bindings, setBindings] = useState<TerminalBinding[]>([]);
  const [variants, setVariants] = useState<MenuVariant[]>([]);
  const [loading, setLoading] = useState(true);

  // Form state
  const [deviceId, setDeviceId] = useState<number | null>(null);
  const [selectedVariant, setSelectedVariant] = useState<number | null>(null);
  const [attaching, setAttaching] = useState(false);

  const load = useCallback(() => {
    setLoading(true);
    Promise.all([getTerminalBindings(), getMenuVariants()])
      .then(([b, v]) => {
        setBindings(b.data);
        setVariants(v.data);
      })
      .catch(() => message.error("Ошибка загрузки"))
      .finally(() => setLoading(false));
  }, []);

  useEffect(() => { load(); }, [load]);

  const handleAttach = async () => {
    if (!deviceId || !selectedVariant) {
      message.warning("Укажите device_id и выберите вариант меню");
      return;
    }
    setAttaching(true);
    try {
      await createOrUpdateBinding({ device_id: deviceId, menu_variant_id: selectedVariant });
      message.success(`Терминал ${deviceId} привязан`);
      setDeviceId(null);
      setSelectedVariant(null);
      load();
    } catch (e: any) {
      message.error(e.message);
    } finally {
      setAttaching(false);
    }
  };

  const handleDelete = async (id: number) => {
    try {
      await deleteBinding(id);
      message.success("Привязка удалена");
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
      sorter: (a: TerminalBinding, b: TerminalBinding) => a.device_id - b.device_id,
    },
    {
      title: "Вариант меню",
      dataIndex: "menu_variant_name",
      key: "menu_variant_name",
      render: (v: string | null) => v || "—",
    },
    {
      title: "ID варианта",
      dataIndex: "menu_variant_id",
      key: "menu_variant_id",
    },
    {
      title: "",
      key: "actions",
      width: 60,
      render: (_: any, record: TerminalBinding) => (
        <Popconfirm
          title="Отвязать терминал?"
          onConfirm={() => handleDelete(record.id)}
          okText="Да"
          cancelText="Нет"
        >
          <Button type="text" danger icon={<DeleteOutlined />} />
        </Popconfirm>
      ),
    },
  ];

  return (
    <>
      <div style={{ display: "flex", justifyContent: "space-between", marginBottom: 16 }}>
        <Title level={3} style={{ margin: 0 }}>
          Терминалы
        </Title>
      </div>

      <Card style={{ marginBottom: 16 }}>
        <Title level={5} style={{ marginBottom: 12 }}>
          <LinkOutlined /> Прикрепить терминал к меню
        </Title>
        <Space wrap>
          <InputNumber
            placeholder="Device ID"
            value={deviceId}
            onChange={(v) => setDeviceId(v)}
            min={1}
            style={{ width: 160 }}
          />
          <Select
            placeholder="Вариант меню"
            value={selectedVariant}
            onChange={setSelectedVariant}
            style={{ width: 280 }}
            options={variants.map((v) => ({ value: v.id, label: v.name }))}
            showSearch
            optionFilterProp="label"
          />
          <Button
            type="primary"
            icon={<PlusOutlined />}
            loading={attaching}
            onClick={handleAttach}
          >
            Прикрепить
          </Button>
        </Space>
      </Card>

      <Card>
        {loading ? (
          <Spin style={{ display: "block", margin: "40px auto" }} />
        ) : bindings.length === 0 ? (
          <Empty description="Нет привязок" />
        ) : (
          <Table
            dataSource={bindings}
            columns={columns}
            rowKey="id"
            pagination={false}
          />
        )}
      </Card>
    </>
  );
}
