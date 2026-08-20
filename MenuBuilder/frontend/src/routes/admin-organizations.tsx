import { useEffect, useState } from "react";
import {
  Button,
  Card,
  Divider,
  Form,
  Input,
  InputNumber,
  Modal,
  Radio,
  Select,
  Space,
  Switch,
  Table,
  Tag,
  Typography,
  message,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import {
  EditOutlined,
  PlusOutlined,
  ReloadOutlined,
  SearchOutlined,
} from "@ant-design/icons";
import {
  createAdminOrganization,
  getAdminOrganizations,
  updateAdminOrganization,
  type AdminOrg,
  type AdminOrgCreateInput,
  type AdminOrgUpdateInput,
} from "../api/admin";

const { Title, Text } = Typography;

export default function AdminOrganizationsPage() {
  const [loading, setLoading] = useState(false);
  const [orgs, setOrgs] = useState<AdminOrg[]>([]);
  const [searchText, setSearchText] = useState("");

  // Create Modal state
  const [createModalOpen, setCreateModalOpen] = useState(false);
  const [createForm] = Form.useForm();
  const [createSubmitting, setCreateSubmitting] = useState(false);
  const [createCertMode, setCreateCertMode] = useState("none");

  // Edit Modal state
  const [editModalOpen, setEditModalOpen] = useState(false);
  const [editingOrg, setEditingOrg] = useState<AdminOrg | null>(null);
  const [editForm] = Form.useForm();
  const [editSubmitting, setEditSubmitting] = useState(false);
  const [editCertMode, setEditCertMode] = useState("none");

  const loadData = async () => {
    setLoading(true);
    try {
      const data = await getAdminOrganizations();
      setOrgs(data);
    } catch (err: any) {
      message.error(err.message || "Ошибка загрузки списка организаций");
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadData();
  }, []);

  const handleOpenCreate = () => {
    createForm.resetFields();
    createForm.setFieldsValue({
      org_name: "",
      name: "",
      is_active: true,
      monthly_price_rub: 1000,
      currency: "RUB",
      cert_billing_mode: "none",
      cert_price_rub: 500,
      tenant_pin_creation_enabled: false,
      cert_charge_primary_issue: true,
      cert_charge_reissue: true,
    });
    setCreateCertMode("none");
    setCreateModalOpen(true);
  };

  const handleCreateSubmit = async (values: any) => {
    setCreateSubmitting(true);
    try {
      const payload: AdminOrgCreateInput = {
        org_id: values.org_id ? Number(values.org_id) : undefined,
        org_name: values.org_name.trim(),
        name: values.name.trim(),
        is_active: values.is_active,
        monthly_price_minor: Math.round(Number(values.monthly_price_rub || 0) * 100),
        currency: values.currency || "RUB",
        cert_billing_mode: values.cert_billing_mode || "none",
        cert_price_minor:
          values.cert_billing_mode === "per_operation"
            ? Math.round(Number(values.cert_price_rub || 0) * 100)
            : null,
        tenant_pin_creation_enabled: Boolean(values.tenant_pin_creation_enabled),
        cert_charge_primary_issue: Boolean(values.cert_charge_primary_issue),
        cert_charge_reissue: Boolean(values.cert_charge_reissue),
      };

      await createAdminOrganization(payload);
      message.success("Организация успешно создана");
      setCreateModalOpen(false);
      loadData();
    } catch (err: any) {
      message.error(err.message || "Ошибка создания организации");
    } finally {
      setCreateSubmitting(false);
    }
  };

  const handleOpenEdit = (org: AdminOrg) => {
    setEditingOrg(org);
    setEditCertMode(org.cert_billing_mode);
    editForm.resetFields();
    editForm.setFieldsValue({
      org_name: org.org_name,
      name: org.name,
      is_active: org.is_active,
      monthly_price_rub: (org.monthly_price_minor / 100).toFixed(2),
      currency: org.currency,
      cert_billing_mode: org.cert_billing_mode,
      cert_price_rub: org.cert_price_minor ? (org.cert_price_minor / 100).toFixed(2) : 500,
      tenant_pin_creation_enabled: org.tenant_pin_creation_enabled,
      cert_charge_primary_issue: org.cert_charge_primary_issue,
      cert_charge_reissue: org.cert_charge_reissue,
    });
    setEditModalOpen(true);
  };

  const handleEditSubmit = async (values: any) => {
    if (!editingOrg) return;
    setEditSubmitting(true);
    try {
      const payload: AdminOrgUpdateInput = {
        org_name: values.org_name.trim(),
        name: values.name.trim(),
        is_active: values.is_active,
        monthly_price_minor: Math.round(Number(values.monthly_price_rub || 0) * 100),
        currency: values.currency,
        cert_billing_mode: values.cert_billing_mode,
        cert_price_minor:
          values.cert_billing_mode === "per_operation"
            ? Math.round(Number(values.cert_price_rub || 0) * 100)
            : null,
        tenant_pin_creation_enabled: Boolean(values.tenant_pin_creation_enabled),
        cert_charge_primary_issue: Boolean(values.cert_charge_primary_issue),
        cert_charge_reissue: Boolean(values.cert_charge_reissue),
      };

      await updateAdminOrganization(editingOrg.org_id, payload);
      message.success(`Организация #${editingOrg.org_id} успешно обновлена`);
      setEditModalOpen(false);
      loadData();
    } catch (err: any) {
      message.error(err.message || "Ошибка обновления организации");
    } finally {
      setEditSubmitting(false);
    }
  };

  const filteredOrgs = orgs.filter((o) => {
    if (!searchText) return true;
    const lower = searchText.toLowerCase();
    return (
      String(o.org_id).includes(lower) ||
      o.org_name.toLowerCase().includes(lower) ||
      o.name.toLowerCase().includes(lower)
    );
  });

  const columns: ColumnsType<AdminOrg> = [
    {
      title: "ID",
      dataIndex: "org_id",
      key: "org_id",
      width: 70,
      sorter: (a, b) => a.org_id - b.org_id,
      render: (val) => <Text strong>#{val}</Text>,
    },
    {
      title: "Организация",
      dataIndex: "org_name",
      key: "org_name",
      render: (text, record) => (
        <div>
          <div><Text strong>{text}</Text></div>
          <Text type="secondary" style={{ fontSize: 12 }}>{record.name}</Text>
        </div>
      ),
    },
    {
      title: "Статус",
      dataIndex: "is_active",
      key: "is_active",
      width: 110,
      render: (active) => (
        <Tag color={active ? "success" : "default"}>
          {active ? "Активна" : "Отключена"}
        </Tag>
      ),
    },
    {
      title: "Стоимость / мес.",
      dataIndex: "monthly_price_minor",
      key: "monthly_price_minor",
      width: 140,
      render: (val, record) => (
        <Text>
          {(val / 100).toLocaleString("ru-RU", { minimumFractionDigits: 2 })} {record.currency}
        </Text>
      ),
    },
    {
      title: "Тариф PIN / Cert",
      dataIndex: "cert_billing_mode",
      key: "cert_billing_mode",
      width: 160,
      render: (mode, record) => {
        if (mode === "per_operation") {
          const price = record.cert_price_minor ? record.cert_price_minor / 100 : 0;
          return (
            <div>
              <Tag color="blue">За операцию</Tag>
              <div style={{ fontSize: 12, color: "#666" }}>
                {price.toLocaleString("ru-RU")} {record.currency}
              </div>
            </div>
          );
        }
        return <Tag color="default">Бесплатно</Tag>;
      },
    },
    {
      title: "PIN клиентом",
      dataIndex: "tenant_pin_creation_enabled",
      key: "tenant_pin_creation_enabled",
      width: 120,
      render: (enabled) => (
        <Tag color={enabled ? "processing" : "default"}>
          {enabled ? "Включено" : "Выключено"}
        </Tag>
      ),
    },
    {
      title: "Первичный / Перевыпуск",
      key: "cert_charges",
      width: 170,
      render: (_, record) => (
        <Space size={4}>
          <Tag color={record.cert_charge_primary_issue ? "cyan" : "default"}>
            Выпуск: {record.cert_charge_primary_issue ? "Да" : "Нет"}
          </Tag>
          <Tag color={record.cert_charge_reissue ? "purple" : "default"}>
            Перевыпуск: {record.cert_charge_reissue ? "Да" : "Нет"}
          </Tag>
        </Space>
      ),
    },
    {
      title: "Действия",
      key: "actions",
      width: 120,
      align: "center",
      render: (_, record) => (
        <Button
          size="small"
          type="primary"
          ghost
          icon={<EditOutlined />}
          onClick={() => handleOpenEdit(record)}
        >
          Настройки
        </Button>
      ),
    },
  ];

  return (
    <Card bordered={false} style={{ borderRadius: 8 }}>
      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          alignItems: "center",
          marginBottom: 16,
          flexWrap: "wrap",
          gap: 12,
        }}
      >
        <div>
          <Title level={4} style={{ margin: 0 }}>
            Организации и параметры лицензирования
          </Title>
          <Text type="secondary">
            Управление организациями, ценообразованием терминалов и сертификатной политикой
          </Text>
        </div>
        <Space>
          <Input
            placeholder="Поиск по ID или названию..."
            prefix={<SearchOutlined />}
            value={searchText}
            onChange={(e) => setSearchText(e.target.value)}
            style={{ width: 240 }}
            allowClear
          />
          <Button icon={<ReloadOutlined />} onClick={loadData}>
            Обновить
          </Button>
          <Button type="primary" icon={<PlusOutlined />} onClick={handleOpenCreate}>
            Создать организацию
          </Button>
        </Space>
      </div>

      <Table
        rowKey="org_id"
        loading={loading}
        columns={columns}
        dataSource={filteredOrgs}
        pagination={{ pageSize: 20, showSizeChanger: true }}
        size="middle"
      />

      {/* Modal: Create Organization */}
      <Modal
        title="Создание новой организации"
        open={createModalOpen}
        onCancel={() => setCreateModalOpen(false)}
        onOk={() => createForm.submit()}
        confirmLoading={createSubmitting}
        okText="Создать организацию"
        cancelText="Отмена"
        width={580}
        destroyOnClose
      >
        <Form
          form={createForm}
          layout="vertical"
          onFinish={handleCreateSubmit}
          style={{ marginTop: 16 }}
        >
          <div style={{ display: "grid", gridTemplateColumns: "1fr 2fr", gap: 12 }}>
            <Form.Item
              name="org_id"
              label="ID организации"
              tooltip="Оставьте пустым для автоматического назначения следующего номера"
            >
              <InputNumber
                placeholder="Авто"
                min={1}
                style={{ width: "100%" }}
              />
            </Form.Item>
            <Form.Item
              name="is_active"
              label="Статус активности"
              valuePropName="checked"
            >
              <Switch checkedChildren="Активна" unCheckedChildren="Отключена" />
            </Form.Item>
          </div>

          <Form.Item
            name="org_name"
            label="Название организации (юр. лицо)"
            rules={[{ required: true, message: "Введите название организации" }]}
          >
            <Input placeholder="ООО 'Пример' или ИП..." />
          </Form.Item>

          <Form.Item
            name="name"
            label="Краткое наименование (для отображения)"
            rules={[{ required: true, message: "Введите краткое наименование" }]}
          >
            <Input placeholder="Краткое имя..." />
          </Form.Item>

          <Divider style={{ margin: "16px 0 12px" }}>
            Параметры лицензирования и тарифов
          </Divider>

          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Form.Item
              name="monthly_price_rub"
              label="Стоимость лицензии / мес (₽)"
              rules={[{ required: true, message: "Укажите стоимость лицензии" }]}
            >
              <InputNumber
                min={0}
                step={100}
                style={{ width: "100%" }}
                placeholder="1000.00"
              />
            </Form.Item>
            <Form.Item name="currency" label="Валюта биллинга">
              <Select>
                <Select.Option value="RUB">RUB (₽)</Select.Option>
                <Select.Option value="USD">USD ($)</Select.Option>
                <Select.Option value="EUR">EUR (€)</Select.Option>
              </Select>
            </Form.Item>
          </div>

          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Form.Item
              name="cert_billing_mode"
              label="Тарификация сертификатов"
            >
              <Select onChange={(val) => setCreateCertMode(val)}>
                <Select.Option value="none">Бесплатно (none)</Select.Option>
                <Select.Option value="per_operation">За операцию (per_operation)</Select.Option>
              </Select>
            </Form.Item>

            {createCertMode === "per_operation" && (
              <Form.Item
                name="cert_price_rub"
                label="Цена выпуска сертификата (₽)"
                rules={[{ required: true, message: "Укажите цену сертификата" }]}
              >
                <InputNumber
                  min={0}
                  step={50}
                  style={{ width: "100%" }}
                  placeholder="500.00"
                />
              </Form.Item>
            )}
          </div>

          <Form.Item
            name="tenant_pin_creation_enabled"
            label="Самостоятельный выпуск PIN клиентом"
            valuePropName="checked"
          >
            <Switch checkedChildren="Разрешен" unCheckedChildren="Запрещен" />
          </Form.Item>

          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Form.Item
              name="cert_charge_primary_issue"
              label="Плата за первичный выпуск"
              valuePropName="checked"
            >
              <Switch checkedChildren="Да" unCheckedChildren="Нет" />
            </Form.Item>
            <Form.Item
              name="cert_charge_reissue"
              label="Плата за перевыпуск"
              valuePropName="checked"
            >
              <Switch checkedChildren="Да" unCheckedChildren="Нет" />
            </Form.Item>
          </div>
        </Form>
      </Modal>

      {/* Modal: Edit Organization & Licensing Policy */}
      <Modal
        title={`Настройка организации #${editingOrg?.org_id} — ${editingOrg?.org_name}`}
        open={editModalOpen}
        onCancel={() => setEditModalOpen(false)}
        onOk={() => editForm.submit()}
        confirmLoading={editSubmitting}
        okText="Сохранить изменения"
        cancelText="Отмена"
        width={580}
        destroyOnClose
      >
        <Form
          form={editForm}
          layout="vertical"
          onFinish={handleEditSubmit}
          style={{ marginTop: 16 }}
        >
          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Form.Item
              name="org_name"
              label="Название организации (юр. лицо)"
              rules={[{ required: true, message: "Введите название" }]}
            >
              <Input />
            </Form.Item>
            <Form.Item
              name="name"
              label="Краткое наименование"
              rules={[{ required: true, message: "Введите краткое наименование" }]}
            >
              <Input />
            </Form.Item>
          </div>

          <Form.Item
            name="is_active"
            label="Статус активности организации"
            valuePropName="checked"
          >
            <Switch checkedChildren="Активна" unCheckedChildren="Отключена" />
          </Form.Item>

          <Divider style={{ margin: "16px 0 12px" }}>
            Параметры лицензирования и тарифов
          </Divider>

          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Form.Item
              name="monthly_price_rub"
              label="Стоимость лицензии / мес (₽)"
              rules={[{ required: true, message: "Укажите стоимость лицензии" }]}
            >
              <InputNumber min={0} step={100} style={{ width: "100%" }} />
            </Form.Item>
            <Form.Item name="currency" label="Валюта биллинга">
              <Select>
                <Select.Option value="RUB">RUB (₽)</Select.Option>
                <Select.Option value="USD">USD ($)</Select.Option>
                <Select.Option value="EUR">EUR (€)</Select.Option>
              </Select>
            </Form.Item>
          </div>

          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Form.Item
              name="cert_billing_mode"
              label="Тарификация сертификатов"
            >
              <Select onChange={(val) => setEditCertMode(val)}>
                <Select.Option value="none">Бесплатно (none)</Select.Option>
                <Select.Option value="per_operation">За операцию (per_operation)</Select.Option>
              </Select>
            </Form.Item>

            {editCertMode === "per_operation" && (
              <Form.Item
                name="cert_price_rub"
                label="Цена выпуска сертификата (₽)"
                rules={[{ required: true, message: "Укажите цену сертификата" }]}
              >
                <InputNumber min={0} step={50} style={{ width: "100%" }} />
              </Form.Item>
            )}
          </div>

          <Form.Item
            name="tenant_pin_creation_enabled"
            label="Самостоятельный выпуск PIN клиентом"
            valuePropName="checked"
          >
            <Switch checkedChildren="Разрешен" unCheckedChildren="Запрещен" />
          </Form.Item>

          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Form.Item
              name="cert_charge_primary_issue"
              label="Плата за первичный выпуск"
              valuePropName="checked"
            >
              <Switch checkedChildren="Да" unCheckedChildren="Нет" />
            </Form.Item>
            <Form.Item
              name="cert_charge_reissue"
              label="Плата за перевыпуск"
              valuePropName="checked"
            >
              <Switch checkedChildren="Да" unCheckedChildren="Нет" />
            </Form.Item>
          </div>
        </Form>
      </Modal>
    </Card>
  );
}
