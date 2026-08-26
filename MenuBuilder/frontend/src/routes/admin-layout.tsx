import { useEffect, useState } from "react";
import { useNavigate } from "react-router";
import { BankOutlined, DesktopOutlined, UserOutlined } from "@ant-design/icons";
import { Alert, Button, Result, Spin } from "antd";
import SectionLayout from "../components/SectionLayout";
import { getMe, type UserInfo } from "../api/auth";

const ADMIN_NAV_ITEMS = [
  {
    key: "organizations",
    icon: <BankOutlined />,
    label: "Организации",
  },
  {
    key: "terminals",
    icon: <DesktopOutlined />,
    label: "Терминалы",
  },
  {
    key: "users",
    icon: <UserOutlined />,
    label: "Пользователи",
  },
];

export default function AdminLayout() {
  const navigate = useNavigate();
  const [loading, setLoading] = useState(true);
  const [isSuperuser, setIsSuperuser] = useState<boolean>(
    localStorage.getItem("mb_is_superuser") === "true"
  );

  useEffect(() => {
    let isMounted = true;
    getMe()
      .then((data) => {
        if (isMounted) {
          const superuser = Boolean(data.is_superuser);
          setIsSuperuser(superuser);
          if (superuser) {
            localStorage.setItem("mb_is_superuser", "true");
          } else {
            localStorage.removeItem("mb_is_superuser");
          }
          setLoading(false);
        }
      })
      .catch(() => {
        if (isMounted) {
          setLoading(false);
        }
      });
    return () => {
      isMounted = false;
    };
  }, []);

  if (loading) {
    return (
      <div style={{ display: "flex", justifyContent: "center", padding: 60 }}>
        <Spin size="large" />
      </div>
    );
  }

  if (!isSuperuser) {
    return (
      <Result
        status="403"
        title="403"
        subTitle="Доступ к разделу Администрирование разрешен только для пользователей с ролью Superuser."
        extra={
          <Button type="primary" onClick={() => navigate("/monitoring")}>
            Вернуться в мониторинг
          </Button>
        }
      />
    );
  }

  return (
    <SectionLayout
      title="Администрирование"
      items={ADMIN_NAV_ITEMS}
      resolvePath={(key) => `/admin/${key}`}
      resolveKey={(pathname) => pathname.split("/")[2] || "organizations"}
    />
  );
}
