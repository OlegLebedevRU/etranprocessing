import React, { useEffect, useState } from "react";
import { Link, useNavigate, useSearchParams } from "react-router";
import {
  Alert,
  Button,
  Card,
  Form,
  Input,
  Result,
  Space,
  Spin,
  Typography,
} from "antd";
import {
  CheckCircleOutlined,
  ClockCircleOutlined,
  CloseCircleOutlined,
  InfoCircleOutlined,
  MailOutlined,
  SendOutlined,
} from "@ant-design/icons";
import {
  confirmRegistration,
  ConfirmRegistrationResponse,
  resendConfirmation,
} from "../api/auth";

const { Text, Paragraph } = Typography;

type ConfirmState =
  | "verifying"
  | "success"
  | "already_confirmed"
  | "expired"
  | "invalid"
  | "missing_token"
  | "error";

export default function ConfirmRegistrationPage() {
  const [searchParams] = useSearchParams();
  const navigate = useNavigate();
  const token = searchParams.get("token") || "";
  const rawReturnUrl = searchParams.get("return_url") || "/monitoring";
  const safeReturnUrl =
    rawReturnUrl.startsWith("/") && !rawReturnUrl.startsWith("//") && !rawReturnUrl.startsWith("/\\")
      ? rawReturnUrl
      : "/monitoring";

  const [state, setState] = useState<ConfirmState>("verifying");
  const [details, setDetails] = useState<ConfirmRegistrationResponse | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  // Resend form state for expired links
  const [resendEmail, setResendEmail] = useState("");
  const [resending, setResending] = useState(false);
  const [resendSuccess, setResendSuccess] = useState(false);
  const [resendCooldown, setResendCooldown] = useState(0);

  useEffect(() => {
    if (resendCooldown <= 0) return;
    const timer = setInterval(() => {
      setResendCooldown((prev) => (prev > 0 ? prev - 1 : 0));
    }, 1000);
    return () => clearInterval(timer);
  }, [resendCooldown]);

  useEffect(() => {
    let active = true;

    if (!token.trim()) {
      setState("missing_token");
      return;
    }

    async function verify() {
      try {
        const res = await confirmRegistration({
          token: token.trim(),
          return_url: safeReturnUrl,
        });
        if (!active) return;
        setDetails(res);
        if (res.status === "already_confirmed") {
          setState("already_confirmed");
        } else {
          setState("success");
        }
      } catch (err: unknown) {
        if (!active) return;
        const e = err as {
          response?: { status?: number; data?: { detail?: string; message?: string } };
          message?: string;
        };
        const detail =
          e.response?.data?.detail || e.response?.data?.message || e.message || "";

        if (detail.includes("token_expired") || detail.toLowerCase().includes("истек")) {
          setState("expired");
        } else if (
          detail.includes("invalid_token") ||
          detail.toLowerCase().includes("неверный") ||
          detail.toLowerCase().includes("не существует")
        ) {
          setState("invalid");
        } else if (detail.includes("already_confirmed")) {
          setState("already_confirmed");
        } else {
          setState("error");
          setErrorMessage(detail || "Произошла ошибка при подтверждении email.");
        }
      }
    }

    verify();

    return () => {
      active = false;
    };
  }, [token, safeReturnUrl]);

  const handleResend = async (values: { email: string }) => {
    setResending(true);
    setErrorMessage(null);
    try {
      await resendConfirmation({
        email: values.email,
        return_url: safeReturnUrl,
      });
      setResendEmail(values.email);
      setResendSuccess(true);
      setResendCooldown(60);
    } catch (err: unknown) {
      const e = err as { response?: { data?: { detail?: string } }; message?: string };
      setErrorMessage(
        e.response?.data?.detail || e.message || "Не удалось отправить письмо повторно."
      );
    } finally {
      setResending(false);
    }
  };

  const handleProceed = () => {
    navigate("/login", { replace: true });
  };

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
          maxWidth: 480,
          boxShadow: "0 8px 28px rgba(15, 23, 42, 0.06)",
        }}
      >
        {state === "verifying" && (
          <div
            style={{ textAlign: "center", padding: "40px 16px" }}
            role="status"
            aria-live="polite"
          >
            <Spin size="large" />
            <div style={{ marginTop: 24 }}>
              <Text strong style={{ fontSize: 16 }}>
                Подтверждение email...
              </Text>
              <Paragraph type="secondary" style={{ marginTop: 8, fontSize: 13 }}>
                Создаем организацию, настраиваем профиль владельца и инициализируем учетную запись L4Desk.
              </Paragraph>
            </div>
          </div>
        )}

        {state === "success" && (
          <Result
            status="success"
            icon={<CheckCircleOutlined style={{ color: "#16a34a" }} />}
            title="Регистрация успешно завершена!"
            subTitle={
              <Paragraph style={{ fontSize: 14 }}>
                {details?.message ||
                  "Email подтвержден. Ваша организация и учетная запись владельца созданы. Теперь вы можете войти в систему под вашим email и паролем."}
              </Paragraph>
            }
            extra={
              <Space direction="vertical" style={{ width: "100%" }}>
                <Button type="primary" size="large" onClick={handleProceed} block>
                  Войти в личный кабинет
                </Button>
              </Space>
            }
          />
        )}

        {state === "already_confirmed" && (
          <Result
            status="info"
            icon={<InfoCircleOutlined style={{ color: "#2563eb" }} />}
            title="Email уже подтвержден"
            subTitle={
              <Paragraph style={{ fontSize: 14 }}>
                {details?.message ||
                  "Эта учетная запись уже была активирована ранее. Повторное подтверждение не требуется."}
              </Paragraph>
            }
            extra={
              <Button type="primary" size="large" onClick={handleProceed} block>
                Перейти ко входу
              </Button>
            }
          />
        )}

        {state === "expired" && (
          <Result
            status="error"
            icon={<ClockCircleOutlined style={{ color: "#f59e0b" }} />}
            title="Срок действия ссылки истек"
            subTitle="Ссылка для подтверждения email действительна в течение 24 часов. Вы можете запросить новую ссылку ниже."
            extra={
              <div style={{ width: "100%", marginTop: 8, textAlign: "left" }}>
                {errorMessage && (
                  <Alert
                    type="error"
                    message={errorMessage}
                    showIcon
                    style={{ marginBottom: 16 }}
                  />
                )}
                {resendSuccess ? (
                  <Alert
                    type="success"
                    message={`Новая ссылка отправлена на ${resendEmail}. Проверьте почту.`}
                    showIcon
                    style={{ marginBottom: 16 }}
                  />
                ) : (
                  <Form onFinish={handleResend} layout="vertical" size="large">
                    <Form.Item
                      name="email"
                      label="Ваша электронная почта"
                      rules={[
                        { required: true, message: "Введите email" },
                        { type: "email", message: "Введите корректный email" },
                      ]}
                    >
                      <Input prefix={<MailOutlined />} placeholder="name@company.com" />
                    </Form.Item>
                    <Form.Item style={{ marginBottom: 8 }}>
                      <Button
                        type="primary"
                        htmlType="submit"
                        icon={<SendOutlined />}
                        loading={resending}
                        disabled={resendCooldown > 0}
                        block
                      >
                        {resendCooldown > 0
                          ? `Отправить повторно (${resendCooldown}с)`
                          : "Запросить новую ссылку"}
                      </Button>
                    </Form.Item>
                  </Form>
                )}
                <div style={{ textAlign: "center", marginTop: 12 }}>
                  <Link to="/login">Вернуться на страницу входа</Link>
                </div>
              </div>
            }
          />
        )}

        {state === "invalid" && (
          <Result
            status="error"
            icon={<CloseCircleOutlined style={{ color: "#ef4444" }} />}
            title="Недействительная ссылка"
            subTitle="Токен подтверждения не найден или указан неверно. Убедитесь, что вы перешли по полной ссылке из письма."
            extra={
              <Space direction="vertical" style={{ width: "100%" }}>
                <Link to="/register" style={{ width: "100%" }}>
                  <Button type="primary" block>
                    Перейти к регистрации
                  </Button>
                </Link>
                <Link to="/login" style={{ display: "block", textAlign: "center", marginTop: 8 }}>
                  Вернуться ко входу
                </Link>
              </Space>
            }
          />
        )}

        {state === "missing_token" && (
          <Result
            status="warning"
            title="Токен не указан"
            subTitle="В ссылке отсутствует токен подтверждения регистрации."
            extra={
              <Space direction="vertical" style={{ width: "100%" }}>
                <Link to="/register" style={{ width: "100%" }}>
                  <Button type="primary" block>
                    Зарегистрироваться
                  </Button>
                </Link>
                <Link to="/login" style={{ display: "block", textAlign: "center", marginTop: 8 }}>
                  Войти в существующий аккаунт
                </Link>
              </Space>
            }
          />
        )}

        {state === "error" && (
          <Result
            status="error"
            title="Ошибка подтверждения"
            subTitle={errorMessage || "Не удалось завершить подтверждение регистрации."}
            extra={
              <Space direction="vertical" style={{ width: "100%" }}>
                <Button
                  type="primary"
                  onClick={() => {
                    setState("verifying");
                    window.location.reload();
                  }}
                  block
                >
                  Повторить попытку
                </Button>
                <Link to="/login" style={{ display: "block", textAlign: "center", marginTop: 8 }}>
                  Вернуться ко входу
                </Link>
              </Space>
            }
          />
        )}
      </Card>

      <div
        style={{
          width: "100%",
          maxWidth: 480,
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
