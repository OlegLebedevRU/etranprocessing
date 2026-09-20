import { useState, useEffect, useRef } from "react";
import {
  Modal,
  Steps,
  Form,
  Input,
  Select,
  Button,
  Space,
  Typography,
  Card,
  Tag,
  Progress,
  message,
  Divider,
  Row,
  Col,
  Spin,
} from "antd";
import {
  DesktopOutlined,
  KeyOutlined,
  DownloadOutlined,
  CheckCircleOutlined,
  ClockCircleOutlined,
  CopyOutlined,
  VideoCameraOutlined,
  CodeOutlined,
  SyncOutlined,
  SafetyCertificateOutlined,
  ArrowRightOutlined,
} from "@ant-design/icons";
import { useNavigate } from "react-router";
import {
  onboardTerminal,
  getTerminalReadiness,
  type TerminalOnboardResponse,
  type TerminalReadiness,
} from "../api/settings";
import RefusalReasonCard from "./RefusalReasonCard";

const { Text, Title, Paragraph } = Typography;

const COMMON_TIMEZONES = [
  { value: "Europe/Kaliningrad", label: "Europe/Kaliningrad (UTC+2)" },
  { value: "Europe/Moscow", label: "Europe/Moscow (UTC+3, МСК)" },
  { value: "Europe/Samara", label: "Europe/Samara (UTC+4)" },
  { value: "Asia/Yekaterinburg", label: "Asia/Yekaterinburg (UTC+5)" },
  { value: "Asia/Omsk", label: "Asia/Omsk (UTC+6)" },
  { value: "Asia/Novosibirsk", label: "Asia/Novosibirsk (UTC+7)" },
  { value: "Asia/Krasnoyarsk", label: "Asia/Krasnoyarsk (UTC+7)" },
  { value: "Asia/Irkutsk", label: "Asia/Irkutsk (UTC+8)" },
  { value: "Asia/Yakutsk", label: "Asia/Yakutsk (UTC+9)" },
  { value: "Asia/Vladivostok", label: "Asia/Vladivostok (UTC+10)" },
  { value: "Asia/Magadan", label: "Asia/Magadan (UTC+11)" },
  { value: "Asia/Kamchatka", label: "Asia/Kamchatka (UTC+12)" },
];

export interface OnboardingWizardModalProps {
  open: boolean;
  onClose: () => void;
  onTerminalCreated?: (terminal: TerminalOnboardResponse) => void;
  orgId?: number;
}

export default function OnboardingWizardModal({
  open,
  onClose,
  onTerminalCreated,
  orgId,
}: OnboardingWizardModalProps) {
  const navigate = useNavigate();
  const [currentStep, setCurrentStep] = useState(0);
  const [loading, setLoading] = useState(false);
  const [createdTerminal, setCreatedTerminal] = useState<TerminalOnboardResponse | null>(null);
  const [readiness, setReadiness] = useState<TerminalReadiness | null>(null);
  const [pollingActive, setPollingActive] = useState(false);
  const [sessionError, setSessionError] = useState<{ code: string; message: string } | null>(null);

  const [form] = Form.useForm();
  const pollTimerRef = useRef<number | null>(null);

  // Reset state on open/close
  useEffect(() => {
    if (open) {
      setCurrentStep(0);
      setCreatedTerminal(null);
      setReadiness(null);
      setSessionError(null);
      form.resetFields();
    } else {
      if (pollTimerRef.current) {
        clearInterval(pollTimerRef.current);
        pollTimerRef.current = null;
      }
      setPollingActive(false);
    }
  }, [open, form]);

  // Clean up polling on unmount
  useEffect(() => {
    return () => {
      if (pollTimerRef.current) {
        clearInterval(pollTimerRef.current);
      }
    };
  }, []);

  const handleGenerateSn = () => {
    const randomHex = Math.random().toString(36).substring(2, 8).toUpperCase();
    form.setFieldsValue({ sn: `L4D-${randomHex}` });
  };

  const handleCreateTerminal = async (values: any) => {
    setLoading(true);
    try {
      const resp = await onboardTerminal(
        {
          sn: values.sn?.trim() || null,
          address: values.address?.trim() || null,
          note: values.note?.trim() || null,
          timezone: values.timezone || "Europe/Moscow",
        },
        orgId
      );
      setCreatedTerminal(resp);
      setReadiness(resp.readiness);
      onTerminalCreated?.(resp);
      message.success(`Терминал ${resp.sn} успешно зарегистрирован! Получен одноразовый PIN-код.`);
      setCurrentStep(1);
    } catch (err: any) {
      message.error(err.response?.data?.detail || "Ошибка при создании терминала");
    } finally {
      setLoading(false);
    }
  };

  const handleCopyPin = (pin: string) => {
    navigator.clipboard.writeText(pin);
    message.success("PIN-код скопирован в буфер обмена!");
  };

  // Start polling when entering Step 2
  const startPolling = (terminalId: number) => {
    setPollingActive(true);
    if (pollTimerRef.current) {
      clearInterval(pollTimerRef.current);
    }

    const poll = async () => {
      try {
        const resp = await getTerminalReadiness(terminalId);
        setReadiness(resp.readiness);
        if (resp.readiness.online === "online") {
          message.success("Терминал успешно вышел в онлайн!");
          if (pollTimerRef.current) {
            clearInterval(pollTimerRef.current);
            pollTimerRef.current = null;
          }
          setPollingActive(false);
        }
      } catch {
        // quiet retry
      }
    };

    void poll();
    pollTimerRef.current = window.setInterval(poll, 3000);
  };

  const handleProceedToReadiness = () => {
    setCurrentStep(2);
    if (createdTerminal?.terminal_id) {
      startPolling(createdTerminal.terminal_id);
    }
  };

  const handleLaunchSession = (sessionType: "video" | "console") => {
    if (!createdTerminal) return;
    if (readiness?.online !== "online") {
      setSessionError({
        code: "offline",
        message: "Терминал сейчас не на связи. Дождитесь выхода терминала в онлайн.",
      });
      return;
    }

    onClose();
    if (sessionType === "video") {
      navigate(`/video?sn=${createdTerminal.sn}`);
    } else {
      navigate(`/console?sn=${createdTerminal.sn}`);
    }
  };

  const isOnline = readiness?.online === "online";

  return (
    <Modal
      title={
        <Space align="center">
          <DesktopOutlined style={{ color: "#1677ff", fontSize: 20 }} />
          <span>Мастер подключения терминала L4Desk</span>
        </Space>
      }
      open={open}
      onCancel={onClose}
      width={760}
      footer={null}
      destroyOnClose
    >
      <div style={{ marginTop: 12, marginBottom: 24 }}>
        <Steps
          current={currentStep}
          size="small"
          items={[
            { title: "Регистрация" },
            { title: "PIN и Агент" },
            { title: "Готовность" },
            { title: "Запуск сессии" },
          ]}
        />
      </div>

      {/* STEP 0: Create Terminal */}
      {currentStep === 0 && (
        <div>
          <Paragraph type="secondary">
            Зарегистрируйте новый компьютер в системе L4Desk. Для него будет немедленно сгенерирован
            одноразовый PIN-код для привязки установленного Агента.
          </Paragraph>

          <Form form={form} layout="vertical" onFinish={handleCreateTerminal}>
            <Form.Item
              name="sn"
              label="Серийный номер / Имя терминала"
              rules={[{ required: false }]}
              extra="Оставьте пустым или нажмите кнопку генерации для автоматического назначения имени."
            >
              <Space.Compact style={{ width: "100%" }}>
                <Input placeholder="Например: POS-01 или L4D-A8B2C" />
                <Button onClick={handleGenerateSn}>Сгенерировать</Button>
              </Space.Compact>
            </Form.Item>

            <Form.Item name="address" label="Адрес установки (опционально)">
              <Input placeholder="г. Москва, ул. Тверская, д. 1" />
            </Form.Item>

            <Form.Item name="note" label="Примечание (опционально)">
              <Input placeholder="Главный вход, терминал №1" />
            </Form.Item>

            <Form.Item
              name="timezone"
              label="Часовой пояс"
              initialValue="Europe/Moscow"
              rules={[{ required: true, message: "Выберите часовой пояс" }]}
            >
              <Select options={COMMON_TIMEZONES} showSearch optionFilterProp="label" />
            </Form.Item>

            <div style={{ textAlign: "right", marginTop: 24 }}>
              <Space>
                <Button onClick={onClose}>Отмена</Button>
                <Button type="primary" htmlType="submit" loading={loading} icon={<ArrowRightOutlined />}>
                  Создать и получить PIN
                </Button>
              </Space>
            </div>
          </Form>
        </div>
      )}

      {/* STEP 1: PIN & Agent Download */}
      {currentStep === 1 && createdTerminal && (
        <div>
          <Card
            style={{
              textAlign: "center",
              background: "#fafafa",
              borderColor: "#d9d9d9",
              marginBottom: 20,
            }}
          >
            <Text type="secondary" style={{ fontSize: 13 }}>
              Одноразовый PIN-код активации для {createdTerminal.sn}
            </Text>
            <div
              style={{
                fontSize: 34,
                fontWeight: 700,
                letterSpacing: 6,
                color: "#1677ff",
                fontFamily: "monospace",
                margin: "12px 0",
              }}
            >
              {createdTerminal.pin || "••••••••"}
            </div>
            {createdTerminal.pin && (
              <Button
                type="dashed"
                icon={<CopyOutlined />}
                onClick={() => handleCopyPin(createdTerminal.pin!)}
              >
                Скопировать PIN-код
              </Button>
            )}
            <div style={{ marginTop: 8, fontSize: 12, color: "#8c8c8c" }}>
              Срок действия PIN: 24 часа. Код одноразовый и будет погашен при первом запуске Агента.
            </div>
          </Card>

          <Card
            size="small"
            style={{ background: "#f0f5ff", borderColor: "#adc6ff", marginBottom: 20 }}
          >
            <Space direction="vertical" style={{ width: "100%" }}>
              <Text strong style={{ color: "#1d39c4" }}>
                Инструкция по установке Агента на компьютер:
              </Text>
              <Paragraph style={{ margin: 0, fontSize: 13, lineHeight: 1.6 }}>
                1. Скачайте установочный пакет Агента по кнопке ниже на целевой компьютер.
                <br />
                2. Запустите инсталлятор и введите указанный выше <strong>PIN-код</strong>.
                <br />
                3. Агент автоматически выпустит клиентский сертификат, подключится к защищённой шине и
                перейдёт в режим готовности.
              </Paragraph>
              <div style={{ marginTop: 10 }}>
                <Button
                  type="primary"
                  icon={<DownloadOutlined />}
                  href={createdTerminal.agent_release_url}
                  target="_blank"
                  rel="noopener noreferrer"
                >
                  Скачать Агент L4Desk (v{createdTerminal.agent_version})
                </Button>
              </div>
            </Space>
          </Card>

          <div style={{ textAlign: "right" }}>
            <Button type="primary" onClick={handleProceedToReadiness} icon={<ArrowRightOutlined />}>
              Перейти к проверке подключения
            </Button>
          </div>
        </div>
      )}

      {/* STEP 2: Readiness & Online Polling */}
      {currentStep === 2 && createdTerminal && (
        <div>
          <Paragraph type="secondary">
            Ожидание запуска Агента и выпуска сертификата. Система непрерывно опрашивает статус устройства.
          </Paragraph>

          <Card style={{ marginBottom: 20 }}>
            <Row gutter={[16, 16]}>
              <Col xs={24} sm={12}>
                <div style={{ padding: 12, border: "1px solid #f0f0f0", borderRadius: 8 }}>
                  <Text type="secondary" style={{ fontSize: 12 }}>
                    1. Запись терминала
                  </Text>
                  <div style={{ marginTop: 6, display: "flex", alignItems: "center", gap: 8 }}>
                    <CheckCircleOutlined style={{ color: "#52c41a", fontSize: 18 }} />
                    <Text strong>Создана в реестре</Text>
                  </div>
                </div>
              </Col>

              <Col xs={24} sm={12}>
                <div style={{ padding: 12, border: "1px solid #f0f0f0", borderRadius: 8 }}>
                  <Text type="secondary" style={{ fontSize: 12 }}>
                    2. Клиентский сертификат
                  </Text>
                  <div style={{ marginTop: 6, display: "flex", alignItems: "center", gap: 8 }}>
                    {readiness?.certificate === "consumed" || readiness?.certificate === "issued" ? (
                      <CheckCircleOutlined style={{ color: "#52c41a", fontSize: 18 }} />
                    ) : (
                      <ClockCircleOutlined style={{ color: "#1677ff", fontSize: 18 }} />
                    )}
                    <Text strong>
                      {readiness?.certificate === "consumed"
                        ? "Сертификат установлен"
                        : readiness?.certificate === "issued"
                        ? "Сертификат выпущен"
                        : "Ожидает ввода PIN"}
                    </Text>
                  </div>
                </div>
              </Col>

              <Col xs={24} sm={12}>
                <div style={{ padding: 12, border: "1px solid #f0f0f0", borderRadius: 8 }}>
                  <Text type="secondary" style={{ fontSize: 12 }}>
                    3. IoT / RPC шлюз
                  </Text>
                  <div style={{ marginTop: 6, display: "flex", alignItems: "center", gap: 8 }}>
                    {readiness?.iot === "ready" ? (
                      <CheckCircleOutlined style={{ color: "#52c41a", fontSize: 18 }} />
                    ) : (
                      <ClockCircleOutlined style={{ color: "#faad14", fontSize: 18 }} />
                    )}
                    <Text strong>
                      {readiness?.iot === "ready" ? "Шлюз готов" : "Синхронизация..."}
                    </Text>
                  </div>
                </div>
              </Col>

              <Col xs={24} sm={12}>
                <div style={{ padding: 12, border: "1px solid #f0f0f0", borderRadius: 8 }}>
                  <Text type="secondary" style={{ fontSize: 12 }}>
                    4. Состояние сети (Online)
                  </Text>
                  <div style={{ marginTop: 6, display: "flex", alignItems: "center", gap: 8 }}>
                    {isOnline ? (
                      <Tag color="success" style={{ fontSize: 13, padding: "2px 10px" }}>
                        Онлайн (На связи)
                      </Tag>
                    ) : (
                      <Tag color="default" style={{ fontSize: 13, padding: "2px 10px" }}>
                        Офлайн (Ожидание)
                      </Tag>
                    )}
                  </div>
                </div>
              </Col>
            </Row>

            <Divider style={{ margin: "16px 0" }} />

            <div style={{ textAlign: "center", padding: "8px 0" }}>
              {pollingActive && !isOnline && (
                <Space>
                  <Spin indicator={<SyncOutlined spin />} />
                  <Text type="secondary">Ожидаем завершения установки Агента на компьютере...</Text>
                </Space>
              )}
              {isOnline && (
                <Text type="success" strong style={{ fontSize: 15 }}>
                  ✓ Терминал успешно подключён и готов к удалённому управлению!
                </Text>
              )}
            </div>
          </Card>

          <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
            <Button
              icon={<SyncOutlined spin={pollingActive} />}
              onClick={() => startPolling(createdTerminal.terminal_id)}
            >
              Проверить статус снова
            </Button>

            <Button
              type="primary"
              onClick={() => setCurrentStep(3)}
              disabled={!isOnline}
              icon={<ArrowRightOutlined />}
            >
              Перейти к запуску сессии
            </Button>
          </div>
        </div>
      )}

      {/* STEP 3: Launch First Session */}
      {currentStep === 3 && createdTerminal && (
        <div>
          <Paragraph type="secondary">
            Терминал <strong>{createdTerminal.sn}</strong> готов к работе. Вы можете открыть сессию
            видеонаблюдения или консоли.
          </Paragraph>

          {sessionError && (
            <RefusalReasonCard
              code={sessionError.code}
              rawMessage={sessionError.message}
              onClose={() => setSessionError(null)}
              onRetry={() => setSessionError(null)}
              onTopUp={() => {
                onClose();
                navigate("/licenses");
              }}
            />
          )}

          <Row gutter={[16, 16]} style={{ marginTop: 16 }}>
            <Col xs={24} sm={12}>
              <Card
                hoverable
                style={{ textAlign: "center", borderColor: "#91caff" }}
                onClick={() => handleLaunchSession("video")}
              >
                <VideoCameraOutlined style={{ fontSize: 36, color: "#1677ff", marginBottom: 12 }} />
                <Title level={5} style={{ margin: 0 }}>
                  Видеонаблюдение
                </Title>
                <Paragraph type="secondary" style={{ fontSize: 13, marginTop: 6, marginBottom: 16 }}>
                  Трансляция рабочего стола и удалённое управление мышью и клавиатурой через WebRTC.
                </Paragraph>
                <Button type="primary" icon={<VideoCameraOutlined />}>
                  Открыть видео
                </Button>
              </Card>
            </Col>

            <Col xs={24} sm={12}>
              <Card
                hoverable
                style={{ textAlign: "center", borderColor: "#b7eb8f" }}
                onClick={() => handleLaunchSession("console")}
              >
                <CodeOutlined style={{ fontSize: 36, color: "#52c41a", marginBottom: 12 }} />
                <Title level={5} style={{ margin: 0 }}>
                  Консоль управления
                </Title>
                <Paragraph type="secondary" style={{ fontSize: 13, marginTop: 6, marginBottom: 16 }}>
                  Командная строка CMD / PowerShell в браузере с историей вывода и таймаутами.
                </Paragraph>
                <Button style={{ borderColor: "#52c41a", color: "#389e0d" }} icon={<CodeOutlined />}>
                  Открыть консоль
                </Button>
              </Card>
            </Col>
          </Row>

          <div style={{ textAlign: "right", marginTop: 24 }}>
            <Button onClick={onClose}>Завершить настройку</Button>
          </div>
        </div>
      )}
    </Modal>
  );
}
