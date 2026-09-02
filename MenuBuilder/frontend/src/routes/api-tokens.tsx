import { useState, useEffect } from "react";
import {
  Card,
  Button,
  Input,
  Select,
  Table,
  Space,
  Typography,
  message,
  Modal,
  Tag,
  Alert,
} from "antd";
import { CopyOutlined, DeleteOutlined, PlusOutlined, KeyOutlined } from "@ant-design/icons";
import {
  createToken,
  listTokens,
  revokeToken,
  getProfile,
  type TokenInfo,
} from "../api/profile";
import PageHeader from "../components/PageHeader";

const { Text, Paragraph } = Typography;

export default function ProfilePage() {
  const [profile, setProfile] = useState<{ username: string; org_id: number } | null>(null);
  const [tokens, setTokens] = useState<TokenInfo[]>([]);
  const [loading, setLoading] = useState(false);
  const [createModalOpen, setCreateModalOpen] = useState(false);
  const [newTokenName, setNewTokenName] = useState("MiMoCode");
  const [newTokenDays, setNewTokenDays] = useState(30);
  const [createdToken, setCreatedToken] = useState<string | null>(null);

  const load = async () => {
    setLoading(true);
    try {
      const [p, t] = await Promise.all([getProfile(), listTokens()]);
      setProfile(p);
      setTokens(t);
    } catch {
      message.error("Ошибка загрузки профиля");
    }
    setLoading(false);
  };

  useEffect(() => {
    load();
  }, []);

  const handleCreate = async () => {
    try {
      const result = await createToken(newTokenName, newTokenDays);
      setCreatedToken(result.token);
      setCreateModalOpen(false);
      message.success("Токен создан");
      load();
    } catch {
      message.error("Ошибка создания токена");
    }
  };

  const handleRevoke = async (jti: string) => {
    Modal.confirm({
      title: "Отозвать токен?",
      content: "Этот токен перестанет работать. Действие необратимо.",
      onOk: async () => {
        try {
          await revokeToken(jti);
          message.success("Токен отозван");
          load();
        } catch {
          message.error("Ошибка отзыва токена");
        }
      },
    });
  };

  const copyToken = (token: string) => {
    navigator.clipboard.writeText(token);
    message.success("Токен скопирован");
  };

  const isActive = (t: TokenInfo) => !t.revoked_at && new Date(t.expires_at) > new Date();

  const columns = [
    {
      title: "Название",
      dataIndex: "name",
      key: "name",
      render: (name: string) => name || "—",
    },
    {
      title: "Статус",
      key: "status",
      render: (_: unknown, record: TokenInfo) => {
        if (record.revoked_at) return <Tag color="red">Отозван</Tag>;
        if (new Date(record.expires_at) < new Date()) return <Tag color="orange">Истёк</Tag>;
        return <Tag color="green">Активен</Tag>;
      },
    },
    {
      title: "Создан",
      dataIndex: "created_at",
      key: "created_at",
      render: (v: string) => new Date(v).toLocaleDateString("ru-RU"),
    },
    {
      title: "Истекает",
      dataIndex: "expires_at",
      key: "expires_at",
      render: (v: string) => new Date(v).toLocaleDateString("ru-RU"),
    },
    {
      title: "Последний запрос",
      dataIndex: "last_used_at",
      key: "last_used_at",
      render: (v: string | null) => (v ? new Date(v).toLocaleString("ru-RU") : "—"),
    },
    {
      title: "",
      key: "actions",
      render: (_: unknown, record: TokenInfo) =>
        isActive(record) ? (
          <Button
            type="text"
            danger
            icon={<DeleteOutlined />}
            onClick={() => handleRevoke(record.jti)}
          >
            Отозвать
          </Button>
        ) : null,
    },
  ];

  return (
    <Space direction="vertical" size="large" style={{ width: "100%" }}>
      <PageHeader
        title="API-токены"
        subtitle={
          profile
            ? `${profile.username} · организация ${profile.org_id}`
            : undefined
        }
      />

      {createdToken && (
        <Alert
          type="success"
          showIcon
          message="Токен создан! Скопируйте его — он больше не будет показан."
          description={
            <Space direction="vertical" style={{ width: "100%", marginTop: 8 }}>
              <Paragraph
                copyable
                code
                style={{ wordBreak: "break-all", marginBottom: 0 }}
              >
                {createdToken}
              </Paragraph>
              <Button
                icon={<CopyOutlined />}
                onClick={() => copyToken(createdToken)}
              >
                Скопировать
              </Button>
              <Text type="secondary" style={{ fontSize: 12 }}>
                Добавьте в .mimocode/mimocode.json:
              </Text>
              <Paragraph code style={{ fontSize: 11, marginBottom: 0 }}>
                {`"mcp": {\n  "processing-pins": {\n    "type": "remote",\n    "url": "https://dev.leo4.ru:3000/api/mcp/proxy",\n    "headers": {\n      "Authorization": "Bearer ${createdToken.substring(0, 20)}..."\n    }\n  }\n}`}
              </Paragraph>
            </Space>
          }
          closable
          onClose={() => setCreatedToken(null)}
        />
      )}

      <Card
        title={
          <Space>
            <KeyOutlined />
            <span>API-токены</span>
          </Space>
        }
        extra={
          <Button
            type="primary"
            icon={<PlusOutlined />}
            onClick={() => setCreateModalOpen(true)}
          >
            Создать токен
          </Button>
        }
      >
        <Table
          className="compact-table"
          dataSource={tokens}
          columns={columns}
          rowKey="jti"
          loading={loading}
          size="small"
          scroll={{ x: 600 }}
          pagination={false}
        />
      </Card>

      <Modal
        title="Создать API-токен"
        open={createModalOpen}
        onOk={handleCreate}
        onCancel={() => {
          setCreateModalOpen(false);
          setCreatedToken(null);
        }}
        okText="Создать"
        cancelText="Отмена"
        style={{ maxWidth: "calc(100vw - 16px)" }}
      >
        <Space direction="vertical" style={{ width: "100%" }}>
          <div>
            <Text>Название:</Text>
            <Input
              value={newTokenName}
              onChange={(e) => setNewTokenName(e.target.value)}
              placeholder="MiMoCode"
              style={{ marginTop: 4 }}
            />
          </div>
          <div>
            <Text>Срок действия:</Text>
            <Select
              value={newTokenDays}
              onChange={setNewTokenDays}
              style={{ width: "100%", marginTop: 4 }}
              options={[
                { value: 7, label: "7 дней" },
                { value: 30, label: "30 дней" },
                { value: 90, label: "90 дней" },
                { value: 365, label: "1 год" },
              ]}
            />
          </div>
        </Space>
      </Modal>
    </Space>
  );
}
