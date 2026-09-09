import { useCallback, useEffect, useState } from "react";
import {
  Alert,
  Button,
  Card,
  Checkbox,
  Divider,
  Form,
  Input,
  Modal,
  Popconfirm,
  Space,
  Switch,
  Table,
  Tag,
  Tooltip,
  Typography,
  message,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import {
  EditOutlined,
  KeyOutlined,
  PlusOutlined,
  ReloadOutlined,
  UserOutlined,
} from "@ant-design/icons";
import {
  TenantUserItem,
  changeTenantUserPassword,
  createTenantUser,
  listTenantUsers,
  toggleTenantUserActive,
  updateTenantUser,
} from "../../api/settingsUsers";
import { useSession } from "../../session/SessionContext";
import {
  ALL_PERMISSIONS,
  PERMISSION_BILLING_VIEW,
  PERMISSION_LABELS,
  PERMISSION_MONITORING_VIEW,
  PERMISSION_REPORTS_BALANCE_TERMINAL_VIEW,
  PERMISSION_REPORTS_BALANCE_TSP_VIEW,
  PERMISSION_REPORTS_EXPORT,
  PERMISSION_REPORTS_INKASS_VIEW,
  PERMISSION_REPORTS_PAYMENTS_VIEW,
  PERMISSION_SETTINGS_TERMINALS_VIEW,
  PERMISSION_VIDEO_VIEW,
} from "../../utils/permissions";

const { Text, Title, Paragraph } = Typography;

export default function UserSettingsPage() {
  const { user } = useSession();
  const [users, setUsers] = useState<TenantUserItem[]>([]);
  const [loading, setLoading] = useState(false);

  // Modals state
  const [createModalVisible, setCreateModalVisible] = useState(false);
  const [editModalVisible, setEditModalVisible] = useState(false);
  const [passwordModalVisible, setPasswordModalVisible] = useState(false);

  const [selectedUser, setSelectedUser] = useState<TenantUserItem | null>(null);
  const [submitting, setSubmitting] = useState(false);

  const [createForm] = Form.useForm();
  const [editForm] = Form.useForm();
  const [passwordForm] = Form.useForm();

  const fetchUsers = useCallback(async () => {
    setLoading(true);
    try {
      const data = await listTenantUsers(user?.org_id || undefined);
      setUsers(data);
    } catch (err: any) {
      message.error(err.response?.data?.detail || "Ошибка загрузки списка пользователей");
    } finally {
      setLoading(false);
    }
  }, [user?.org_id]);

  useEffect(() => {
    fetchUsers();
  }, [fetchUsers]);

  // Handle open create
  const handleOpenCreate = () => {
    createForm.resetFields();
    createForm.setFieldsValue({
      permissions: [
        PERMISSION_MONITORING_VIEW,
        PERMISSION_REPORTS_PAYMENTS_VIEW,
        PERMISSION_REPORTS_INKASS_VIEW,
      ],
    });
    setCreateModalVisible(true);
  };

  // Submit create
  const handleCreateSubmit = async (values: any) => {
    setSubmitting(true);
    try {
      await createTenantUser({
        username: values.username,
        password: values.password,
        full_name: values.full_name,
        permissions: values.permissions || [],
      });
      message.success(`Пользователь-наблюдатель ${values.username} успешно создан`);
      setCreateModalVisible(false);
      fetchUsers();
    } catch (err: any) {
      message.error(err.response?.data?.detail || "Ошибка создания пользователя");
    } finally {
      setSubmitting(false);
    }
  };

  // Handle open edit
  const handleOpenEdit = (item: TenantUserItem) => {
    setSelectedUser(item);
    editForm.setFieldsValue({
      full_name: item.full_name || "",
      permissions: item.permissions || [],
      is_active: item.is_active,
    });
    setEditModalVisible(true);
  };

  // Submit edit
  const handleEditSubmit = async (values: any) => {
    if (!selectedUser) return;
    setSubmitting(true);
    try {
      await updateTenantUser(selectedUser.id, {
        full_name: values.full_name,
        permissions: values.permissions || [],
        is_active: values.is_active,
      });
      message.success("Данные пользователя обновлены");
      setEditModalVisible(false);
      setSelectedUser(null);
      fetchUsers();
    } catch (err: any) {
      message.error(err.response?.data?.detail || "Ошибка обновления пользователя");
    } finally {
      setSubmitting(false);
    }
  };

  // Handle open password
  const handleOpenPassword = (item: TenantUserItem) => {
    setSelectedUser(item);
    passwordForm.resetFields();
    setPasswordModalVisible(true);
  };

  // Submit password
  const handlePasswordSubmit = async (values: any) => {
    if (!selectedUser) return;
    setSubmitting(true);
    try {
      await changeTenantUserPassword(selectedUser.id, {
        password: values.password,
      });
      message.success(`Пароль пользователя ${selectedUser.username} успешно изменен`);
      setPasswordModalVisible(false);
      setSelectedUser(null);
    } catch (err: any) {
      message.error(err.response?.data?.detail || "Ошибка смены пароля");
    } finally {
      setSubmitting(false);
    }
  };

  // Handle toggle active
  const handleToggleActive = async (item: TenantUserItem) => {
    try {
      const res = await toggleTenantUserActive(item.id, !item.is_active);
      message.success(
        res.is_active
          ? `Пользователь ${item.username} активирован`
          : `Пользователь ${item.username} деактивирован`
      );
      fetchUsers();
    } catch (err: any) {
      message.error(err.response?.data?.detail || "Ошибка изменения статуса пользователя");
    }
  };

  const renderPermissionsCheckboxes = (formInstance: any) => {
    const handleSelectAll = () => {
      formInstance.setFieldsValue({ permissions: [...ALL_PERMISSIONS] });
    };
    const handleDeselectAll = () => {
      formInstance.setFieldsValue({ permissions: [] });
    };

    return (
      <div style={{ background: "#fafafa", padding: 12, borderRadius: 6, border: "1px solid #f0f0f0" }}>
        <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 8 }}>
          <Text strong style={{ fontSize: 13 }}>Права доступа к разделам системы:</Text>
          <Space size="small">
            <Button size="small" type="link" onClick={handleSelectAll}>Выбрать все</Button>
            <Button size="small" type="link" onClick={handleDeselectAll}>Снять все</Button>
          </Space>
        </div>

        <Form.Item name="permissions" noStyle>
          <Checkbox.Group style={{ width: "100%" }}>
            <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
              <div>
                <Checkbox value={PERMISSION_MONITORING_VIEW}>
                  <Text strong>{PERMISSION_LABELS[PERMISSION_MONITORING_VIEW]}</Text>
                  <span style={{ color: "#8c8c8c", fontSize: 12, marginLeft: 6 }}>
                    — просмотр состояния и связи терминалов
                  </span>
                </Checkbox>
              </div>

              <Divider style={{ margin: "4px 0" }} />
              <div>
                <Text type="secondary" style={{ fontSize: 12, display: "block", marginBottom: 4 }}>
                  ОТЧЁТЫ:
                </Text>
                <div style={{ paddingLeft: 12, display: "flex", flexDirection: "column", gap: 6 }}>
                  <Checkbox value={PERMISSION_REPORTS_INKASS_VIEW}>
                    {PERMISSION_LABELS[PERMISSION_REPORTS_INKASS_VIEW]}
                  </Checkbox>
                  <Checkbox value={PERMISSION_REPORTS_PAYMENTS_VIEW}>
                    {PERMISSION_LABELS[PERMISSION_REPORTS_PAYMENTS_VIEW]}
                  </Checkbox>
                  <Checkbox value={PERMISSION_REPORTS_BALANCE_TERMINAL_VIEW}>
                    {PERMISSION_LABELS[PERMISSION_REPORTS_BALANCE_TERMINAL_VIEW]}
                  </Checkbox>
                  <Checkbox value={PERMISSION_REPORTS_BALANCE_TSP_VIEW}>
                    {PERMISSION_LABELS[PERMISSION_REPORTS_BALANCE_TSP_VIEW]}
                  </Checkbox>
                  <Checkbox value={PERMISSION_REPORTS_EXPORT}>
                    <Text strong style={{ color: "#1677ff" }}>
                      {PERMISSION_LABELS[PERMISSION_REPORTS_EXPORT]}
                    </Text>
                    <span style={{ color: "#8c8c8c", fontSize: 12, marginLeft: 6 }}>
                      (сохранение в Excel/CSV для доступных отчетов)
                    </span>
                  </Checkbox>
                </div>
              </div>

              <Divider style={{ margin: "4px 0" }} />
              <div>
                <Checkbox value={PERMISSION_BILLING_VIEW}>
                  <Text strong>{PERMISSION_LABELS[PERMISSION_BILLING_VIEW]}</Text>
                  <span style={{ color: "#8c8c8c", fontSize: 12, marginLeft: 6 }}>
                    — просмотр сроков лицензий терминалов
                  </span>
                </Checkbox>
              </div>

              <Divider style={{ margin: "4px 0" }} />
              <div>
                <Checkbox value={PERMISSION_SETTINGS_TERMINALS_VIEW}>
                  <Text strong>{PERMISSION_LABELS[PERMISSION_SETTINGS_TERMINALS_VIEW]}</Text>
                  <span style={{ color: "#8c8c8c", fontSize: 12, marginLeft: 6 }}>
                    — просмотр адресов и параметров терминалов (только чтение)
                  </span>
                </Checkbox>
              </div>

              <Divider style={{ margin: "4px 0" }} />
              <div>
                <Tooltip title="Доступ к разделу Видеонаблюдение и просмотр запущенной трансляции. Не даёт управлять трансляцией и терминалом">
                  <Checkbox value={PERMISSION_VIDEO_VIEW}>
                    <Text strong>{PERMISSION_LABELS[PERMISSION_VIDEO_VIEW]}</Text>
                    <span style={{ color: "#8c8c8c", fontSize: 12, marginLeft: 6 }}>
                      — доступ к разделу Видеонаблюдение и просмотр запущенной трансляции
                    </span>
                  </Checkbox>
                </Tooltip>
              </div>
            </div>
          </Checkbox.Group>
        </Form.Item>
      </div>
    );
  };

  const columns: ColumnsType<TenantUserItem> = [
    {
      title: "Логин",
      dataIndex: "username",
      key: "username",
      width: 140,
      render: (uname: string) => (
        <Space>
          <UserOutlined style={{ color: "#1677ff" }} />
          <Text strong>{uname}</Text>
        </Space>
      ),
    },
    {
      title: "ФИО",
      dataIndex: "full_name",
      key: "full_name",
      width: 160,
      render: (v: string | null) => v || <Text type="secondary">—</Text>,
    },
    {
      title: "Роль",
      dataIndex: "role_id",
      key: "role_id",
      width: 150,
      render: (rid: number) => {
        if (rid === 3) {
          return <Tag color="blue">Главный пользователь</Tag>;
        }
        if (rid === 4) {
          return <Tag color="purple">Наблюдатель</Tag>;
        }
        if (rid === 1) {
          return <Tag color="gold">Суперадминистратор</Tag>;
        }
        return <Tag>{`Роль ${rid}`}</Tag>;
      },
    },
    {
      title: "Статус",
      dataIndex: "is_active",
      key: "is_active",
      width: 110,
      render: (active: boolean) =>
        active ? (
          <Tag color="success">Активен</Tag>
        ) : (
          <Tag color="error">Заблокирован</Tag>
        ),
    },
    {
      title: "Назначенные права",
      dataIndex: "permissions",
      key: "permissions",
      render: (perms: string[], r: TenantUserItem) => {
        if (r.role_id === 3 || r.role_id === 1) {
          return <Tag color="cyan">Полный доступ (все разделы)</Tag>;
        }
        if (!perms || perms.length === 0) {
          return <Text type="secondary">Нет назначенных прав (доступ закрыт)</Text>;
        }
        return (
          <Space wrap size={[4, 4]}>
            {perms.map((p) => (
              <Tag key={p} color="geekblue" style={{ fontSize: 11 }}>
                {PERMISSION_LABELS[p] || p}
              </Tag>
            ))}
          </Space>
        );
      },
    },
    {
      title: "Действия",
      key: "actions",
      width: 190,
      align: "center",
      render: (_: any, record: TenantUserItem) => {
        if (record.role_id !== 4) {
          return (
            <Text type="secondary" style={{ fontSize: 12 }}>
              Администратор
            </Text>
          );
        }

        return (
          <Space size="small">
            <Tooltip title="Редактировать права и данные">
              <Button
                size="small"
                icon={<EditOutlined />}
                onClick={() => handleOpenEdit(record)}
              />
            </Tooltip>
            <Tooltip title="Сменить пароль">
              <Button
                size="small"
                icon={<KeyOutlined />}
                onClick={() => handleOpenPassword(record)}
              />
            </Tooltip>
            <Popconfirm
              title={
                record.is_active
                  ? "Деактивировать пользователя?"
                  : "Активировать пользователя?"
              }
              description={
                record.is_active
                  ? "Пользователь потеряет доступ, а все его активные сессии будут немедленно завершены."
                  : "Пользователь снова сможет войти в систему."
              }
              onConfirm={() => handleToggleActive(record)}
              okText="Да"
              cancelText="Отмена"
              okButtonProps={{ danger: record.is_active }}
            >
              <Button
                size="small"
                danger={record.is_active}
                type={record.is_active ? "default" : "primary"}
              >
                {record.is_active ? "Блокировать" : "Включить"}
              </Button>
            </Popconfirm>
          </Space>
        );
      },
    },
  ];

  return (
    <Card bordered={false}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start", marginBottom: 16 }}>
        <div>
          <Title level={4} style={{ margin: 0 }}>
            Управление пользователями организации
          </Title>
          <Paragraph type="secondary" style={{ margin: "4px 0 0" }}>
            Вы можете создавать учетные записи с ролью «Наблюдатель» (роль 4) и назначать им
            выборочный доступ к просмотру разделов и отчетов.
          </Paragraph>
        </div>
        <Space>
          <Button icon={<ReloadOutlined />} onClick={fetchUsers} loading={loading}>
            Обновить
          </Button>
          <Button
            type="primary"
            icon={<PlusOutlined />}
            onClick={handleOpenCreate}
          >
            Добавить пользователя
          </Button>
        </Space>
      </div>

      <Alert
        message="Режим работы учетных записей наблюдателей"
        description="Пользователи-наблюдатели имеют строго ограниченный доступ только для чтения (Readonly) к выбранным разделам. Им запрещено вносить какие-либо изменения и настройки. Сессии отзываются автоматически при смене пароля или деактивации."
        type="info"
        showIcon
        style={{ marginBottom: 16 }}
      />

      <Table
        dataSource={users}
        columns={columns}
        rowKey="id"
        loading={loading}
        pagination={false}
      />

      {/* Modal: Create User */}
      <Modal
        title="Создание пользователя-наблюдателя"
        open={createModalVisible}
        onCancel={() => setCreateModalVisible(false)}
        onOk={() => createForm.submit()}
        confirmLoading={submitting}
        okText="Создать"
        cancelText="Отмена"
        width={620}
        destroyOnClose
      >
        <Form
          form={createForm}
          layout="vertical"
          onFinish={handleCreateSubmit}
          initialValues={{ permissions: [] }}
        >
          <Form.Item
            name="username"
            label="Логин (имя пользователя)"
            rules={[
              { required: true, message: "Введите логин" },
              { min: 3, message: "Минимум 3 символа" },
            ]}
          >
            <Input placeholder="Например: manager_viewer" />
          </Form.Item>

          <Form.Item
            name="password"
            label="Пароль"
            rules={[
              { required: true, message: "Введите пароль" },
              { min: 6, message: "Минимум 6 символов" },
            ]}
          >
            <Input.Password placeholder="Введите надежный пароль" />
          </Form.Item>

          <Form.Item name="full_name" label="ФИО сотрудника">
            <Input placeholder="Иванов Иван Иванович (необязательно)" />
          </Form.Item>

          {renderPermissionsCheckboxes(createForm)}
        </Form>
      </Modal>

      {/* Modal: Edit User */}
      <Modal
        title={`Редактирование прав пользователя ${selectedUser?.username || ""}`}
        open={editModalVisible}
        onCancel={() => {
          setEditModalVisible(false);
          setSelectedUser(null);
        }}
        onOk={() => editForm.submit()}
        confirmLoading={submitting}
        okText="Сохранить"
        cancelText="Отмена"
        width={620}
        destroyOnClose
      >
        <Form form={editForm} layout="vertical" onFinish={handleEditSubmit}>
          <Form.Item name="full_name" label="ФИО сотрудника">
            <Input placeholder="Иванов Иван Иванович" />
          </Form.Item>

          <Form.Item name="is_active" label="Статус учетной записи" valuePropName="checked">
            <Switch
              checkedChildren="Активен"
              unCheckedChildren="Заблокирован"
            />
          </Form.Item>

          {renderPermissionsCheckboxes(editForm)}
        </Form>
      </Modal>

      {/* Modal: Change Password */}
      <Modal
        title={`Смена пароля: ${selectedUser?.username || ""}`}
        open={passwordModalVisible}
        onCancel={() => {
          setPasswordModalVisible(false);
          setSelectedUser(null);
        }}
        onOk={() => passwordForm.submit()}
        confirmLoading={submitting}
        okText="Сохранить пароль"
        cancelText="Отмена"
        destroyOnClose
      >
        <Form form={passwordForm} layout="vertical" onFinish={handlePasswordSubmit}>
          <Paragraph type="secondary">
            После сохранения пароля все активные сессии данного пользователя будут немедленно завершены.
          </Paragraph>
          <Form.Item
            name="password"
            label="Новый пароль"
            rules={[
              { required: true, message: "Введите новый пароль" },
              { min: 6, message: "Минимум 6 символов" },
            ]}
          >
            <Input.Password placeholder="Минимум 6 символов" />
          </Form.Item>
        </Form>
      </Modal>
    </Card>
  );
}
