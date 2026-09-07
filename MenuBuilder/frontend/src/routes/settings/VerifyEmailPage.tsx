import { useEffect, useState } from "react";
import { useNavigate, useSearchParams } from "react-router";
import { Button, Result, Spin } from "antd";
import { confirmEmailToken } from "../../api/settings";

export default function VerifyEmailPage() {
  const [searchParams] = useSearchParams();
  const navigate = useNavigate();
  const token = searchParams.get("token");

  const [loading, setLoading] = useState(true);
  const [success, setSuccess] = useState(false);
  const [verifiedEmail, setVerifiedEmail] = useState("");
  const [errorMsg, setErrorMsg] = useState("");

  useEffect(() => {
    if (!token) {
      setLoading(false);
      setErrorMsg("Токен подтверждения не найден в ссылке.");
      return;
    }

    const verify = async () => {
      try {
        const res = await confirmEmailToken(token);
        setSuccess(true);
        setVerifiedEmail(res.email);
      } catch (err: any) {
        setErrorMsg(
          err.response?.data?.detail ||
            "Ссылка подтверждения недействительна или срок её действия истёк."
        );
      } finally {
        setLoading(false);
      }
    };

    verify();
  }, [token]);

  if (loading) {
    return (
      <div
        style={{
          display: "flex",
          flexDirection: "column",
          alignItems: "center",
          justifyContent: "center",
          minHeight: "50vh",
          gap: 16,
        }}
      >
        <Spin size="large" />
        <div>Проверка токена подтверждения email...</div>
      </div>
    );
  }

  if (success) {
    return (
      <div style={{ maxWidth: 600, margin: "60px auto" }}>
        <Result
          status="success"
          title="Email успешно подтвержден!"
          subTitle={`Адрес ${verifiedEmail} успешно привязан к вашей организации. Теперь вам доступна отправка отчетов, уведомлений и смена пароля.`}
          extra={[
            <Button
              type="primary"
              key="profile"
              onClick={() => navigate("/settings/profile")}
            >
              Перейти в настройки профиля
            </Button>,
          ]}
        />
      </div>
    );
  }

  return (
    <div style={{ maxWidth: 600, margin: "60px auto" }}>
      <Result
        status="error"
        title="Ошибка подтверждения email"
        subTitle={errorMsg}
        extra={[
          <Button
            type="primary"
            key="profile"
            onClick={() => navigate("/settings/profile")}
          >
            Перейти в профиль
          </Button>,
        ]}
      />
    </div>
  );
}
