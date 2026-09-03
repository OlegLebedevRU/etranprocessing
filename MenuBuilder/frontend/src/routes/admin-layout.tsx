import { useNavigate } from "react-router";
import { BankOutlined, DesktopOutlined, UserOutlined } from "@ant-design/icons";
import { Button, Result, Spin } from "antd";
import SectionLayout from "../components/SectionLayout";
import { useSession } from "../session/SessionContext";

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
  const { user, loading } = useSession();
  const isSuperuser = Boolean(user?.is_superuser);

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
