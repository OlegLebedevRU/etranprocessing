import { useState, useEffect } from "react";
import {
  Card,
  Row,
  Col,
  Typography,
  Radio,
  Form,
  Input,
  Button,
  Tag,
  Alert,
  Space,
  Badge,
  Spin,
  message,
} from "antd";
import {
  ApiOutlined,
  CheckCircleOutlined,
  ThunderboltOutlined,
  SecurityScanOutlined,
  LineChartOutlined,
  RobotOutlined,
  SendOutlined,
  BranchesOutlined,
} from "@ant-design/icons";
import { useSession } from "../../session/SessionContext";
import {
  joinMcpWaitlist,
  getMcpWaitlistStatus,
  type McpWaitlistStatusResponse,
} from "../../api/mcpWaitlist";

const { Title, Paragraph, Text } = Typography;

const MCP_USE_CASES = [
  {
    key: "auto_triage",
    title: "Автоматическая диагностика и устранение сбоев",
    icon: <ThunderboltOutlined style={{ fontSize: 24, color: "#faad14" }} />,
    description:
      "Автономные ИИ-агенты выявляют причины зависания процессов, сбоев драйверов устройств и сетевых задержек, предлагая проверенные шаги восстановления.",
    badge: "Популярный",
  },
  {
    key: "log_telemetry_analysis",
    title: "Интеллектуальный анализ логов и телеметрии",
    icon: <LineChartOutlined style={{ fontSize: 24, color: "#1677ff" }} />,
    description:
      "Предиктивный мониторинг аномалий на основе системных метрик и логов с информированием до возникновения инцидента.",
    badge: "Аналитика",
  },
  {
    key: "fleet_nlp_control",
    title: "Управление парком через диалоговые модели (LLM)",
    icon: <RobotOutlined style={{ fontSize: 24, color: "#52c41a" }} />,
    description:
      "Выполнение типовых команд проверки статуса, перезагрузки служб и отчётов с помощью голосовых или текстовых запросов на естественном языке.",
    badge: "Инновация",
  },
  {
    key: "security_audit",
    title: "Групповой аудит безопасности и соответствия",
    icon: <SecurityScanOutlined style={{ fontSize: 24, color: "#722ed1" }} />,
    description:
      "Регулярная проверка версий ПО, целостности сертификатов и контроль периметра устройств с автоматическим формированием отчётов.",
    badge: "Безопасность",
  },
];

export default function McpPromoPage() {
  const { user } = useSession();
  const [selectedUseCase, setSelectedUseCase] = useState<string>("auto_triage");
  const [loading, setLoading] = useState(false);
  const [statusLoading, setStatusLoading] = useState(true);
  const [waitlistStatus, setWaitlistStatus] = useState<McpWaitlistStatusResponse | null>(null);
  const [form] = Form.useForm();

  const orgName = user?.org_name || `Организация #${user?.org_id || ""}`;
  const userEmail = (user as any)?.email || user?.username || "";

  useEffect(() => {
    async function fetchStatus() {
      setStatusLoading(true);
      try {
        const res = await getMcpWaitlistStatus();
        setWaitlistStatus(res);
        if (res.registered && res.use_case) {
          setSelectedUseCase(res.use_case);
        }
      } catch {
        // quiet fallback
      } finally {
        setStatusLoading(false);
      }
    }
    void fetchStatus();
  }, []);

  const handleSubmit = async (values: any) => {
    setLoading(true);
    try {
      const resp = await joinMcpWaitlist({
        use_case: selectedUseCase,
        contact_email: values.contact_email?.trim() || undefined,
        note: values.note?.trim() || undefined,
      });
      message.success("Ваша организация успешно добавлена в лист ожидания MCP!");
      setWaitlistStatus({
        registered: true,
        tenant_id: resp.tenant_id,
        use_case: resp.use_case,
        registered_at: resp.registered_at,
        note: values.note?.trim() || undefined,
      });
    } catch (err: any) {
      message.error(err.response?.data?.detail || "Ошибка при сохранении заявки");
    } finally {
      setLoading(false);
    }
  };

  return (
    <div style={{ maxWidth: 1200, margin: "0 auto", paddingBottom: 40 }}>
      {/* Hero Header */}
      <Card
        style={{
          background: "linear-gradient(135deg, #f6f9fc 0%, #eef5ff 100%)",
          borderColor: "#d6e4ff",
          marginBottom: 24,
          borderRadius: 12,
        }}
      >
        <div style={{ padding: "16px 8px" }}>
          <Space align="center" style={{ marginBottom: 8 }}>
            <ApiOutlined style={{ fontSize: 28, color: "#1677ff" }} />
            <Tag color="processing" style={{ fontSize: 13, padding: "2px 8px" }}>
              L4Desk Model Context Protocol
            </Tag>
          </Space>
          <Title level={2} style={{ marginTop: 8, marginBottom: 8, fontSize: 26 }}>
            Искусственный интеллект для управления парком компьютеров
          </Title>
          <Paragraph
            type="secondary"
            style={{ fontSize: 15, maxWidth: 840, margin: 0, lineHeight: 1.6 }}
          >
            L4Desk MCP открывает стандартизированный протокол безопасного подключения языковых моделей
            (LLM) и ИИ-ассистентов к вашим терминалам. Без раскрытия приватных ключей и с полным
            сохранением журналов действий в финансово-техническом subledger.
          </Paragraph>
        </div>
      </Card>

      {/* Already registered notice */}
      {statusLoading ? (
        <div style={{ textAlign: "center", padding: "20px 0" }}>
          <Spin />
        </div>
      ) : (
        waitlistStatus?.registered && (
          <Alert
            type="success"
            showIcon
            icon={<CheckCircleOutlined style={{ fontSize: 20 }} />}
            style={{ marginBottom: 24, borderRadius: 8 }}
            message={
              <Text strong style={{ fontSize: 15 }}>
                Вы уже в листе ожидания раннего доступа MCP
              </Text>
            }
            description={
              <div style={{ marginTop: 4 }}>
                <Paragraph style={{ margin: "0 0 6px 0", fontSize: 13 }}>
                  Организация <strong>{orgName}</strong> зарегистрирована на приоритетный запуск.
                  {waitlistStatus.use_case && (
                    <span>
                      {" "}
                      Выбранный сценарий:{" "}
                      <strong>
                        {MCP_USE_CASES.find((u) => u.key === waitlistStatus.use_case)?.title ||
                          waitlistStatus.use_case}
                      </strong>
                      .
                    </span>
                  )}
                  {waitlistStatus.registered_at && (
                    <span>
                      {" "}
                      Дата регистрации: {new Date(waitlistStatus.registered_at).toLocaleDateString("ru-RU")}.
                    </span>
                  )}
                </Paragraph>
                <Text type="secondary" style={{ fontSize: 12 }}>
                  Инженеры L4Desk свяжутся с вами при открытии бета-доступа для вашей конфигурации.
                </Text>
              </div>
            }
          />
        )
      )}

      {/* Use cases selection */}
      <Title level={4} style={{ marginBottom: 16 }}>
        Выберите приоритетный сценарий использования для вашей организации
      </Title>

      <Row gutter={[16, 16]} style={{ marginBottom: 24 }}>
        {MCP_USE_CASES.map((uc) => {
          const isSelected = selectedUseCase === uc.key;
          return (
            <Col xs={24} sm={12} key={uc.key}>
              <Card
                hoverable
                onClick={() => setSelectedUseCase(uc.key)}
                style={{
                  height: "100%",
                  borderRadius: 8,
                  borderColor: isSelected ? "#1677ff" : "#f0f0f0",
                  backgroundColor: isSelected ? "#f0f7ff" : "#fff",
                  boxShadow: isSelected ? "0 2px 8px rgba(22,119,255,0.15)" : undefined,
                  transition: "all 0.2s ease",
                  cursor: "pointer",
                }}
              >
                <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start" }}>
                  <Space align="center">
                    {uc.icon}
                    <Text strong style={{ fontSize: 15 }}>
                      {uc.title}
                    </Text>
                  </Space>
                  <Tag color={isSelected ? "blue" : "default"}>{uc.badge}</Tag>
                </div>
                <Paragraph
                  type="secondary"
                  style={{ fontSize: 13, marginTop: 12, marginBottom: 0, lineHeight: 1.5 }}
                >
                  {uc.description}
                </Paragraph>
              </Card>
            </Col>
          );
        })}
      </Row>

      {/* Waitlist form */}
      <Card style={{ borderRadius: 8 }}>
        <Title level={4} style={{ margin: "0 0 16px 0", fontSize: 16 }}>
          {waitlistStatus?.registered ? "Обновить данные заявки" : "Заявка на подключение MCP"}
        </Title>

        <Form form={form} layout="vertical" onFinish={handleSubmit}>
          <Row gutter={16}>
            <Col xs={24} sm={12}>
              <Form.Item label="Организация / Тенант">
                <Input value={orgName} disabled />
              </Form.Item>
            </Col>

            <Col xs={24} sm={12}>
              <Form.Item
                name="contact_email"
                label="Контактный email для обратной связи"
                initialValue={userEmail}
                rules={[{ type: "email", message: "Введите корректный email" }]}
              >
                <Input placeholder="email@company.ru" />
              </Form.Item>
            </Col>
          </Row>

          <Form.Item
            name="note"
            label="Особенности оборудования и пожелания (опционально)"
            initialValue={waitlistStatus?.note}
          >
            <Input.TextArea
              rows={3}
              placeholder="Количество терминалов, типы используемых ОС, специфические внешние устройства..."
              maxLength={500}
              showCount
            />
          </Form.Item>

          <div style={{ textAlign: "right" }}>
            <Button
              type="primary"
              htmlType="submit"
              size="large"
              loading={loading}
              icon={<SendOutlined />}
            >
              {waitlistStatus?.registered ? "Сохранить изменения" : "Записаться в лист ожидания"}
            </Button>
          </div>
        </Form>
      </Card>
    </div>
  );
}
