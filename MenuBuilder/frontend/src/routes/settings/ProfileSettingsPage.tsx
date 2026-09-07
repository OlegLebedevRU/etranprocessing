import { useEffect, useState } from "react";
import {
  Alert,
  Badge,
  Button,
  Card,
  Col,
  Divider,
  Form,
  Input,
  Modal,
  Row,
  Select,
  Space,
  Spin,
  Switch,
  Tag,
  Typography,
  message,
} from "antd";
import {
  CheckCircleOutlined,
  ExclamationCircleOutlined,
  KeyOutlined,
  MailOutlined,
  PhoneOutlined,
  SafetyCertificateOutlined,
  UserOutlined,
} from "@ant-design/icons";
import {
  ProfileSettings,
  changePassword,
  confirmEmailOtp,
  getProfileSettings,
  requestEmailVerification,
  updateProfileSettings,
} from "../../api/settings";
import { useSession } from "../../session/SessionContext";

const { Title, Text, Paragraph } = Typography;

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

export default function ProfileSettingsPage() {
  const { user } = useSession();
  const [loading, setLoading] = useState(true);
  const [profile, setProfile] = useState<ProfileSettings | null>(null);

  // Profile fields form
  const [profileForm] = Form.useForm();
  const [savingProfile, setSavingProfile] = useState(false);

  // Password form
  const [passwordForm] = Form.useForm();
  const [savingPassword, setSavingPassword] = useState(false);

  // Email verification modal
  const [emailModalVisible, setEmailModalVisible] = useState(false);
  const [emailStep, setEmailStep] = useState<"input" | "otp">("input");
  const [emailInput, setEmailInput] = useState("");
  const [otpInput, setOtpInput] = useState("");
  const [sendingEmail, setSendingEmail] = useState(false);
  const [confirmingOtp, setConfirmingOtp] = useState(false);

  const isSuperuser = Boolean(user?.is_superuser || user?.role_id === 1);
  const isReadonly = Boolean(isSuperuser || profile?.is_readonly);

  const fetchProfile = async () => {
    setLoading(true);
    try {
      const data = await getProfileSettings(user?.org_id || undefined);
      setProfile(data);
      profileForm.setFieldsValue({
        phone: data.phone,
        timezone: data.timezone,
        notify_by_email: data.notify_by_email,
        send_reports: data.send_reports,
      });
    } catch (err: any) {
      message.error(
        err.response?.data?.detail || "Ошибка загрузки настроек профиля"
      );
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchProfile();
  }, [user?.org_id]);

  const handleSaveProfile = async (values: any) => {
    if (isReadonly) return;
    setSavingProfile(true);
    try {
      const updated = await updateProfileSettings({
        phone: values.phone,
        timezone: values.timezone,
        notify_by_email: values.notify_by_email,
        send_reports: values.send_reports,
      });
      setProfile(updated);
      message.success("Настройки организации успешно сохранены");
    } catch (err: any) {
      message.error(err.response?.data?.detail || "Ошибка сохранения настроек");
    } finally {
      setSavingProfile(false);
    }
  };

  const handleChangePassword = async (values: any) => {
    if (isReadonly) return;
    if (values.new_password !== values.confirm_password) {
      message.error("Новые пароли не совпадают");
      return;
    }
    setSavingPassword(true);
    try {
      const res = await changePassword({
        old_password: values.old_password,
        new_password: values.new_password,
      });
      message.success(res.message || "Пароль успешно изменен");
      passwordForm.resetFields();
    } catch (err: any) {
      message.error(err.response?.data?.detail || "Ошибка смены пароля");
    } finally {
      setSavingPassword(false);
    }
  };

  const handleRequestEmailVerification = async () => {
    if (!emailInput || !emailInput.includes("@")) {
      message.error("Пожалуйста, введите корректный адрес электронной почты");
      return;
    }
    setSendingEmail(true);
    try {
      const res = await requestEmailVerification(emailInput);
      message.success(res.message);
      setEmailStep("otp");
    } catch (err: any) {
      message.error(
        err.response?.data?.detail || "Не удалось отправить код подтверждения"
      );
    } finally {
      setSendingEmail(false);
    }
  };

  const handleConfirmOtp = async () => {
    if (!otpInput || otpInput.trim().length < 4) {
      message.error("Введите код подтверждения из письма");
      return;
    }
    setConfirmingOtp(true);
    try {
      const res = await confirmEmailOtp(otpInput.trim());
      message.success(res.message || "Email успешно подтвержден!");
      setEmailModalVisible(false);
      setEmailStep("input");
      setOtpInput("");
      fetchProfile();
    } catch (err: any) {
      message.error(
        err.response?.data?.detail || "Неверный код подтверждения"
      );
    } finally {
      setConfirmingOtp(false);
    }
  };

  if (loading) {
    return (
      <div style={{ display: "flex", justifyContent: "center", padding: 60 }}>
        <Spin size="large" />
      </div>
    );
  }

  return (
    <div style={{ maxWidth: 1000, margin: "0 auto", paddingBottom: 40 }}>
      {isReadonly && (
        <Alert
          type="info"
          showIcon
          style={{ marginBottom: 16 }}
          message="Режим только для чтения"
          description="Суперпользователь имеет доступ к разделу Настройки только для чтения. Права и возможности суперпользователя сосредоточены в разделе Администрирование."
        />
      )}

      {/* User / Org Header Info */}
      <Card style={{ marginBottom: 16 }}>
        <Row gutter={[16, 16]} align="middle">
          <Col xs={24} md={12}>
            <Space align="center" size="middle">
              <div
                style={{
                  width: 44,
                  height: 44,
                  borderRadius: "50%",
                  backgroundColor: "#1677ff",
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                  color: "#fff",
                  fontSize: 20,
                }}
              >
                <UserOutlined />
              </div>
              <div>
                <Title level={4} style={{ margin: 0 }}>
                  {profile?.username}
                </Title>
                <Text type="secondary">
                  Роль: {profile?.role} (ID роли: {profile?.role_id})
                </Text>
              </div>
            </Space>
          </Col>
          <Col xs={24} md={12} style={{ textAlign: "right" }}>
            <Text strong>Организация: </Text>
            <Tag color="blue" style={{ fontSize: 13, padding: "2px 8px" }}>
              {profile?.org_name} (ID: {profile?.org_id})
            </Tag>
          </Col>
        </Row>
      </Card>

      <Row gutter={[16, 16]}>
        {/* Left Column: Organization Settings & Email */}
        <Col xs={24} lg={14}>
          {/* Organization Settings */}
          <Card
            title={
              <Space>
                <PhoneOutlined />
                <span>Настройки организации и уведомления</span>
              </Space>
            }
            style={{ marginBottom: 16 }}
          >
            <Form
              form={profileForm}
              layout="vertical"
              onFinish={handleSaveProfile}
              disabled={isReadonly}
            >
              <Form.Item
                name="phone"
                label="Телефон организации"
                tooltip="Контактный номер телефона организации"
              >
                <Input placeholder="+7 (999) 000-00-00" allowClear />
              </Form.Item>

              <Form.Item
                name="timezone"
                label="Часовой пояс организации"
                tooltip="Часовой пояс для отчетов и расписаний организации"
                rules={[{ required: true, message: "Выберите часовой пояс" }]}
              >
                <Select
                  options={COMMON_TIMEZONES}
                  showSearch
                  optionFilterProp="label"
                  placeholder="Выберите часовой пояс"
                />
              </Form.Item>

              <Divider style={{ margin: "16px 0" }} />

              <Form.Item
                name="notify_by_email"
                valuePropName="checked"
                label="Отправка уведомлений"
                extra="Уведомления о статусе терминалов, балансе и лицензиях на email организации"
              >
                <Switch checkedChildren="Включено" unCheckedChildren="Отключено" />
              </Form.Item>

              <Form.Item
                name="send_reports"
                valuePropName="checked"
                label="Отправка отчетов"
                extra="Автоматическая отправка финансовых отчетов, актов и Z-отчетов на email организации"
              >
                <Switch checkedChildren="Включено" unCheckedChildren="Отключено" />
              </Form.Item>

              {!isReadonly && (
                <Form.Item style={{ marginBottom: 0 }}>
                  <Button
                    type="primary"
                    htmlType="submit"
                    loading={savingProfile}
                  >
                    Сохранить изменения
                  </Button>
                </Form.Item>
              )}
            </Form>
          </Card>

          {/* Email Verification Card */}
          <Card
            title={
              <Space>
                <MailOutlined />
                <span>Email организации</span>
              </Space>
            }
          >
            <div style={{ marginBottom: 16 }}>
              <Text type="secondary" style={{ display: "block", marginBottom: 6 }}>
                Текущий официальный email:
              </Text>
              <Space align="center" wrap>
                <Text strong style={{ fontSize: 16 }}>
                  {profile?.email || "Не привязан"}
                </Text>
                {profile?.is_email_verified ? (
                  <Tag icon={<CheckCircleOutlined />} color="success">
                    Подтвержден
                  </Tag>
                ) : (
                  <Tag icon={<ExclamationCircleOutlined />} color="warning">
                    Не подтвержден
                  </Tag>
                )}
              </Space>
              {profile?.email_verified_at && (
                <div style={{ marginTop: 4 }}>
                  <Text type="secondary" style={{ fontSize: 12 }}>
                    Подтвержден:{" "}
                    {new Date(profile.email_verified_at).toLocaleString("ru-RU")}
                  </Text>
                </div>
              )}
            </div>

            <Paragraph type="secondary" style={{ fontSize: 13 }}>
              Установка и смена email организации осуществляется строго с подтверждением
              по 6-значному коду или защищенной ссылке в письме. Смена пароля возможна только при
              подтвержденном email.
            </Paragraph>

            {!isReadonly && (
              <Button
                type={profile?.is_email_verified ? "default" : "primary"}
                icon={<SafetyCertificateOutlined />}
                onClick={() => {
                  setEmailInput(profile?.email || "");
                  setEmailStep("input");
                  setOtpInput("");
                  setEmailModalVisible(true);
                }}
              >
                {profile?.email ? "Изменить / подтвердить email" : "Привязать email"}
              </Button>
            )}
          </Card>
        </Col>

        {/* Right Column: Change Password */}
        <Col xs={24} lg={10}>
          <Card
            title={
              <Space>
                <KeyOutlined />
                <span>Смена пароля</span>
              </Space>
            }
          >
            {!profile?.is_email_verified ? (
              <Alert
                type="warning"
                showIcon
                message="Смена пароля недоступна"
                description="Смена своего пароля разрешена только при ранее подтвержденном email организации. Сначала подтвердите email в блоке слева."
              />
            ) : isReadonly ? (
              <Alert
                type="info"
                showIcon
                message="Смена пароля недоступна"
                description="Суперпользователь находится в режиме только для чтения."
              />
            ) : (
              <Form
                form={passwordForm}
                layout="vertical"
                onFinish={handleChangePassword}
              >
                <Form.Item
                  name="old_password"
                  label="Текущий пароль"
                  rules={[
                    {
                      required: true,
                      message: "Введите ваш текущий пароль",
                    },
                  ]}
                >
                  <Input.Password placeholder="Текущий пароль" />
                </Form.Item>

                <Form.Item
                  name="new_password"
                  label="Новый пароль"
                  rules={[
                    {
                      required: true,
                      message: "Введите новый пароль",
                    },
                    {
                      min: 6,
                      message: "Пароль должен содержать не менее 6 символов",
                    },
                  ]}
                >
                  <Input.Password placeholder="Новый пароль (мин. 6 символов)" />
                </Form.Item>

                <Form.Item
                  name="confirm_password"
                  label="Подтверждение нового пароля"
                  dependencies={["new_password"]}
                  rules={[
                    {
                      required: true,
                      message: "Повторите новый пароль",
                    },
                    ({ getFieldValue }) => ({
                      validator(_, value) {
                        if (!value || getFieldValue("new_password") === value) {
                          return Promise.resolve();
                        }
                        return Promise.reject(
                          new Error("Пароли не совпадают")
                        );
                      },
                    }),
                  ]}
                >
                  <Input.Password placeholder="Повторите новый пароль" />
                </Form.Item>

                <Form.Item style={{ marginBottom: 0 }}>
                  <Button
                    type="primary"
                    htmlType="submit"
                    loading={savingPassword}
                    block
                  >
                    Сменить пароль
                  </Button>
                </Form.Item>
              </Form>
            )}
          </Card>
        </Col>
      </Row>

      {/* Email Verification Modal */}
      <Modal
        title="Подтверждение адреса электронной почты"
        open={emailModalVisible}
        onCancel={() => setEmailModalVisible(false)}
        footer={null}
        destroyOnClose
      >
        {emailStep === "input" ? (
          <div>
            <Paragraph type="secondary">
              Введите email адрес организации. На него будет отправлен 6-значный проверочный код
              и защищенная ссылка для подтверждения.
            </Paragraph>
            <div style={{ marginBottom: 16 }}>
              <Input
                type="email"
                placeholder="name@company.com"
                value={emailInput}
                onChange={(e) => setEmailInput(e.target.value)}
                size="large"
                prefix={<MailOutlined style={{ color: "#999" }} />}
              />
            </div>
            <div style={{ textAlign: "right" }}>
              <Space>
                <Button onClick={() => setEmailModalVisible(false)}>Отмена</Button>
                <Button
                  type="primary"
                  loading={sendingEmail}
                  onClick={handleRequestEmailVerification}
                >
                  Получить код
                </Button>
              </Space>
            </div>
          </div>
        ) : (
          <div>
            <Alert
              type="success"
              showIcon
              style={{ marginBottom: 16 }}
              message="Письмо отправлено"
              description={`Проверочный код отправлен на адрес ${emailInput}. Вы можете ввести код ниже или перейти по ссылке из полученного письма.`}
            />

            <div style={{ marginBottom: 16 }}>
              <Text strong style={{ display: "block", marginBottom: 8 }}>
                Введите 6-значный код из письма:
              </Text>
              <Input
                placeholder="123456"
                value={otpInput}
                onChange={(e) => setOtpInput(e.target.value.replace(/\D/g, "").slice(0, 6))}
                maxLength={6}
                size="large"
                style={{
                  fontSize: 24,
                  letterSpacing: 8,
                  textAlign: "center",
                }}
              />
            </div>

            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
              <Button
                type="link"
                size="small"
                onClick={() => setEmailStep("input")}
              >
                Изменить email
              </Button>
              <Space>
                <Button onClick={() => setEmailModalVisible(false)}>Закрыть</Button>
                <Button
                  type="primary"
                  loading={confirmingOtp}
                  onClick={handleConfirmOtp}
                >
                  Подтвердить код
                </Button>
              </Space>
            </div>
          </div>
        )}
      </Modal>
    </div>
  );
}
