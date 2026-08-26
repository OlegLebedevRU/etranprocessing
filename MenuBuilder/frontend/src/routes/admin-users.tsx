import { useEffect, useState } from "react";
import {
  Badge,
  Button,
  Card,
  Drawer,
  Form,
  Input,
  Modal,
  Popconfirm,
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
  CheckCircleOutlined,
  CloseCircleOutlined,
  CrownOutlined,
  DeleteOutlined,
  EditOutlined,
  KeyOutlined,
  PlusOutlined,
  ReloadOutlined,
  SearchOutlined,
  StopOutlined,
  UserOutlined,
} from "@ant-design/icons";
import { getAdminOrganizations, type AdminOrg } from "../api/admin";
import {
  createUser,
  deleteUser,
  fetchUsers,
  fetchUserSessions,
  revokeAllUserSessions,
  revokeUserSession,
  updateUser,
  type CreateUserData,
  type UpdateUserData,
  type UserItem,
  type UserSessionItem,
} from "../api/adminUsers";

const { Title, Text } = Typography;

export default function AdminUsersPage() {
  const [loading, setLoading] = useState(false);
  const [users, setUsers] = useState<UserItem[]>([]);
  const [total, setTotal] = useState(0);
  const [orgs, setOrgs] = useState<AdminOrg[]>([]);

  // Filter states
  const [searchText, setSearchText] = useState("");
  const [selectedOrgId, setSelectedOrgId] = useState<number | undefined>(undefined);
  const [selectedRole, setSelectedRole] = useState<string | undefined>(undefined);
  const [selectedActive, setSelectedActive] = useState<boolean | undefined>(undefined);
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(50);

  // Create Modal
  const [createModalOpen, setCreateModalOpen] = useState(false);
  const [createForm] = Form.useForm();
  const [createSubmitting, setCreateSubmitting] = useState(false);

  // Edit Modal
  const [editModalOpen, setEditModalOpen] = useState(false);
  const [editingUser, setEditingUser] = useState<UserItem | null>(null);
  const [editForm] = Form.useForm();
  const [editSubmitting, setEditSubmitting] = useState(false);

  // Sessions Drawer
  const [sessionsDrawerOpen, setSessionsDrawerOpen] = useState(false);
  const [selectedUserForSessions, setSelectedUserForSessions] = useState<UserItem | null>(null);
  const [sessionsLoading, setSessionsLoading] = useState(false);
  const [sessions, setSessions] = useState<UserSessionItem[]>([]);

  const loadOrgs = async () => {
    try {
      const data = await getAdminOrganizations();
      setOrgs(data);
    } catch {
      // Non-blocking
    }
  };

  const loadData = async () => {
    setLoading(true);
    try {
      const resp = await fetchUsers({
        search: searchText || undefined,
        org_id: selectedOrgId,
        role: selectedRole,
        is_active: selectedActive,
        limit: pageSize,
        offset: (page - 1) * pageSize,
      });
      setUsers(resp.items);
      setTotal(resp.total);
    } catch (err: any) {
      message.error(err.message || "Ошибка загрузки пользователей");
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadOrgs();
  }, []);

  useEffect(() => {
    loadData();
  }, [page, pageSize, selectedOrgId, selectedRole, selectedActive]);

  const handleSearch = () => {
    setPage(1);
    loadData();
  };

  const handleOpenCreate = () => {
    createForm.resetFields();
    createForm.setFieldsValue({
      username: "",
      password: "",
      full_name: "",
      org_id: undefined,
      role: "user",
      is_superuser: false,
      is_active: true,
    });
    setCreateModalOpen(true);
  };

  const handleCreateSubmit = async (values: any) => {
    setCreateSubmitting(true);
    try {
      const isSu = Boolean(values.is_superuser);
      const role = values.role || (isSu ? "admin" : "user");
      const roleId = isSu ? 1 : role === "admin" ? 1 : 3;

      const payload: CreateUserData = {
        username: values.username.trim(),
        password: values.password,
        org_id: values.org_id ? Number(values.org_id) : null,
        role_id: roleId,
        role: role,
        full_name: values.full_name ? values.full_name.trim() : null,
        is_active: values.is_active ?? true,
        is_superuser: isSu,
      };

      await createUser(payload);
      message.success(`Пользователь ${payload.username} успешно создан`);
      setCreateModalOpen(false);
      loadData();
    } catch (err: any) {
      message.error(err.message || "Ошибка создания пользователя");
    } finally {
      setCreateSubmitting(false);
    }
  };

  const handleOpenEdit = (user: UserItem) => {
    setEditingUser(user);
    editForm.resetFields();
    editForm.setFieldsValue({
      username: user.username,
      password: "",
      full_name: user.full_name || "",
      org_id: user.org_id || undefined,
      role: user.role,
      is_superuser: user.is_superuser,
      is_active: user.is_active,
    });
    setEditModalOpen(true);
  };

  const handleEditSubmit = async (values: any) => {
    if (!editingUser) return;
    setEditSubmitting(true);
    try {
      const isSu = Boolean(values.is_superuser);
      const role = values.role || (isSu ? "admin" : "user");
      const roleId = isSu ? 1 : role === "admin" ? 1 : 3;

      const payload: UpdateUserData = {
        password: values.password ? values.password : null,
        org_id: values.org_id ? Number(values.org_id) : null,
        role_id: roleId,
        role: role,
        full_name: values.full_name ? values.full_name.trim() : null,
        is_active: values.is_active,
        is_superuser: isSu,
      };

      await updateUser(editingUser.id, payload);
      message.success(`Данные пользователя ${editingUser.username} обновлены`);
      setEditModalOpen(false);
      loadData();
    } catch (err: any) {
      message.error(err.message || "Ошибка обновления пользователя");
    } finally {
      setEditSubmitting(false);
    }
  };

  const handleDeleteUser = async (user: UserItem) => {
    try {
      await deleteUser(user.id);
      message.success(`Пользователь ${user.username} удален`);
      loadData();
    } catch (err: any) {
      message.error(err.message || "Ошибка удаления пользователя");
    }
  };

  const handleOpenSessions = async (user: UserItem) => {
    setSelectedUserForSessions(user);
    setSessionsDrawerOpen(true);
    setSessionsLoading(true);
    try {
      const data = await fetchUserSessions(user.id);
      setSessions(data);
    } catch (err: any) {
      message.error(err.message || "Ошибка загрузки сессий");
    } finally {
      setSessionsLoading(false);
    }
  };

  const handleRevokeSingleSession = async (sessionId: number) => {
    if (!selectedUserForSessions) return;
    try {
      await revokeUserSession(selectedUserForSessions.id, sessionId);
      message.success("Сессия успешно отозвана");
      const data = await fetchUserSessions(selectedUserForSessions.id);
      setSessions(data);
    } catch (err: any) {
      message.error(err.message || "Ошибка отзыва сессии");
    }
  };

  const handleRevokeAllSessions = async () => {
    if (!selectedUserForSessions) return;
    try {
      const res = await revokeAllUserSessions(selectedUserForSessions.id);
      message.success(`Отозвано активных сессий: ${res.revoked_count}`);
      const data = await fetchUserSessions(selectedUserForSessions.id);
      setSessions(data);
    } catch (err: any) {
      message.error(err.message || "Ошибка сброса сессий");
    }
  };

  const columns: ColumnsType<UserItem> = [
    {
      title: "ID",
      dataIndex: "id",
      key: "id",
      width: 70,
    },
    {
      title: "Логин",
      dataIndex: "username",
      key: "username",
      render: (text, record) => (
        <Space direction="vertical" size={2}>
          <Space>
            <Text strong>{text}</Text>
            {record.is_superuser && (
              <Tag color="magenta" icon={<CrownOutlined />}>
                Superuser
              </Tag>
            )}
          </Space>
          {record.full_name && (
            <Text type="secondary" style={{ fontSize: 12 }}>
              {record.full_name}
            </Text>
          )}
        </Space>
      ),
    },
    {
      title: "Организация",
      dataIndex: "org_name",
      key: "org_name",
      render: (orgName, record) => {
        if (!record.org_id) {
          return <Tag color="default">Все организации (0)</Tag>;
        }
        return (
          <Space direction="vertical" size={0}>
            <Text>{orgName || `Организация ${record.org_id}`}</Text>
            <Text type="secondary" style={{ fontSize: 11 }}>
              ID: {record.org_id}
            </Text>
          </Space>
        );
      },
    },
    {
      title: "Роль",
      dataIndex: "role",
      key: "role",
      width: 120,
      render: (role, record) => {
        if (record.is_superuser) return <Tag color="purple">Суперадмин (1)</Tag>;
        if (role === "admin" || record.role_id === 1) return <Tag color="red">Администратор (1)</Tag>;
        return <Tag color="blue">Пользователь ({record.role_id})</Tag>;
      },
    },
    {
      title: "Статус",
      dataIndex: "is_active",
      key: "is_active",
      width: 110,
      render: (active) => (
        active ? (
          <Badge status="success" text="Активен" />
        ) : (
          <Badge status="error" text="Отключен" />
        )
      ),
    },
    {
      title: "Действия",
      key: "actions",
      width: 280,
      render: (_, record) => (
        <Space size="small">
          <Button
            size="small"
            icon={<EditOutlined />}
            onClick={() => handleOpenEdit(record)}
          >
            Редактировать
          </Button>
          <Button
            size="small"
            icon={<KeyOutlined />}
            onClick={() => handleOpenSessions(record)}
          >
            Сессии
          </Button>
          <Popconfirm
            title="Удалить пользователя?"
            description={`Вы действительно хотите удалить пользователя ${record.username}?`}
            onConfirm={() => handleDeleteUser(record)}
            okText="Да"
            cancelText="Нет"
            okButtonProps={{ danger: true }}
          >
            <Button size="small" danger icon={<DeleteOutlined />} />
          </Popconfirm>
        </Space>
      ),
    },
  ];

  const sessionColumns: ColumnsType<UserSessionItem> = [
    {
      title: "ID",
      dataIndex: "id",
      key: "id",
      width: 60,
    },
    {
      title: "IP / Устройство",
      key: "client",
      render: (_, session) => (
        <Space direction="vertical" size={2}>
          <Text strong>{session.ip_address || "IP неизвестен"}</Text>
          {session.user_agent && (
            <Text type="secondary" style={{ fontSize: 11, maxWidth: 260 }} ellipsis={{ tooltip: session.user_agent }}>
              {session.user_agent}
            </Text>
          )}
        </Space>
      ),
    },
    {
      title: "Создана / Активность",
      key: "dates",
      render: (_, session) => (
        <Space direction="vertical" size={0}>
          <Text style={{ fontSize: 12 }}>Создан: {new Date(session.created_at).toLocaleString()}</Text>
          {session.last_used_at && (
            <Text type="secondary" style={{ fontSize: 11 }}>
              Вход: {new Date(session.last_used_at).toLocaleString()}
            </Text>
          )}
        </Space>
      ),
    },
    {
      title: "Статус",
      dataIndex: "is_revoked",
      key: "is_revoked",
      width: 100,
      render: (revoked, session) => {
        const isExpired = new Date(session.expires_at) < new Date();
        if (revoked) return <Tag color="error">Отозвана</Tag>;
        if (isExpired) return <Tag color="default">Истекла</Tag>;
        return <Tag color="success">Активна</Tag>;
      },
    },
    {
      title: "Действие",
      key: "action",
      width: 90,
      render: (_, session) => {
        const isExpired = new Date(session.expires_at) < new Date();
        if (session.is_revoked || isExpired) return null;
        return (
          <Button
            size="small"
            danger
            icon={<StopOutlined />}
            onClick={() => handleRevokeSingleSession(session.id)}
          >
            Отозвать
          </Button>
        );
      },
    },
  ];

  return (
    <div>
      <Card>
        <Space direction="vertical" size="middle" style={{ width: "100%" }}>
          <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", flexWrap: "wrap", gap: 12 }}>
            <div>
              <Title level={4} style={{ margin: 0 }}>
                Пользователи MenuBuilder
              </Title>
              <Text type="secondary">
                Управление учетными записями, привязками к организациям и активными JWT/Refresh сессиями
              </Text>
            </div>
            <Space>
              <Button icon={<ReloadOutlined />} onClick={loadData}>
                Обновить
              </Button>
              <Button type="primary" icon={<PlusOutlined />} onClick={handleOpenCreate}>
                Добавить пользователя
              </Button>
            </Space>
          </div>

          <Space wrap size="middle" style={{ width: "100%" }}>
            <Input
              placeholder="Поиск по логину или ФИО"
              prefix={<SearchOutlined />}
              value={searchText}
              onChange={(e) => setSearchText(e.target.value)}
              onPressEnter={handleSearch}
              style={{ width: 220 }}
              allowClear
            />
            <Select
              placeholder="Все организации"
              allowClear
              style={{ width: 220 }}
              value={selectedOrgId}
              onChange={(val) => {
                setSelectedOrgId(val);
                setPage(1);
              }}
              options={[
                { label: "Все организации", value: undefined },
                ...orgs.map((o) => ({
                  label: `${o.org_name || o.name} (ID: ${o.org_id})`,
                  value: o.org_id,
                })),
              ]}
            />
            <Select
              placeholder="Роль"
              allowClear
              style={{ width: 160 }}
              value={selectedRole}
              onChange={(val) => {
                setSelectedRole(val);
                setPage(1);
              }}
              options={[
                { label: "Все роли", value: undefined },
                { label: "Администраторы", value: "admin" },
                { label: "Пользователи", value: "user" },
              ]}
            />
            <Select
              placeholder="Статус"
              allowClear
              style={{ width: 140 }}
              value={selectedActive}
              onChange={(val) => {
                setSelectedActive(val);
                setPage(1);
              }}
              options={[
                { label: "Все статусы", value: undefined },
                { label: "Активные", value: true },
                { label: "Отключенные", value: false },
              ]}
            />
            <Button onClick={handleSearch} icon={<SearchOutlined />}>
              Найти
            </Button>
          </Space>

          <Table
            columns={columns}
            dataSource={users}
            rowKey="id"
            loading={loading}
            pagination={{
              current: page,
              pageSize: pageSize,
              total: total,
              showSizeChanger: true,
              pageSizeOptions: ["20", "50", "100"],
              onChange: (p, ps) => {
                setPage(p);
                setPageSize(ps);
              },
            }}
          />
        </Space>
      </Card>

      {/* Modal: Create User */}
      <Modal
        title="Создание нового пользователя"
        open={createModalOpen}
        onCancel={() => setCreateModalOpen(false)}
        onOk={() => createForm.submit()}
        confirmLoading={createSubmitting}
        destroyOnClose
      >
        <Form form={createForm} layout="vertical" onFinish={handleCreateSubmit}>
          <Form.Item
            name="username"
            label="Логин"
            rules={[{ required: true, message: "Введите логин пользователя" }]}
          >
            <Input prefix={<UserOutlined />} placeholder="например, admin424" />
          </Form.Item>

          <Form.Item
            name="password"
            label="Пароль"
            rules={[{ required: true, message: "Введите пароль" }]}
          >
            <Input.Password prefix={<KeyOutlined />} placeholder="Пароль учетной записи" />
          </Form.Item>

          <Form.Item name="full_name" label="ФИО / Описание">
            <Input placeholder="например, Иванов Иван Иванович" />
          </Form.Item>

          <Form.Item name="org_id" label="Организация">
            <Select
              placeholder="Выберите организацию"
              allowClear
              options={orgs.map((o) => ({
                label: `${o.org_name || o.name} (ID: ${o.org_id})`,
                value: o.org_id,
              }))}
            />
          </Form.Item>

          <Form.Item name="role" label="Роль">
            <Select
              options={[
                { label: "Пользователь (role_id: 3)", value: "user" },
                { label: "Администратор (role_id: 1)", value: "admin" },
              ]}
            />
          </Form.Item>

          <Form.Item name="is_superuser" label="Права Superuser" valuePropName="checked">
            <Switch checkedChildren="Да" unCheckedChildren="Нет" />
          </Form.Item>

          <Form.Item name="is_active" label="Аккаунт активен" valuePropName="checked">
            <Switch checkedChildren="Да" unCheckedChildren="Нет" defaultChecked />
          </Form.Item>
        </Form>
      </Modal>

      {/* Modal: Edit User */}
      <Modal
        title={`Редактирование пользователя ${editingUser?.username || ""}`}
        open={editModalOpen}
        onCancel={() => setEditModalOpen(false)}
        onOk={() => editForm.submit()}
        confirmLoading={editSubmitting}
        destroyOnClose
      >
        <Form form={editForm} layout="vertical" onFinish={handleEditSubmit}>
          <Form.Item name="username" label="Логин">
            <Input disabled prefix={<UserOutlined />} />
          </Form.Item>

          <Form.Item
            name="password"
            label="Новый пароль"
            help="Оставьте пустым, если не требуется менять текущий пароль"
          >
            <Input.Password prefix={<KeyOutlined />} placeholder="Новый пароль (опционально)" />
          </Form.Item>

          <Form.Item name="full_name" label="ФИО / Описание">
            <Input placeholder="например, Иванов Иван Иванович" />
          </Form.Item>

          <Form.Item name="org_id" label="Организация">
            <Select
              placeholder="Выберите организацию"
              allowClear
              options={orgs.map((o) => ({
                label: `${o.org_name || o.name} (ID: ${o.org_id})`,
                value: o.org_id,
              }))}
            />
          </Form.Item>

          <Form.Item name="role" label="Роль">
            <Select
              options={[
                { label: "Пользователь (role_id: 3)", value: "user" },
                { label: "Администратор (role_id: 1)", value: "admin" },
              ]}
            />
          </Form.Item>

          <Form.Item name="is_superuser" label="Права Superuser" valuePropName="checked">
            <Switch checkedChildren="Да" unCheckedChildren="Нет" />
          </Form.Item>

          <Form.Item name="is_active" label="Аккаунт активен" valuePropName="checked">
            <Switch checkedChildren="Да" unCheckedChildren="Нет" />
          </Form.Item>
        </Form>
      </Modal>

      {/* Drawer: User Sessions */}
      <Drawer
        title={
          <Space>
            <KeyOutlined />
            <span>Активные сессии пользователя {selectedUserForSessions?.username}</span>
          </Space>
        }
        width={680}
        open={sessionsDrawerOpen}
        onClose={() => setSessionsDrawerOpen(false)}
        extra={
          <Button
            danger
            icon={<StopOutlined />}
            onClick={handleRevokeAllSessions}
            disabled={sessions.filter((s) => !s.is_revoked).length === 0}
          >
            Завершить все сессии
          </Button>
        }
      >
        <Table
          columns={sessionColumns}
          dataSource={sessions}
          rowKey="id"
          loading={sessionsLoading}
          pagination={false}
          size="small"
        />
      </Drawer>
    </div>
  );
}
