import { useNavigate } from "react-router";
import { AuditOutlined, BankOutlined, DesktopOutlined, UserOutlined } from "@ant-design/icons";
import { Badge, Button, Result, Spin } from "antd";
import SectionLayout from "../components/SectionLayout";
import { useSession } from "../session/SessionContext";

import { useAdminDeviceEvents } from "../components/AdminDeviceEventsContext";

const ADMIN_NAV_ITEMS = [
  { key: "device-events", icon: <AuditOutlined />, label: "События устройств" },
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
  {
    key: "hub",
    icon: <AuditOutlined />,
    label: "Хаб и финсверка",
  },
];

export default function AdminLayout() {
  const navigate = useNavigate();
  const { count } = useAdminDeviceEvents();
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
      items={ADMIN_NAV_ITEMS.map(item => item.key === "device-events" ? { ...item, label: <span>События устройств <Badge count={count} overflowCount={99} size="small" /></span> } : item)}
      resolvePath={(key) => `/admin/${key}`}
      resolveKey={(pathname) => pathname.split("/")[2] || "organizations"}
    />
  );
}
