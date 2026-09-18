import React, { useEffect, useState } from "react";
import { Link, useSearchParams } from "react-router";
import {
  Alert,
  Button,
  Card,
  Checkbox,
  Form,
  Input,
  Result,
  Select,
  Space,
  Spin,
  Typography,
} from "antd";
import {
  CheckCircleOutlined,
  GlobalOutlined,
  LockOutlined,
  MailOutlined,
  SendOutlined,
} from "@ant-design/icons";
import {
  getRegistrationStatus,
  registerAccount,
  resendConfirmation,
  RegisterRequest,
} from "../api/auth";
import {
  DEFAULT_TIMEZONE,
  TIMEZONE_OPTIONS,
} from "../utils/timezone";

const { Title, Text, Paragraph } = Typography;

export default function RegisterPage() {
  const [searchParams] = useSearchParams();
  const rawReturnUrl = searchParams.get("return_url") || "/monitoring";
  // Safe relative return url guard
  const safeReturnUrl =
    rawReturnUrl.startsWith("/") && !rawReturnUrl.startsWith("//") && !rawReturnUrl.startsWith("/\\")
      ? rawReturnUrl
      : "/monitoring";

  const [loading, setLoading] = useState(false);
  const [checkingStatus, setCheckingStatus] = useState(true);
  const [registrationEnabled, setRegistrationEnabled] = useState(false);
  const [termsVersion, setTermsVersion] = useState("v1");
  const [successEmail, setSuccessEmail] = useState<string | null>(null);
  const [resending, setResending] = useState(false);
  const [resendCooldown, setResendCooldown] = useState(0);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [form] = Form.useForm();

  useEffect(() => {
    let mounted = true;
    async function checkStatus() {
      try {
        const envFlag = (import.meta as unknown as { env?: Record<string, string | undefined> })
          .env?.VITE_L4DESK_REGISTRATION_ENABLED;
        const res = await getRegistrationStatus();
        if (mounted) {
          setRegistrationEnabled(res.enabled || envFlag === "true");
          setTermsVersion(res.terms_version || "v1");
        }
      } catch {
        if (mounted) {
          const envFlag = (import.meta as unknown as { env?: Record<string, string | undefined> })
            .env?.VITE_L4DESK_REGISTRATION_ENABLED;
          setRegistrationEnabled(envFlag === "true");
        }
      } finally {
        if (mounted) {
          setCheckingStatus(false);
        }
      }
    }
    checkStatus();
    return () => {
      mounted = false;
    };
  }, []);

  useEffect(() => {
    if (resendCooldown <= 0) return;
    const timer = setInterval(() => {
      setResendCooldown((prev) => (prev > 0 ? prev - 1 : 0));
    }, 1000);
    return () => clearInterval(timer);
  }, [resendCooldown]);

  const onFinish = async (values: {
    email: string;
    password: string;
    timezone: string;
    agreeTerms: boolean;
  }) => {
    setLoading(true);
    setErrorMessage(null);
    try {
      const payload: RegisterRequest = {
        email: values.email,
        password: values.password,
        terms_version: termsVersion,
        timezone: values.timezone || DEFAULT_TIMEZONE,
        return_url: safeReturnUrl,
      };
      await registerAccount(payload);
      setSuccessEmail(values.email);
      setResendCooldown(60);
    } catch (err: unknown) {
      const e = err as { response?: { data?: { detail?: string } }; message?: string };
      const detail = e.response?.data?.detail || e.message || "Не удалось отправить форму регистрации.";
      setErrorMessage(detail);
    } finally {
      setLoading(false);
    }
  };

  const handleResend = async () => {
    if (!successEmail || resendCooldown > 0) return;
    setResending(true);
    setErrorMessage(null);
    try {
      await resendConfirmation({ email: successEmail, return_url: safeReturnUrl });
      setResendCooldown(60);
    } catch (err: unknown) {
      const e = err as { response?: { data?: { detail?: string } }; message?: string };
      const detail = e.response?.data?.detail || e.message || "Не удалось отправить письмо повторно.";
      setErrorMessage(detail);
    } finally {
      setResending(false);
    }
  };

  if (checkingStatus) {
    return (
      <div
        style={{
          minHeight: "100vh",
          display: "flex",
          justifyContent: "center",
          alignItems: "center",
          background: "#f6f7f9",
        }}
      >
        <Spin size="large" />
      </div>
    );
  }

  if (!registrationEnabled) {
    return (
      <div
        style={{
          minHeight: "100vh",
          display: "flex",
          justifyContent: "center",
          alignItems: "center",
          background: "#f6f7f9",
          padding: 16,
        }}
      >
        <Card style={{ width: "100%", maxWidth: 440, textAlign: "center" }}>
          <Result
            status="info"
            title="Регистрация временно недоступна"
            subTitle="Публичная саморегистрация в данный момент отключена администратором. Пожалуйста, обратитесь в службу поддержки."
            extra={
              <Link to="/login">
                <Button type="primary">Вернуться ко входу</Button>
              </Link>
            }
          />
        </Card>
      </div>
    );
  }

  return (
    <div
      style={{
        minHeight: "100vh",
        display: "flex",
        flexDirection: "column",
        alignItems: "center",
        justifyContent: "center",
        gap: 16,
        background: "#f6f7f9",
        padding: 16,
      }}
    >
      <Card
        style={{
          width: "100%",
          maxWidth: 440,
          boxShadow: "0 8px 28px rgba(15, 23, 42, 0.06)",
        }}
      >
        <div style={{ textAlign: "center", marginBottom: 20 }}>
          <div
            style={{
              width: 40,
              height: 40,
              margin: "0 auto 12px",
              borderRadius: 10,
              background: "#2563eb",
              color: "#fff",
              fontSize: 18,
              fontWeight: 700,
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
            }}
          >
            L4
          </div>
          <Title level={4} style={{ margin: 0 }}>
            Регистрация в L4Desk
          </Title>
          <Text type="secondary" style={{ fontSize: 13 }}>
            Создание организации и аккаунта администратора
          </Text>
        </div>

        {errorMessage && (
          <Alert
            type="error"
            message={errorMessage}
            showIcon
            style={{ marginBottom: 16 }}
            role="alert"
          />
        )}

        {successEmail ? (
          <Result
            status="success"
            icon={<CheckCircleOutlined style={{ color: "#16a34a" }} />}
            title="Проверьте вашу почту"
            subTitle={
              <Paragraph style={{ fontSize: 14 }}>
                Мы отправили ссылку для подтверждения на адрес <strong>{successEmail}</strong>.
                Пожалуйста, перейдите по ссылке в письме, чтобы подтвердить email и активировать организацию.
              </Paragraph>
            }
            extra={
              <Space direction="vertical" style={{ width: "100%" }}>
                <Button
                  icon={<SendOutlined />}
                  onClick={handleResend}
                  loading={resending}
                  disabled={resendCooldown > 0}
                  block
                >
                  {resendCooldown > 0
                    ? `Отправить повторно через (${resendCooldown}с)`
                    : "Отправить письмо еще раз"}
                </Button>
                <Link to="/login" style={{ display: "block", textAlign: "center", marginTop: 8 }}>
                  Перейти на страницу входа
                </Link>
              </Space>
            }
          />
        ) : (
          <Form
            form={form}
            layout="vertical"
            onFinish={onFinish}
            autoComplete="off"
            initialValues={{ timezone: DEFAULT_TIMEZONE }}
            size="large"
          >
            <Form.Item
              name="email"
              label="Электронная почта"
              rules={[
                { required: true, message: "Введите адрес электронной почты" },
                { type: "email", message: "Введите корректный email" },
              ]}
            >
              <Input
                prefix={<MailOutlined style={{ color: "#9ca3af" }} />}
                placeholder="name@company.com"
                aria-required="true"
              />
            </Form.Item>

            <Form.Item
              name="password"
              label="Пароль"
              rules={[
                { required: true, message: "Введите пароль" },
                { min: 8, message: "Минимум 8 символов" },
              ]}
              hasFeedback
            >
              <Input.Password
                prefix={<LockOutlined style={{ color: "#9ca3af" }} />}
                placeholder="Минимум 8 символов"
                aria-required="true"
              />
            </Form.Item>

            <Form.Item
              name="confirmPassword"
              label="Подтверждение пароля"
              dependencies={["password"]}
              hasFeedback
              rules={[
                { required: true, message: "Подтвердите пароль" },
                ({ getFieldValue }) => ({
                  validator(_, value) {
                    if (!value || getFieldValue("password") === value) {
                      return Promise.resolve();
                    }
                    return Promise.reject(new Error("Пароли не совпадают"));
                  },
                }),
              ]}
            >
              <Input.Password
                prefix={<LockOutlined style={{ color: "#9ca3af" }} />}
                placeholder="Повторите пароль"
                aria-required="true"
              />
            </Form.Item>

            <Form.Item
              name="timezone"
              label="Часовой пояс организации"
              rules={[{ required: true, message: "Выберите часовой пояс" }]}
            >
              <Select
                suffixIcon={<GlobalOutlined style={{ color: "#9ca3af" }} />}
                showSearch
                optionFilterProp="label"
                options={TIMEZONE_OPTIONS.map((tz) => ({
                  value: tz.value,
                  label: `${tz.city} (${tz.offset})`,
                }))}
              />
            </Form.Item>

            <Form.Item
              name="agreeTerms"
              valuePropName="checked"
              rules={[
                {
                  validator: (_, value) =>
                    value
                      ? Promise.resolve()
                      : Promise.reject(new Error("Необходимо принять условия соглашения")),
                },
              ]}
            >
              <Checkbox>
                Я принимаю{" "}
                <Text type="secondary" underline style={{ cursor: "pointer" }}>
                  условия использования
                </Text>{" "}
                и согласен на обработку данных
              </Checkbox>
            </Form.Item>

            <Form.Item style={{ marginBottom: 12 }}>
              <Button type="primary" htmlType="submit" loading={loading} block>
                Зарегистрироваться
              </Button>
            </Form.Item>

            <div style={{ textAlign: "center", marginTop: 8 }}>
              <Text type="secondary" style={{ fontSize: 13 }}>
                Уже есть учетная запись?{" "}
                <Link to="/login" style={{ color: "#2563eb", fontWeight: 500 }}>
                  Войти
                </Link>
              </Text>
            </div>
          </Form>
        )}
      </Card>

      <div
        style={{
          width: "100%",
          maxWidth: 440,
          textAlign: "center",
          fontSize: 11,
          lineHeight: 1.6,
          color: "#a0a8b4",
        }}
      >
        L4Desk Terminal Management & Monitoring Ecosystem
      </div>
    </div>
  );
}
