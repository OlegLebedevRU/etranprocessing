import React, { useEffect, useState } from "react";
import {
  Card,
  Button,
  Input,
  Space,
  Typography,
  message,
  Modal,
  Tag,
  Alert,
  Spin,
  Row,
  Col,
  Tabs,
  Switch,
  Tooltip,
  Form,
  Divider,
} from "antd";
import {
  ApiOutlined,
  CopyOutlined,
  EyeOutlined,
  EyeInvisibleOutlined,
  ReloadOutlined,
  DeleteOutlined,
  PlusOutlined,
  ExportOutlined,
  BookOutlined,
  FileTextOutlined,
  CloudServerOutlined,
  ThunderboltOutlined,
  NotificationOutlined,
  RobotOutlined,
  CheckCircleOutlined,
  SafetyCertificateOutlined,
  CodeOutlined,
  BankOutlined,
  SyncOutlined,
} from "@ant-design/icons";
import PageHeader from "../components/PageHeader";
import {
  getApiKey,
  revealApiKey,
  provisionApiKey,
  toggleApiKeyActive,
  deleteApiKey,
  type ApiKeyData,
} from "../api/integrations";
import { getMe, type UserInfo } from "../api/auth";

const { Text, Paragraph, Title } = Typography;

const DOC_LINKS = [
  {
    title: "Интерактивная документация Swagger UI",
    url: "https://dev.leo4.ru/docs",
    description:
      "Интерактивная спецификация OpenAPI (Swagger) для тестирования и просмотра всех доступных эндпоинтов, схем запросов и ответов.",
    tag: "OpenAPI / Swagger",
    tagColor: "blue",
    icon: <BookOutlined style={{ fontSize: 20, color: "#1677ff" }} />,
    isExternal: true,
  },
  {
    title: "Руководство по интеграции с сервером",
    url: "https://github.com/OlegLebedevRU/iot-rpc-rest-app/blob/master/docs/server-integration-guide.md",
    description:
      "Архитектура взаимодействия, формат передачи заголовка X-API-Key, базовые REST-методы для управления устройствами и обработка ошибок.",
    tag: "Архитектура & API",
    tagColor: "cyan",
    icon: <CloudServerOutlined style={{ fontSize: 20, color: "#13c2c2" }} />,
    isExternal: true,
  },
  {
    title: "Жизненный цикл и воркфлоу задач",
    url: "https://github.com/OlegLebedevRU/iot-rpc-rest-app/blob/master/docs/1-task-workflow-doc.md",
    description:
      "Создание RPC-задач, отправка команд на устройства, статусы выполнения (pending, delivered, executed, failed), очереди и таймауты.",
    tag: "RPC & Задачи",
    tagColor: "geekblue",
    icon: <SyncOutlined style={{ fontSize: 20, color: "#2f54eb" }} />,
    isExternal: true,
  },
  {
    title: "Формат событий и телеметрии",
    url: "https://github.com/OlegLebedevRU/iot-rpc-rest-app/blob/master/docs/2-events-api-format-description.md",
    description:
      "Спецификация структуры телеметрии, событий, датчиков и аварийных сигналов, поступающих от оборудования в реальном времени.",
    tag: "События & Телеметрия",
    tagColor: "gold",
    icon: <ThunderboltOutlined style={{ fontSize: 20, color: "#faad14" }} />,
    isExternal: true,
  },
  {
    title: "Вебхуки и обратные вызовы",
    url: "https://github.com/OlegLebedevRU/iot-rpc-rest-app/blob/master/docs/3-webhooks.md",
    description:
      "Настройка HTTP-вебхуков для мгновенного получения уведомлений о смене статусов задач, результатах команд и системных событиях.",
    tag: "Webhooks",
    tagColor: "purple",
    icon: <NotificationOutlined style={{ fontSize: 20, color: "#722ed1" }} />,
    isExternal: true,
  },
  {
    title: "Руководство по интеграции для AI-агентов",
    url: "https://github.com/OlegLebedevRU/iot-rpc-rest-app/blob/master/docs/ai-agent-integration-guide.md",
    description:
      "Специализированное руководство по вызову API iot-rpc автономными нейросетевыми агентами, регламенты безопасности и автодиагностики.",
    tag: "AI Agents",
    tagColor: "magenta",
    icon: <RobotOutlined style={{ fontSize: 20, color: "#eb2f96" }} />,
    isExternal: true,
  },
];

export default function ApiConnectionPage() {
  const [currentUser, setCurrentUser] = useState<UserInfo | null>(null);
  const [keyData, setKeyData] = useState<ApiKeyData | null>(null);
  const [loading, setLoading] = useState<boolean>(true);
  const [revealed, setRevealed] = useState<boolean>(false);
  const [unmaskedKey, setUnmaskedKey] = useState<string | null>(null);
  const [actionLoading, setActionLoading] = useState<boolean>(false);
  const [customModalOpen, setCustomModalOpen] = useState<boolean>(false);
  const [form] = Form.useForm();

  const loadData = async () => {
    setLoading(true);
    try {
      const [user, key] = await Promise.all([getMe(), getApiKey({ mask: true })]);
      setCurrentUser(user);
      setKeyData(key);
      setRevealed(false);
      setUnmaskedKey(null);
    } catch {
      message.error("Ошибка загрузки данных подключения к API");
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadData();
  }, []);

  const handleRevealToggle = async () => {
    if (revealed) {
      setRevealed(false);
      return;
    }

    if (unmaskedKey) {
      setRevealed(true);
      return;
    }

    setActionLoading(true);
    try {
      const res = await revealApiKey();
      if (res.api_key) {
        setUnmaskedKey(res.api_key);
        setRevealed(true);
      }
    } catch {
      message.error("Не удалось получить открытый API-ключ");
    } finally {
      setActionLoading(false);
    }
  };

  const handleCopyKey = async () => {
    try {
      let keyToCopy = unmaskedKey;
      if (!keyToCopy) {
        const res = await revealApiKey();
        keyToCopy = res.api_key;
        if (keyToCopy) {
          setUnmaskedKey(keyToCopy);
        }
      }

      if (keyToCopy) {
        await navigator.clipboard.writeText(keyToCopy);
        message.success("API-ключ скопирован в буфер обмена");
      }
    } catch {
      message.error("Ошибка копирования в буфер обмена");
    }
  };

  const handleGenerateKey = async (customValues?: {
    api_key?: string;
    name?: string;
    is_active?: boolean;
  }) => {
    setActionLoading(true);
    try {
      const defaultName = currentUser?.org_name
        ? `Ключ организации ${currentUser.org_name}`
        : "Основной API-ключ";

      const res = await provisionApiKey({
        api_key: customValues?.api_key || undefined,
        name: customValues?.name || defaultName,
        is_active: customValues?.is_active ?? true,
      });

      setKeyData(res);
      setUnmaskedKey(res.api_key);
      setRevealed(true);
      setCustomModalOpen(false);
      form.resetFields();
      message.success("API-ключ успешно создан и активирован");
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : "Ошибка провиженинга API-ключа";
      message.error(msg);
    } finally {
      setActionLoading(false);
    }
  };

  const handleRotateKey = () => {
    Modal.confirm({
      title: "Ротировать API-ключ?",
      icon: <ReloadOutlined style={{ color: "#faad14" }} />,
      content: (
        <div>
          <Paragraph>
            Прежний API-ключ будет <b>немедленно аннулирован</b> и перестанет действовать
            во всех внешних интеграциях и сервисах.
          </Paragraph>
          <Paragraph type="secondary">
            Будет сгенерирован новый криптостойкий ключ для вашей организации.
          </Paragraph>
        </div>
      ),
      okText: "Да, ротировать",
      okType: "primary",
      okButtonProps: { danger: true },
      cancelText: "Отмена",
      onOk: async () => {
        await handleGenerateKey({
          name: keyData?.name || `Ключ организации #${keyData?.org_id}`,
          is_active: true,
        });
      },
    });
  };

  const handleToggleActive = async (checked: boolean) => {
    setActionLoading(true);
    try {
      const res = await toggleApiKeyActive({ is_active: checked });
      setKeyData((prev) => (prev ? { ...prev, is_active: res.is_active } : res));
      message.success(
        checked ? "API-ключ успешно включён" : "API-ключ временно заблокирован"
      );
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : "Ошибка изменения статуса ключа";
      message.error(msg);
    } finally {
      setActionLoading(false);
    }
  };

  const handleDeleteKey = () => {
    Modal.confirm({
      title: "Отозвать и удалить API-ключ?",
      icon: <DeleteOutlined style={{ color: "#ff4d4f" }} />,
      content: (
        <div>
          <Paragraph>
            API-ключ будет <b>полностью удалён</b> из базы данных платформы. Все запросы
            с данным ключом будут отклоняться с ошибкой <code>401 Unauthorized</code>.
          </Paragraph>
        </div>
      ),
      okText: "Удалить ключ",
      okType: "primary",
      okButtonProps: { danger: true },
      cancelText: "Отмена",
      onOk: async () => {
        setActionLoading(true);
        try {
          await deleteApiKey();
          setKeyData({
            has_key: false,
            org_id: currentUser?.org_id ?? 0,
            api_key: null,
            name: null,
            is_active: false,
            created_at: null,
            updated_at: null,
          });
          setUnmaskedKey(null);
          setRevealed(false);
          message.success("API-ключ успешно отозван и удалён");
        } catch (err: unknown) {
          const msg = err instanceof Error ? err.message : "Ошибка удаления API-ключа";
          message.error(msg);
        } finally {
          setActionLoading(false);
        }
      },
    });
  };

  const copyTextToClipboard = (textToCopy: string, label: string) => {
    navigator.clipboard.writeText(textToCopy);
    message.success(`${label} скопирован в буфер обмена`);
  };

  const currentKeyString = revealed && unmaskedKey ? unmaskedKey : keyData?.api_key || "";
  const placeholderKey = unmaskedKey || "sec_live_ВАШ_API_КЛЮЧ";

  const curlExample = `# Проверка подключения и получение списка устройств организации
curl -X GET "https://dev.leo4.ru/api/v1/devices/" \\
  -H "X-API-Key: ${placeholderKey}" \\
  -H "Content-Type: application/json"`;

  const pythonExample = `import httpx

BASE_URL = "https://dev.leo4.ru/api/v1"
API_KEY = "${placeholderKey}"

headers = {
    "X-API-Key": API_KEY,
    "Content-Type": "application/json",
}

# Запрос списка зарегистрированных устройств
with httpx.Client(base_url=BASE_URL, headers=headers, timeout=10.0) as client:
    response = client.get("/devices/")
    response.raise_for_status()
    devices = response.json()
    print("Устройства организации:", devices)`;

  const jsExample = `// Пример на JavaScript / TypeScript (Node.js 18+ / Браузер)
const BASE_URL = "https://dev.leo4.ru/api/v1";
const API_KEY = "${placeholderKey}";

async function fetchDevices() {
  const response = await fetch(\`\${BASE_URL}/devices/\`, {
    headers: {
      "X-API-Key": API_KEY,
      "Content-Type": "application/json",
    },
  });

  if (!response.ok) {
    throw new Error(\`Ошибка HTTP: \${response.status}\`);
  }

  const data = await response.json();
  console.log("Устройства организации:", data);
}

fetchDevices().catch(console.error);`;

  if (loading) {
    return (
      <div style={{ display: "flex", justifyContent: "center", padding: 80 }}>
        <Spin size="large" />
      </div>
    );
  }

  const orgLabel = currentUser?.org_name
    ? `${currentUser.org_name} (ID: ${currentUser.org_id})`
    : currentUser?.org_id
    ? `Организация #${currentUser.org_id}`
    : "Текущая организация";

  return (
    <div style={{ maxWidth: 1200, margin: "0 auto" }}>
      <PageHeader
        title={
          <Space>
            <ApiOutlined />
            <span>Подключение АПИ</span>
          </Space>
        }
        subtitle="Интеграция с платформой Leo4 IoT RPC REST API и провиженинг ключей доступа"
        extra={
          <Space>
            <Tag color="blue" icon={<BankOutlined />}>
              Тенант: {orgLabel}
            </Tag>
            <Tag color="geekblue" icon={<SafetyCertificateOutlined />}>
              Шлюз: dev.leo4.ru
            </Tag>
          </Space>
        }
      />

      <Alert
        message="Аутентификация внешних систем по API-ключу"
        description={
          <div>
            API-ключ связывается с вашей организацией (1:1) и передается в HTTP-заголовке{" "}
            <code>X-API-Key</code> при обращении к сервисам платформы <b>Leo4 IoT</b>.
            Используйте его для интеграции бэкенд-систем, скриптов автоматизации и AI-агентов.
          </div>
        }
        type="info"
        showIcon
        style={{ marginBottom: 20 }}
      />

      <Row gutter={[20, 20]}>
        {/* API Key Management Section */}
        <Col span={24}>
          <Card
            title={
              <Space>
                <SafetyCertificateOutlined style={{ color: "#1677ff" }} />
                <span>API-ключ организации</span>
              </Space>
            }
            extra={
              keyData?.has_key && (
                <Space>
                  <Text type="secondary" style={{ fontSize: 13 }}>
                    Активность:
                  </Text>
                  <Switch
                    checked={keyData.is_active}
                    loading={actionLoading}
                    onChange={handleToggleActive}
                    checkedChildren="Вкл"
                    unCheckedChildren="Выкл"
                  />
                </Space>
              )
            }
          >
            {keyData?.has_key ? (
              <div>
                <Row gutter={[16, 16]} align="middle">
                  <Col xs={24} lg={16}>
                    <Space orientation="vertical" size={6} style={{ width: "100%" }}>
                      <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
                        <Text strong style={{ fontSize: 15 }}>
                          {keyData.name || "Основной ключ организации"}
                        </Text>
                        {keyData.is_active ? (
                          <Tag color="success" icon={<CheckCircleOutlined />}>
                            Активен
                          </Tag>
                        ) : (
                          <Tag color="error">Заблокирован</Tag>
                        )}
                      </div>

                      {/* Key Display Bar with Monospace font and Action Icons */}
                      <div
                        style={{
                          display: "flex",
                          alignItems: "center",
                          gap: 8,
                          background: "#fafafa",
                          border: "1px solid #d9d9d9",
                          borderRadius: 6,
                          padding: "6px 12px",
                          marginTop: 6,
                        }}
                      >
                        <Text
                          style={{
                            fontFamily: "Consolas, Monaco, monospace",
                            fontSize: 14,
                            letterSpacing: 0.5,
                            flex: 1,
                            wordBreak: "break-all",
                            color: keyData.is_active ? "#1f1f1f" : "#8c8c8c",
                          }}
                        >
                          {currentKeyString}
                        </Text>

                        {/* Action Icons (Пикты) */}
                        <Space size={4}>
                          <Tooltip title={revealed ? "Скрыть ключ" : "Показать ключ"}>
                            <Button
                              type="text"
                              size="small"
                              icon={revealed ? <EyeInvisibleOutlined /> : <EyeOutlined />}
                              onClick={handleRevealToggle}
                              loading={actionLoading}
                            />
                          </Tooltip>

                          <Tooltip title="Скопировать ключ">
                            <Button
                              type="text"
                              size="small"
                              icon={<CopyOutlined />}
                              onClick={handleCopyKey}
                              loading={actionLoading}
                            />
                          </Tooltip>
                        </Space>
                      </div>

                      <div style={{ marginTop: 6 }}>
                        <Space size={16}>
                          {keyData.created_at && (
                            <Text type="secondary" style={{ fontSize: 12 }}>
                              Создан:{" "}
                              {new Date(keyData.created_at).toLocaleString("ru-RU")}
                            </Text>
                          )}
                          {keyData.updated_at && (
                            <Text type="secondary" style={{ fontSize: 12 }}>
                              Обновлён:{" "}
                              {new Date(keyData.updated_at).toLocaleString("ru-RU")}
                            </Text>
                          )}
                        </Space>
                      </div>
                    </Space>
                  </Col>

                  <Col xs={24} lg={8} style={{ textAlign: "right" }}>
                    <Space wrap>
                      <Button
                        icon={<ReloadOutlined />}
                        onClick={handleRotateKey}
                        loading={actionLoading}
                      >
                        Ротировать ключ
                      </Button>

                      <Button
                        danger
                        icon={<DeleteOutlined />}
                        onClick={handleDeleteKey}
                        loading={actionLoading}
                      >
                        Отозвать
                      </Button>
                    </Space>
                  </Col>
                </Row>
              </div>
            ) : (
              <div
                style={{
                  textAlign: "center",
                  padding: "36px 16px",
                  background: "#fafafa",
                  borderRadius: 8,
                  border: "1px dashed #d9d9d9",
                }}
              >
                <ApiOutlined style={{ fontSize: 44, color: "#1677ff", marginBottom: 12 }} />
                <Title level={4} style={{ margin: "0 0 8px" }}>
                  API-ключ для вашей организации ещё не создан
                </Title>
                <Paragraph type="secondary" style={{ maxWidth: 520, margin: "0 auto 20px" }}>
                  Сгенерируйте API-ключ для подключения внешних систем, выполнения RPC-задач,
                  получения телеметрии и интеграции с IoT-платформой.
                </Paragraph>
                <Space>
                  <Button
                    type="primary"
                    icon={<PlusOutlined />}
                    size="large"
                    loading={actionLoading}
                    onClick={() => handleGenerateKey()}
                  >
                    Сгенерировать API-ключ
                  </Button>
                  <Button size="large" onClick={() => setCustomModalOpen(true)}>
                    Указать свой ключ
                  </Button>
                </Space>
              </div>
            )}
          </Card>
        </Col>

        {/* Quick Start & Code Examples */}
        <Col span={24}>
          <Card
            title={
              <Space>
                <CodeOutlined style={{ color: "#52c41a" }} />
                <span>Быстрый старт и примеры запросов</span>
              </Space>
            }
          >
            <Tabs
              defaultActiveKey="curl"
              items={[
                {
                  key: "curl",
                  label: "cURL (CLI)",
                  children: (
                    <div>
                      <div style={{ position: "relative" }}>
                        <pre
                          style={{
                            background: "#1e1e1e",
                            color: "#d4d4d4",
                            padding: 16,
                            borderRadius: 6,
                            overflowX: "auto",
                            fontSize: 13,
                            fontFamily: "Consolas, Monaco, monospace",
                          }}
                        >
                          {curlExample}
                        </pre>
                        <Button
                          size="small"
                          icon={<CopyOutlined />}
                          style={{ position: "absolute", top: 10, right: 10 }}
                          onClick={() => copyTextToClipboard(curlExample, "cURL пример")}
                        >
                          Копировать
                        </Button>
                      </div>
                    </div>
                  ),
                },
                {
                  key: "python",
                  label: "Python (httpx / requests)",
                  children: (
                    <div>
                      <div style={{ position: "relative" }}>
                        <pre
                          style={{
                            background: "#1e1e1e",
                            color: "#d4d4d4",
                            padding: 16,
                            borderRadius: 6,
                            overflowX: "auto",
                            fontSize: 13,
                            fontFamily: "Consolas, Monaco, monospace",
                          }}
                        >
                          {pythonExample}
                        </pre>
                        <Button
                          size="small"
                          icon={<CopyOutlined />}
                          style={{ position: "absolute", top: 10, right: 10 }}
                          onClick={() => copyTextToClipboard(pythonExample, "Python пример")}
                        >
                          Копировать
                        </Button>
                      </div>
                    </div>
                  ),
                },
                {
                  key: "js",
                  label: "JavaScript / TypeScript",
                  children: (
                    <div>
                      <div style={{ position: "relative" }}>
                        <pre
                          style={{
                            background: "#1e1e1e",
                            color: "#d4d4d4",
                            padding: 16,
                            borderRadius: 6,
                            overflowX: "auto",
                            fontSize: 13,
                            fontFamily: "Consolas, Monaco, monospace",
                          }}
                        >
                          {jsExample}
                        </pre>
                        <Button
                          size="small"
                          icon={<CopyOutlined />}
                          style={{ position: "absolute", top: 10, right: 10 }}
                          onClick={() => copyTextToClipboard(jsExample, "JavaScript пример")}
                        >
                          Копировать
                        </Button>
                      </div>
                    </div>
                  ),
                },
              ]}
            />
          </Card>
        </Col>

        {/* Documentation & Reference Links */}
        <Col span={24}>
          <Card
            title={
              <Space>
                <BookOutlined style={{ color: "#722ed1" }} />
                <span>Документация и справочные материалы</span>
              </Space>
            }
          >
            <Row gutter={[16, 16]}>
              {DOC_LINKS.map((doc, idx) => (
                <Col xs={24} md={12} key={idx}>
                  <Card
                    type="inner"
                    hoverable
                    style={{ height: "100%", display: "flex", flexDirection: "column" }}
                    bodyStyle={{
                      flex: 1,
                      display: "flex",
                      flexDirection: "column",
                      justifyContent: "space-between",
                    }}
                  >
                    <div>
                      <div
                        style={{
                          display: "flex",
                          alignItems: "flex-start",
                          justifyContent: "space-between",
                          marginBottom: 8,
                          gap: 8,
                        }}
                      >
                        <Space align="start">
                          {doc.icon}
                          <Text strong style={{ fontSize: 14 }}>
                            {doc.title}
                          </Text>
                        </Space>
                        <Tag color={doc.tagColor}>{doc.tag}</Tag>
                      </div>
                      <Paragraph
                        type="secondary"
                        style={{ fontSize: 13, minHeight: 40, marginBottom: 12 }}
                      >
                        {doc.description}
                      </Paragraph>
                    </div>

                    <div style={{ marginTop: 10, textAlign: "right" }}>
                      <Button
                        type="link"
                        icon={<ExportOutlined />}
                        href={doc.url}
                        target="_blank"
                        rel="noopener noreferrer"
                        style={{ padding: 0 }}
                      >
                        Перейти к документу
                      </Button>
                    </div>
                  </Card>
                </Col>
              ))}
            </Row>
          </Card>
        </Col>
      </Row>

      {/* Modal for Custom Key Provisioning */}
      <Modal
        title="Создание / Настройка API-ключа"
        open={customModalOpen}
        onCancel={() => setCustomModalOpen(false)}
        onOk={() => form.submit()}
        confirmLoading={actionLoading}
        okText="Создать ключ"
        cancelText="Отмена"
      >
        <Form
          form={form}
          layout="vertical"
          initialValues={{
            name: currentUser?.org_name
              ? `Ключ организации ${currentUser.org_name}`
              : "Основной API-ключ",
            is_active: true,
          }}
          onFinish={(values) => handleGenerateKey(values)}
        >
          <Form.Item
            name="name"
            label="Название / Описание ключа"
            rules={[{ required: true, message: "Введите наименование ключа" }]}
          >
            <Input placeholder="Например: Основной интеграционный ключ" />
          </Form.Item>

          <Form.Item
            name="api_key"
            label="Собственный API-ключ (опционально)"
            extra="Оставьте пустым для автоматической безопасной генерации"
          >
            <Input.Password placeholder="sec_live_..." />
          </Form.Item>

          <Form.Item name="is_active" label="Статус" valuePropName="checked">
            <Switch checkedChildren="Активен" unCheckedChildren="Заблокирован" defaultChecked />
          </Form.Item>
        </Form>
      </Modal>
    </div>
  );
}
