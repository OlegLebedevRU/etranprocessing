import { useState } from "react";
import { Outlet, useNavigate, useLocation } from "react-router";
import { Layout, Menu, Button, Tooltip, Typography, theme, Grid } from "antd";
import {
  AppstoreOutlined,
  DashboardOutlined,
  FileTextOutlined,
  DollarOutlined,
  MenuFoldOutlined,
  MenuUnfoldOutlined,
  LogoutOutlined,
  ApiOutlined,
  ControlOutlined,
  ClusterOutlined,
  SettingOutlined,
} from "@ant-design/icons";
import { logout } from "../api/auth";
import { notifySessionEvent } from "../api/session";
import { useSession } from "../session/SessionContext";
import { OrgSwitcher } from "../components/OrgSwitcher";

const { Sider, Header, Content } = Layout;
const { Text } = Typography;

const NAV_ITEMS = [
  { key: "monitoring", icon: <DashboardOutlined />, label: "Мониторинг" },
  { key: "menu", icon: <AppstoreOutlined />, label: "Управление меню" },
  { key: "reports", icon: <FileTextOutlined />, label: "Отчёты" },
  { key: "billing", icon: <DollarOutlined />, label: "Лицензии" },
  { key: "integrations", icon: <ApiOutlined />, label: "Интеграции" },
  { key: "settings", icon: <SettingOutlined />, label: "Настройки" },
];

export default function AppLayout() {
  const [collapsed, setCollapsed] = useState(false);
  const { user, setUser } = useSession();
  const currentUser = user;
  const navigate = useNavigate();
  const location = useLocation();
  const { token } = theme.useToken();
  const screens = Grid.useBreakpoint();
  const isXs = screens.xs;

  const isSuperuser = Boolean(currentUser?.is_superuser);

  const isPlatformMode = Boolean(isSuperuser && currentUser?.org_id === 0);

  const navItems = isPlatformMode
    ? [
        {
          key: "devices",
          icon: <ClusterOutlined />,
          label: "Управление устройствами",
        },
        {
          key: "settings",
          icon: <SettingOutlined />,
          label: "Настройки",
        },
        {
          key: "admin",
          icon: <ControlOutlined />,
          label: "Администрирование",
        },
      ]
    : [
        ...NAV_ITEMS,
        ...(isSuperuser
          ? [
              {
                key: "devices",
                icon: <ClusterOutlined />,
                label: "Управление устройствами",
              },
              {
                key: "admin",
                icon: <ControlOutlined />,
                label: "Администрирование",
              },
            ]
          : []),
      ];

  const segment = location.pathname.split("/")[1] || (isPlatformMode ? "admin" : "monitoring");
  const selectedKey = navItems.some((i) => i.key === segment)
    ? segment
    : (isPlatformMode ? "admin" : "monitoring");

  return (
    <Layout style={{ minHeight: "100vh" }}>
      <Sider
        collapsible
        collapsed={collapsed}
        onCollapse={setCollapsed}
        theme="light"
        width={196}
        collapsedWidth={56}
        breakpoint="lg"
        onBreakpoint={(broken) => setCollapsed(broken)}
        trigger={null}
        style={{
          borderInlineEnd: `1px solid ${token.colorBorderSecondary}`,
          position: "sticky",
          top: 0,
          height: "100vh",
          overflowY: "auto",
          zIndex: 100,
        }}
      >
        <div
          style={{
            height: 48,
            display: "flex",
            alignItems: "center",
            gap: 8,
            justifyContent: collapsed ? "center" : "flex-start",
            padding: collapsed ? 0 : "0 16px",
            whiteSpace: "nowrap",
            overflow: "hidden",
          }}
        >
          <span
            style={{
              width: 22,
              height: 22,
              flex: "0 0 22px",
              borderRadius: 6,
              background: token.colorPrimary,
              color: "#fff",
              fontSize: 11,
              fontWeight: 700,
              display: "inline-flex",
              alignItems: "center",
              justifyContent: "center",
            }}
          >
            P
          </span>
          {!collapsed && (
            <Text
              strong
              style={{ fontSize: 14, letterSpacing: -0.2 }}
              ellipsis
            >
              PlaterraMonitoring
            </Text>
          )}
        </div>
        <Menu
          theme="light"
          mode="inline"
          selectedKeys={[selectedKey]}
          onClick={({ key }) => navigate(`/${key}`)}
          items={navItems}
          style={{ borderInlineEnd: "none", paddingInline: 6 }}
        />
      </Sider>
      <Layout>
        <Header
          style={{
            padding: isXs ? "0 8px" : "0 16px",
            background: token.colorBgContainer,
            display: "flex",
            alignItems: "center",
            borderBottom: `1px solid ${token.colorBorderSecondary}`,
            height: 48,
            lineHeight: "48px",
            position: "sticky",
            top: 0,
            zIndex: 10,
          }}
        >
          <Button
            type="text"
            size="small"
            icon={collapsed ? <MenuUnfoldOutlined /> : <MenuFoldOutlined />}
            onClick={() => setCollapsed((v) => !v)}
          />
          <Text strong style={{ marginLeft: isXs ? 6 : 12, fontSize: isXs ? 13 : 14 }} ellipsis>
            {navItems.find((i) => i.key === selectedKey)?.label}
          </Text>
          <div
            style={{
              marginLeft: "auto",
              display: "flex",
              alignItems: "center",
              gap: isXs ? 6 : 12,
            }}
          >
            <OrgSwitcher currentUser={currentUser} />
            {!isXs && (
              <Text type="secondary" style={{ fontSize: 13 }} ellipsis>
                {currentUser?.username || "Пользователь"}
              </Text>
            )}
            <Tooltip title="Выйти" mouseEnterDelay={0.3}>
              <Button
                type="text"
                size="small"
                icon={<LogoutOutlined />}
                onClick={async () => {
                  notifySessionEvent({ type: "logout" });
                  await logout();
                  setUser(null);
                  window.location.href = "/login";
                }}
              />
            </Tooltip>
          </div>
        </Header>
        <Content style={{ padding: isXs ? 8 : 16 }}>
          {isPlatformMode &&
          ["monitoring", "menu", "reports", "billing", "integrations"].includes(segment) ? (
            <div
              style={{
                textAlign: "center",
                padding: "48px 16px",
                background: token.colorBgContainer,
                borderRadius: 8,
              }}
            >
              <Typography.Title level={4}>Выберите организацию</Typography.Title>
              <Typography.Text type="secondary">
                Вы находитесь в платформенном контексте. Для доступа к тенантным разделам выберите организацию в переключателе сверху.
              </Typography.Text>
            </div>
          ) : (
            <Outlet />
          )}
        </Content>
      </Layout>
    </Layout>
  );
}
