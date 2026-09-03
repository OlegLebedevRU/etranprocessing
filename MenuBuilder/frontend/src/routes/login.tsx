import { useState } from "react";
import { useNavigate } from "react-router";
import { Button, Card, Form, Input, Typography, message } from "antd";
import { UserOutlined, LockOutlined } from "@ant-design/icons";
import { login } from "../api/auth";
import { scheduleRefresh } from "../api/session";
import { useSession } from "../session/SessionContext";

const { Title, Text } = Typography;

export default function LoginPage() {
  const [loading, setLoading] = useState(false);
  const navigate = useNavigate();
  const { refreshUser } = useSession();

  const onFinish = async (values: { username: string; password: string }) => {
    setLoading(true);
    try {
      const result = await login(values.username, values.password);
      const authTransport = (import.meta as unknown as { env?: Record<string, string | undefined> }).env?.VITE_AUTH_TRANSPORT;
      if (authTransport === "bearer") {
        localStorage.setItem("mb_token", result.access_token);
      }
      await refreshUser(true);
      if (result.expires_in) {
        scheduleRefresh(result.expires_in);
      }
      message.success("Вход выполнен");
      navigate("/monitoring", { replace: true });
    } catch (e: unknown) {
      const err = e as Error;
      message.error(err.message || "Ошибка авторизации");
    } finally {
      setLoading(false);
    }
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
          maxWidth: 380,
          boxShadow: "0 8px 28px rgba(15, 23, 42, 0.06)",
        }}
      >
        <div style={{ textAlign: "center", marginBottom: 24 }}>
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
            P
          </div>
          <Title level={4} style={{ margin: 0 }}>
            PlaterraMonitoring
          </Title>
          <Text type="secondary" style={{ fontSize: 12 }}>
            Вход в личный кабинет
          </Text>
        </div>
        <Form onFinish={onFinish} autoComplete="off" size="large">
          <Form.Item
            name="username"
            rules={[{ required: true, message: "Введите логин" }]}
          >
            <Input prefix={<UserOutlined />} placeholder="Логин" />
          </Form.Item>
          <Form.Item
            name="password"
            rules={[{ required: true, message: "Введите пароль" }]}
          >
            <Input.Password prefix={<LockOutlined />} placeholder="Пароль" />
          </Form.Item>
          <Form.Item style={{ marginBottom: 0 }}>
            <Button type="primary" htmlType="submit" loading={loading} block>
              Войти
            </Button>
          </Form.Item>
        </Form>
      </Card>

      <div
        style={{
          width: "100%",
          maxWidth: 380,
          textAlign: "center",
          fontSize: 11,
          lineHeight: 1.6,
          color: "#a0a8b4",
        }}
      >
        ИП Лебедев О.В., ИНН 741104493519, ОГРНИП 321508100180662
      </div>
    </div>
  );
}
