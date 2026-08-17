import { useState } from "react";
import { useNavigate } from "react-router";
import { Button, Card, Form, Input, Typography, message } from "antd";
import { UserOutlined, LockOutlined } from "@ant-design/icons";
import { login } from "../api/auth";

const { Title } = Typography;

export default function LoginPage() {
  const [loading, setLoading] = useState(false);
  const navigate = useNavigate();

  const onFinish = async (values: { username: string; password: string }) => {
    setLoading(true);
    try {
      const result = await login(values.username, values.password);
      localStorage.setItem("mb_token", result.access_token);
      localStorage.setItem("mb_user", values.username);
      message.success("Вход выполнен");
      navigate("/", { replace: true });
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
        alignItems: "center",
        justifyContent: "center",
        background: "#f0f2f5",
      }}
    >
      <Card style={{ width: 360 }}>
        <Title level={4} style={{ textAlign: "center", marginBottom: 24 }}>
          MenuBuilder
        </Title>
        <Form onFinish={onFinish} autoComplete="off">
          <Form.Item name="username" rules={[{ required: true, message: "Введите логин" }]}>
            <Input prefix={<UserOutlined />} placeholder="Логин" size="large" />
          </Form.Item>
          <Form.Item name="password" rules={[{ required: true, message: "Введите пароль" }]}>
            <Input.Password prefix={<LockOutlined />} placeholder="Пароль" size="large" />
          </Form.Item>
          <Form.Item>
            <Button type="primary" htmlType="submit" loading={loading} block size="large">
              Войти
            </Button>
          </Form.Item>
        </Form>
      </Card>
    </div>
  );
}
