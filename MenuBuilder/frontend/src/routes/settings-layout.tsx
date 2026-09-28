import { useNavigate } from "react-router";
import { DesktopOutlined, TeamOutlined, UserOutlined } from "@ant-design/icons";
import { Button, Result, Spin } from "antd";
import SectionLayout from "../components/SectionLayout";
import { useSession } from "../session/SessionContext";
import { getNavigationProfile } from "../utils/navigationProfile";
import {
  PERMISSION_SETTINGS_TERMINALS_VIEW,
  hasPermission,
} from "../utils/permissions";

export default function SettingsLayout() {
  const navigate = useNavigate();
  const { user, loading } = useSession();
  const isSuperuser = Boolean(user?.is_superuser || user?.role_id === 1);
  const isRole3 = Boolean(user?.role_id === 3);
  const isRole4 = Boolean(user?.role_id === 4);
  const canViewTerminals = hasPermission(user, PERMISSION_SETTINGS_TERMINALS_VIEW);
  const isL4Desk = getNavigationProfile(user) === "l4desk";

  if (loading) {
    return (
      <div style={{ display: "flex", justifyContent: "center", padding: 60 }}>
        <Spin size="large" />
      </div>
    );
  }

  // Access allowed for role 1, 3, or role 4 with settings:terminals:view
  const hasSettingsAccess = isSuperuser || isRole3 || (isRole4 && canViewTerminals);

  if (!hasSettingsAccess) {
    return (
      <Result
        status="403"
        title="403"
        subTitle="Доступ к разделу Настройки не предоставлен."
        extra={
          <Button type="primary" onClick={() => navigate("/")}>
            На главную
          </Button>
        }
      />
    );
  }

  // Build navigation items based on role. L4Desk has root «Терминалы» — no nested entry.
  let navItems = [];
  if (isL4Desk) {
    navItems = [
      {
        key: "profile",
        icon: <UserOutlined />,
        label: "Профиль",
      },
    ];
    if (!isRole4) {
      navItems.push({
        key: "users",
        icon: <TeamOutlined />,
        label: "Пользователи",
      });
    }
  } else if (isRole4) {
    navItems = [
      {
        key: "terminals",
        icon: <DesktopOutlined />,
        label: "Терминалы",
      },
    ];
  } else {
    navItems = [
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
      {
        key: "users",
        icon: <TeamOutlined />,
        label: "Пользователи",
      },
    ];
  }

  const defaultKey = isL4Desk ? "profile" : isRole4 ? "terminals" : "profile";

  return (
    <SectionLayout
      title="Настройки"
      items={navItems}
      resolvePath={(key) => `/settings/${key}`}
      resolveKey={(pathname) => {
        const seg = pathname.split("/")[2];
        if (isL4Desk && seg === "terminals") {
          return "profile";
        }
        if (!isL4Desk && isRole4 && seg !== "terminals") {
          return "terminals";
        }
        return seg || defaultKey;
      }}
    />
  );
}
