import { useCallback, useEffect, useState } from "react";
import {
  Alert,
  Button,
  Card,
  Empty,
  Space,
  Table,
  Tag,
  Typography,
  message,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import {
  CheckCircleOutlined,
  ClockCircleOutlined,
  CodeOutlined,
  CopyOutlined,
  DesktopOutlined,
  DisconnectOutlined,
  PlusOutlined,
  ReloadOutlined,
  SafetyCertificateOutlined,
  SyncOutlined,
  VideoCameraOutlined,
} from "@ant-design/icons";
import { useNavigate } from "react-router";
import {
  listTerminalsSettings,
  retryTerminalOnboarding,
  type TerminalSettingsItem,
} from "../../api/settings";
import { useSession } from "../../session/SessionContext";
import OnboardingWizardModal from "../../components/OnboardingWizardModal";

const { Text, Title, Paragraph } = Typography;

function readinessTag(item: TerminalSettingsItem) {
  const r = item.readiness;
  if (!r) {
    return <Tag>{item.is_active ? "Активен" : "Неактивен"}</Tag>;
  }
  if (r.online === "online") {
    return (
      <Tag icon={<CheckCircleOutlined />} color="success">
        Online
      </Tag>
    );
  }
  return (
    <Tag icon={<DisconnectOutlined />} color="default">
      Offline
    </Tag>
  );
}

function pinTag(item: TerminalSettingsItem) {
  if (item.pin_state === "issued") {
    return (
      <Tag icon={<SafetyCertificateOutlined />} color="processing">
        PIN выдан
      </Tag>
    );
  }
  if (item.pin_state === "consumed") {
    return (
      <Tag icon={<SafetyCertificateOutlined />} color="success">
        PIN активирован
      </Tag>
    );
  }
  return (
    <Tag icon={<ClockCircleOutlined />} color="default">
      PIN: {item.pin_state || "pending"}
    </Tag>
  );
}

export default function L4DeskTerminalsPage() {
  const { user } = useSession();
  const navigate = useNavigate();
  const [loading, setLoading] = useState(true);
  const [terminals, setTerminals] = useState<TerminalSettingsItem[]>([]);
  const [wizardOpen, setWizardOpen] = useState(false);
  const [retryingId, setRetryingId] = useState<number | null>(null);

  const fetchTerminals = useCallback(async () => {
    setLoading(true);
    try {
      const data = await listTerminalsSettings({
        org_id: user?.org_id || undefined,
        page: 1,
        page_size: 100,
      });
      setTerminals(data.items || []);
    } catch {
      message.error("Не удалось загрузить список терминалов");
    } finally {
      setLoading(false);
    }
  }, [user?.org_id]);

  useEffect(() => {
    void fetchTerminals();
  }, [fetchTerminals]);

  const handleRetry = async (record: TerminalSettingsItem) => {
    setRetryingId(record.id);
    try {
      await retryTerminalOnboarding(record.id);
      message.success(`Повторный provisioning для ${record.sn} запущен`);
      void fetchTerminals();
    } catch (err: any) {
      message.error(err.response?.data?.detail || "Ошибка повторного provisioning");
    } finally {
      setRetryingId(null);
    }
  };

  const canRetry = (item: TerminalSettingsItem) =>
    item.provisioning_state === "failed" ||
    item.provisioning_state === "pending" ||
    item.pin_state === "failed" ||
    item.pin_state === "pending";

  const columns: ColumnsType<TerminalSettingsItem> = [
    {
      title: "SN",
      dataIndex: "sn",
      key: "sn",
      render: (sn: string) => (
        <Space size={4}>
          <Text copyable={{ text: sn }} style={{ fontFamily: "monospace", fontSize: 12 }}>
            {sn}
          </Text>
        </Space>
      ),
    },
    {
      title: "device_id",
      dataIndex: "device_id",
      key: "device_id",
      width: 110,
      render: (v: number) => (
        <Text copyable={{ text: String(v) }} style={{ fontFamily: "monospace" }}>
          {v}
        </Text>
      ),
    },
    {
      title: "Название / адрес",
      key: "label",
      render: (_, r) => (
        <div>
          <div>{r.note || "—"}</div>
          {r.address && (
            <Text type="secondary" style={{ fontSize: 12 }}>
              {r.address}
            </Text>
          )}
        </div>
      ),
    },
    {
      title: "Статус",
      key: "status",
      width: 120,
      render: (_, r) => readinessTag(r),
    },
    {
      title: "Provisioning / PIN",
      key: "prov",
      width: 180,
      render: (_, r) => (
        <Space direction="vertical" size={4}>
          <Tag color={r.provisioning_state === "ready" ? "success" : "default"}>
            IoT: {r.provisioning_state || "pending"}
          </Tag>
          {pinTag(r)}
        </Space>
      ),
    },
    {
      title: "Действия",
      key: "actions",
      width: 280,
      render: (_, r) => (
        <Space wrap>
          <Button
            size="small"
            icon={<CodeOutlined />}
            onClick={() => navigate(`/console?sn=${encodeURIComponent(r.sn)}`)}
          >
            Консоль
          </Button>
          <Button
            size="small"
            icon={<VideoCameraOutlined />}
            onClick={() => navigate(`/video?sn=${encodeURIComponent(r.sn)}`)}
          >
            Видео
          </Button>
          {canRetry(r) && (
            <Button
              size="small"
              icon={<SyncOutlined spin={retryingId === r.id} />}
              onClick={() => handleRetry(r)}
              loading={retryingId === r.id}
            >
              Повторить
            </Button>
          )}
        </Space>
      ),
    },
  ];

  return (
    <div style={{ maxWidth: 1200, margin: "0 auto" }}>
      <Card
        size="small"
        style={{ marginBottom: 16, borderRadius: 8 }}
        styles={{ body: { padding: "12px 16px" } }}
      >
        <div
          style={{
            display: "flex",
            justifyContent: "space-between",
            alignItems: "center",
            flexWrap: "wrap",
            gap: 12,
          }}
        >
          <Space align="center">
            <DesktopOutlined style={{ fontSize: 20, color: "#1677ff" }} />
            <Title level={4} style={{ margin: 0, fontSize: 16 }}>
              Терминалы
            </Title>
          </Space>
          <Space>
            <Button icon={<ReloadOutlined spin={loading} />} onClick={fetchTerminals}>
              Обновить
            </Button>
            <Button
              type="primary"
              icon={<PlusOutlined />}
              onClick={() => setWizardOpen(true)}
            >
              Подключить терминал
            </Button>
          </Space>
        </div>
      </Card>

      <Alert
        type="info"
        showIcon
        style={{ marginBottom: 16 }}
        message="Серийный номер и device_id назначаются сервером"
        description="SN и device_id доступны только для чтения и копирования. Создание и подключение терминала выполняется здесь."
      />

      {!loading && terminals.length === 0 && (
        <Card style={{ textAlign: "center", padding: "48px 24px" }}>
          <Empty
            description={
              <div>
                <Title level={5}>Терминалов пока нет</Title>
                <Paragraph type="secondary" style={{ maxWidth: 460, margin: "0 auto 16px" }}>
                  Создайте первый терминал через мастер подключения.
                </Paragraph>
              </div>
            }
          >
            <Button
              type="primary"
              size="large"
              icon={<PlusOutlined />}
              onClick={() => setWizardOpen(true)}
            >
              Подключить терминал
            </Button>
          </Empty>
        </Card>
      )}

      {(terminals.length > 0 || loading) && (
        <Card>
          <Table
            rowKey="id"
            columns={columns}
            dataSource={terminals}
            loading={loading}
            pagination={false}
            locale={{ emptyText: "Нет терминалов" }}
          />
        </Card>
      )}

      <OnboardingWizardModal
        open={wizardOpen}
        onClose={() => setWizardOpen(false)}
        onTerminalCreated={() => {
          void fetchTerminals();
        }}
        orgId={user?.org_id ?? undefined}
      />
    </div>
  );
}
