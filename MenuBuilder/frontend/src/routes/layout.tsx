import { useEffect, useState } from "react";
import { Outlet, useNavigate, useLocation } from "react-router";
import { Layout, Menu, Button, Tooltip, Typography, theme } from "antd";
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
} from "@ant-design/icons";
import { getMe, type UserInfo } from "../api/auth";
import { OrgSwitcher } from "../components/OrgSwitcher";

const { Sider, Header, Content } = Layout;
const { Text } = Typography;

const NAV_ITEMS = [
  { key: "monitoring", icon: <DashboardOutlined />, label: "Мониторинг" },
  { key: "menu", icon: <AppstoreOutlined />, label: "Управление меню" },
  { key: "reports", icon: <FileTextOutlined />, label: "Отчёты" },
  { key: "billing", icon: <DollarOutlined />, label: "Лицензии" },
  { key: "integrations", icon: <ApiOutlined />, label: "Интеграции" },
];

export default function AppLayout() {
  const [collapsed, setCollapsed] = useState(false);
  const [currentUser, setCurrentUser] = useState<UserInfo | null>(null);
  const navigate = useNavigate();
  const location = useLocation();
  const { token } = theme.useToken();

  useEffect(() => {
    let isMounted = true;
    getMe()
      .then((data) => {
        if (isMounted) {
          setCurrentUser(data);
          if (data.username) {
            localStorage.setItem("mb_user", data.username);
          }
          if (data.is_superuser) {
            localStorage.setItem("mb_is_superuser", "true");
          }
          if (data.org_id) {
            localStorage.setItem("mb_current_org_id", String(data.org_id));
          }
          if (data.org_name) {
            localStorage.setItem("mb_current_org_name", data.org_name);
          }
        }
      })
      .catch(() => {
        // Handled by axios interceptor
      });
    return () => {
      isMounted = false;
    };
  }, []);

  const isSuperuser =
    Boolean(currentUser?.is_superuser) ||
    localStorage.getItem("mb_is_superuser") === "true";

  const navItems = [
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

  const segment = location.pathname.split("/")[1] || "monitoring";
  const selectedKey = navItems.some((i) => i.key === segment)
    ? segment
    : "monitoring";

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
            padding: "0 16px",
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
          <Text strong style={{ marginLeft: 12, fontSize: 14 }}>
            {navItems.find((i) => i.key === selectedKey)?.label}
          </Text>
          <div
            style={{
              marginLeft: "auto",
              display: "flex",
              alignItems: "center",
              gap: 12,
            }}
          >
            <OrgSwitcher currentUser={currentUser} />
            <Text type="secondary" style={{ fontSize: 13 }}>
              {currentUser?.username || localStorage.getItem("mb_user")}
            </Text>
            <Tooltip title="Выйти">
              <Button
                type="text"
                size="small"
                icon={<LogoutOutlined />}
                onClick={() => {
                  localStorage.removeItem("mb_token");
                  localStorage.removeItem("mb_user");
                  localStorage.removeItem("mb_master_token");
                  localStorage.removeItem("mb_is_superuser");
                  localStorage.removeItem("mb_current_org_id");
                  localStorage.removeItem("mb_current_org_name");
                  window.location.href = "/login";
                }}
              />
            </Tooltip>
          </div>
        </Header>
        <Content style={{ padding: 16 }}>
          <Outlet />
        </Content>
      </Layout>
    </Layout>
  );
}
