import { useNavigate } from "react-router";
import { DesktopOutlined, UserOutlined } from "@ant-design/icons";
import { Button, Result, Spin } from "antd";
import SectionLayout from "../components/SectionLayout";
import { useSession } from "../session/SessionContext";

const SETTINGS_NAV_ITEMS = [
  {
    key: "profile",
    icon: <UserOutlined />,
    label: "Профиль",
  },
  {
    key: "terminals",
    icon: <DesktopOutlined />,
    label: "Терминалы",
  },
];

export default function SettingsLayout() {
  const navigate = useNavigate();
  const { user, loading } = useSession();
  const isSuperuser = Boolean(user?.is_superuser || user?.role_id === 1);
  const isRole3 = Boolean(user?.role_id === 3);

  if (loading) {
    return (
      <div style={{ display: "flex", justifyContent: "center", padding: 60 }}>
        <Spin size="large" />
      </div>
    );
  }

  // Access allowed only for role 3 and superuser
  if (!isSuperuser && !isRole3) {
    return (
      <Result
        status="403"
        title="403"
        subTitle="Доступ к разделу Настройки разрешен только для пользователей с ролью 3 и суперпользователя."
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
      title="Настройки"
      items={SETTINGS_NAV_ITEMS}
      resolvePath={(key) => `/settings/${key}`}
      resolveKey={(pathname) => pathname.split("/")[2] || "profile"}
    />
  );
}
