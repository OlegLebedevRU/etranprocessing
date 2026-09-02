import { useEffect, useState } from "react";
import {
  Button,
  Card,
  Checkbox,
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
  Grid,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import {
  EditOutlined,
  MailOutlined,
  PhoneOutlined,
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
import {
  TIMEZONE_OPTIONS,
  DEFAULT_TIMEZONE,
  getTimezoneLabel,
} from "../utils/timezone";

const { Title, Text } = Typography;
const { useBreakpoint } = Grid;

export default function AdminOrganizationsPage() {
  const screens = useBreakpoint();
  const isMobile = !screens.md;

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
      timezone: DEFAULT_TIMEZONE,
      email: "",
      phone: "",
      notify_by_email: true,
      is_active: true,
      monthly_price_rub: 1000,
      currency: "RUB",
      billing_mode: "standard",
      min_billing_periods: 1,
      allowed_billing_periods: "",
      default_selection_mode: "all_due",
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
        timezone: values.timezone || DEFAULT_TIMEZONE,
        email: values.email ? values.email.trim() : null,
        phone: values.phone ? values.phone.trim() : null,
        notify_by_email: values.notify_by_email ?? true,
        is_active: values.is_active,
        monthly_price_minor: Math.round(Number(values.monthly_price_rub || 0) * 100),
        currency: values.currency || "RUB",
        billing_mode: values.billing_mode || "standard",
        min_billing_periods: Number(values.min_billing_periods || 1),
        allowed_billing_periods: values.allowed_billing_periods ? values.allowed_billing_periods.trim() : null,
        default_selection_mode: values.default_selection_mode || "all_due",
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
    setEditCertMode(org.cert_billing_mode || "none");
    editForm.resetFields();
    editForm.setFieldsValue({
      org_name: org.org_name,
      name: org.name,
      timezone: org.timezone || DEFAULT_TIMEZONE,
      email: org.email || "",
      phone: org.phone || "",
      notify_by_email: org.notify_by_email ?? true,
      is_active: org.is_active,
      monthly_price_rub: (org.monthly_price_minor / 100).toFixed(2),
      currency: org.currency,
      billing_mode: org.billing_mode || "standard",
      min_billing_periods: org.min_billing_periods || 1,
      allowed_billing_periods: org.allowed_billing_periods || "",
      default_selection_mode: org.default_selection_mode || "all_due",
      cert_billing_mode: org.cert_billing_mode || "none",
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
        timezone: values.timezone || DEFAULT_TIMEZONE,
        email: values.email ? values.email.trim() : null,
        phone: values.phone ? values.phone.trim() : null,
        notify_by_email: values.notify_by_email ?? true,
        is_active: values.is_active,
        monthly_price_minor: Math.round(Number(values.monthly_price_rub || 0) * 100),
        currency: values.currency,
        billing_mode: values.billing_mode || "standard",
        min_billing_periods: Number(values.min_billing_periods || 1),
        allowed_billing_periods: values.allowed_billing_periods ? values.allowed_billing_periods.trim() : null,
        default_selection_mode: values.default_selection_mode || "all_due",
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
      o.name.toLowerCase().includes(lower) ||
      (o.email && o.email.toLowerCase().includes(lower)) ||
      (o.phone && o.phone.toLowerCase().includes(lower))
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
      title: "Организация и контакты",
      dataIndex: "org_name",
      key: "org_name",
      render: (text, record) => (
        <div>
          <div><Text strong>{text}</Text></div>
          <Text type="secondary" style={{ fontSize: 12 }}>{record.name}</Text>
          {(record.email || record.phone) && (
            <div style={{ marginTop: 4, fontSize: 12, color: "#555" }}>
              {record.email && (
                <span style={{ marginRight: 8 }}>
                  <MailOutlined style={{ marginRight: 3, color: "#1677ff" }} />
                  {record.email}
                </span>
              )}
              {record.phone && (
                <span>
                  <PhoneOutlined style={{ marginRight: 3, color: "#52c41a" }} />
                  {record.phone}
                </span>
              )}
            </div>
          )}
        </div>
      ),
    },
    {
      title: "Статус",
      dataIndex: "is_active",
      key: "is_active",
      width: 100,
      render: (active) => (
        <Tag color={active ? "success" : "default"}>
          {active ? "Активна" : "Отключена"}
        </Tag>
      ),
    },
    {
      title: "Часовой пояс",
      dataIndex: "timezone",
      key: "timezone",
      width: 170,
      render: (tz) => (
        <Tag color="geekblue">
          {getTimezoneLabel(tz || "Europe/Moscow")}
        </Tag>
      ),
    },
    {
      title: "Модель биллинга",
      key: "billing_mode",
      width: 170,
      render: (_, record) => {
        const mode = record.billing_mode || "standard";
        let tagColor = "default";
        let label = "Стандартная";
        if (mode === "post_factum") {
          tagColor = "orange";
          label = "Пост-оплата (post_factum)";
        } else if (mode === "cert_linked") {
          tagColor = "geekblue";
          label = "По сертификату (cert_linked)";
        }
        return (
          <div>
            <Tag color={tagColor}>{label}</Tag>
            {record.allowed_billing_periods && (
              <div style={{ fontSize: 11, color: "#777", marginTop: 2 }}>
                Сетка: {record.allowed_billing_periods} мес.
              </div>
            )}
            {(record.min_billing_periods || 1) > 1 && (
              <div style={{ fontSize: 11, color: "#777" }}>
                Мин. период: {record.min_billing_periods}
              </div>
            )}
          </div>
        );
      },
    },
    {
      title: "Стоимость / мес.",
      dataIndex: "monthly_price_minor",
      key: "monthly_price_minor",
      width: 130,
      render: (val, record) => {
        if (record.billing_mode === "cert_linked") {
          return <Text type="secondary">0 ₽ (в сертификате)</Text>;
        }
        return (
          <Text>
            {(val / 100).toLocaleString("ru-RU", { minimumFractionDigits: 2 })} {record.currency}
          </Text>
        );
      },
    },
    {
      title: "Тариф PIN / Cert",
      dataIndex: "cert_billing_mode",
      key: "cert_billing_mode",
      width: 150,
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
      width: 110,
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
      width: 110,
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
        <Space wrap style={{ width: isMobile ? "100%" : "auto" }}>
          <Input
            placeholder="Поиск ID, названию, email, тел..."
            prefix={<SearchOutlined />}
            value={searchText}
            onChange={(e) => setSearchText(e.target.value)}
            style={{ width: isMobile ? "100%" : 260 }}
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
        className="compact-table"
        rowKey="org_id"
        loading={loading}
        columns={columns}
        dataSource={filteredOrgs}
        pagination={{
          defaultPageSize: 50,
          pageSize: 50,
          showSizeChanger: true,
          pageSizeOptions: ["10", "20", "50", "100"],
          simple: isMobile,
          size: "small",
        }}
        size="small"
        scroll={{ x: 1100 }}
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
        width={620}
        style={{ maxWidth: "calc(100vw - 16px)" }}
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

          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Form.Item
              name="org_name"
              label="Название организации (юр. лицо)"
              rules={[{ required: true, message: "Введите название организации" }]}
            >
              <Input placeholder="ООО 'Компания' или ИП..." />
            </Form.Item>

            <Form.Item
              name="name"
              label="Краткое наименование"
              rules={[{ required: true, message: "Введите краткое наименование" }]}
            >
              <Input placeholder="Краткое имя..." />
            </Form.Item>
          </div>

          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Form.Item
              name="email"
              label="Контактный Email"
              rules={[{ type: "email", message: "Некорректный email" }]}
            >
              <Input prefix={<MailOutlined />} placeholder="billing@example.com" />
            </Form.Item>

            <Form.Item
              name="phone"
              label="Контактный телефон"
            >
              <Input prefix={<PhoneOutlined />} placeholder="+7 (999) 000-00-00" />
            </Form.Item>
          </div>

          <Form.Item
            name="timezone"
            label="Часовой пояс организации"
            tooltip="Используется для расчетов суточных отчетов, агрегации балансов и отображения времени"
            rules={[{ required: true, message: "Выберите часовой пояс" }]}
          >
            <Select
              showSearch
              placeholder="Выберите часовой пояс..."
              filterOption={(input, option) =>
                (option?.label ?? "").toLowerCase().includes(input.toLowerCase()) ||
                (option?.value ?? "").toLowerCase().includes(input.toLowerCase())
              }
              options={TIMEZONE_OPTIONS.map((tz) => ({
                value: tz.value,
                label: `${tz.city} (${tz.offset}) — ${tz.regions}`,
              }))}
            />
          </Form.Item>

          <Form.Item
            name="notify_by_email"
            valuePropName="checked"
          >
            <Checkbox>Отправлять почтовые уведомления (счета, акты, напоминания)</Checkbox>
          </Form.Item>

          <Divider style={{ margin: "16px 0 12px", fontSize: 14 }}>
            Лицензирование и биллинг
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
              name="billing_mode"
              label="Модель биллинга"
              tooltip="Режим расчета и выставления счетов"
            >
              <Select>
                <Select.Option value="standard">Стандартная (предоплата)</Select.Option>
                <Select.Option value="post_factum">По факту задолженности (пост-оплата)</Select.Option>
                <Select.Option value="cert_linked">Привязана к сертификату (cert_linked)</Select.Option>
              </Select>
            </Form.Item>

            <Form.Item
              name="default_selection_mode"
              label="Выделение по умолчанию"
              tooltip="Какие терминалы по умолчанию отмечены галочками на странице /billing"
            >
              <Select>
                <Select.Option value="all_due">Задолженность и до 30 дней (all_due)</Select.Option>
                <Select.Option value="only_lapsed">Только с задолженностью (only_lapsed)</Select.Option>
                <Select.Option value="all">Все терминалы (all)</Select.Option>
              </Select>
            </Form.Item>
          </div>

          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Form.Item
              name="allowed_billing_periods"
              label="Разрешенные периоды продления"
              tooltip="Сетка продления в месяцах через запятую (например, 1 или 3,6,12). Оставьте пустым для любых."
            >
              <Select
                allowClear
                placeholder="Любые (без ограничений)"
                options={[
                  { label: "Любые периоды (без ограничений)", value: "" },
                  { label: "1 мес (строго ежемесячно)", value: "1" },
                  { label: "3, 6, 12 мес (шаги 3/6/12)", value: "3,6,12" },
                  { label: "1, 3, 6, 12 мес", value: "1,3,6,12" },
                  { label: "1, 2 мес", value: "1,2" },
                  { label: "12 мес (только год)", value: "12" },
                ]}
              />
            </Form.Item>

            <Form.Item
              name="min_billing_periods"
              label="Минимальный период оплаты"
              tooltip="Минимальное количество периодов за единовременную оплату"
            >
              <InputNumber min={1} max={120} style={{ width: "100%" }} placeholder="1" />
            </Form.Item>
          </div>

          <Divider style={{ margin: "16px 0 12px", fontSize: 14 }}>
            Настройки mTLS-сертификатов
          </Divider>

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
        width={620}
        style={{ maxWidth: "calc(100vw - 16px)" }}
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

          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Form.Item
              name="email"
              label="Контактный Email"
              rules={[{ type: "email", message: "Некорректный email" }]}
            >
              <Input prefix={<MailOutlined />} placeholder="billing@example.com" />
            </Form.Item>

            <Form.Item
              name="phone"
              label="Контактный телефон"
            >
              <Input prefix={<PhoneOutlined />} placeholder="+7 (999) 000-00-00" />
            </Form.Item>
          </div>

          <Form.Item
            name="timezone"
            label="Часовой пояс организации"
            tooltip="Используется для расчетов суточных отчетов, агрегации балансов и отображения времени"
            rules={[{ required: true, message: "Выберите часовой пояс" }]}
          >
            <Select
              showSearch
              placeholder="Выберите часовой пояс..."
              filterOption={(input, option) =>
                (option?.label ?? "").toLowerCase().includes(input.toLowerCase()) ||
                (option?.value ?? "").toLowerCase().includes(input.toLowerCase())
              }
              options={TIMEZONE_OPTIONS.map((tz) => ({
                value: tz.value,
                label: `${tz.city} (${tz.offset}) — ${tz.regions}`,
              }))}
            />
          </Form.Item>

          <Form.Item
            name="notify_by_email"
            valuePropName="checked"
          >
            <Checkbox>Отправлять почтовые уведомления (счета, акты, напоминания)</Checkbox>
          </Form.Item>

          <Form.Item
            name="is_active"
            label="Статус активности организации"
            valuePropName="checked"
          >
            <Switch checkedChildren="Активна" unCheckedChildren="Отключена" />
          </Form.Item>

          <Divider style={{ margin: "16px 0 12px", fontSize: 14 }}>
            Лицензирование и биллинг
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
              name="billing_mode"
              label="Модель биллинга"
              tooltip="Режим расчета и выставления счетов"
            >
              <Select>
                <Select.Option value="standard">Стандартная (предоплата)</Select.Option>
                <Select.Option value="post_factum">По факту задолженности (пост-оплата)</Select.Option>
                <Select.Option value="cert_linked">Привязана к сертификату (cert_linked)</Select.Option>
              </Select>
            </Form.Item>

            <Form.Item
              name="default_selection_mode"
              label="Выделение по умолчанию"
              tooltip="Какие терминалы по умолчанию отмечены галочками на странице /billing"
            >
              <Select>
                <Select.Option value="all_due">Задолженность и до 30 дней (all_due)</Select.Option>
                <Select.Option value="only_lapsed">Только с задолженностью (only_lapsed)</Select.Option>
                <Select.Option value="all">Все терминалы (all)</Select.Option>
              </Select>
            </Form.Item>
          </div>

          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Form.Item
              name="allowed_billing_periods"
              label="Разрешенные периоды продления"
              tooltip="Сетка продления в месяцах через запятую (например, 1 или 3,6,12). Оставьте пустым для любых."
            >
              <Select
                allowClear
                placeholder="Любые (без ограничений)"
                options={[
                  { label: "Любые периоды (без ограничений)", value: "" },
                  { label: "1 мес (строго ежемесячно)", value: "1" },
                  { label: "3, 6, 12 мес (шаги 3/6/12)", value: "3,6,12" },
                  { label: "1, 3, 6, 12 мес", value: "1,3,6,12" },
                  { label: "1, 2 мес", value: "1,2" },
                  { label: "12 мес (только год)", value: "12" },
                ]}
              />
            </Form.Item>

            <Form.Item
              name="min_billing_periods"
              label="Минимальный период оплаты"
              tooltip="Минимальное количество периодов за единовременную оплату"
            >
              <InputNumber min={1} max={120} style={{ width: "100%" }} placeholder="1" />
            </Form.Item>
          </div>

          <Divider style={{ margin: "16px 0 12px", fontSize: 14 }}>
            Настройки mTLS-сертификатов
          </Divider>

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
