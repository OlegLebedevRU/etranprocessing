import { useState } from "react";
import { Outlet, useNavigate, useLocation } from "react-router";
import { Layout, Menu, Button, theme } from "antd";
import {
  MobileOutlined,
  AppstoreOutlined,
  DashboardOutlined,
  FileTextOutlined,
  DollarOutlined,
  MenuFoldOutlined,
  MenuUnfoldOutlined,
  LogoutOutlined,
  UserOutlined,
} from "@ant-design/icons";

const { Sider, Header, Content } = Layout;

export default function AppLayout() {
  const [collapsed, setCollapsed] = useState(false);
  const navigate = useNavigate();
  const location = useLocation();
  const { token } = theme.useToken();

  const selectedKey = location.pathname === "/" ? "terminals" : location.pathname.split("/")[1];

  return (
    <Layout style={{ minHeight: "100vh" }}>
      <Sider
        collapsible
        collapsed={collapsed}
        onCollapse={setCollapsed}
        theme="dark"
        width={180}
        collapsedWidth={48}
        breakpoint="lg"
        onBreakpoint={(broken) => setCollapsed(broken)}
        trigger={null}
      >
        <div
          style={{
            height: 40,
            display: "flex",
            alignItems: "center",
            justifyContent: collapsed ? "center" : "flex-start",
            paddingLeft: collapsed ? 0 : 12,
            color: "#fff",
            fontSize: collapsed ? 14 : 13,
            fontWeight: 600,
            letterSpacing: 0.5,
            whiteSpace: "nowrap",
            overflow: "hidden",
          }}
        >
          {collapsed ? "MB" : "MenuBuilder"}
        </div>
        <Menu
          theme="dark"
          mode="inline"
          selectedKeys={[selectedKey]}
          onClick={({ key }) => navigate(key === "terminals" ? "/" : `/${key}`)}
          items={[
            { key: "terminals", icon: <MobileOutlined />, label: "Терминалы" },
            { key: "variants", icon: <AppstoreOutlined />, label: "Варианты меню" },
            { key: "monitoring", icon: <DashboardOutlined />, label: "Мониторинг" },
            { key: "reports", icon: <FileTextOutlined />, label: "Отчёты" },
            { key: "billing", icon: <DollarOutlined />, label: "Лицензии" },
            { key: "profile", icon: <UserOutlined />, label: "Профиль" },
          ]}
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
            height: 40,
            lineHeight: "40px",
          }}
        >
          {collapsed ? (
            <MenuUnfoldOutlined
              style={{ fontSize: 16, cursor: "pointer" }}
              onClick={() => setCollapsed(false)}
            />
          ) : (
            <MenuFoldOutlined
              style={{ fontSize: 16, cursor: "pointer" }}
              onClick={() => setCollapsed(true)}
            />
          )}
          <div style={{ marginLeft: "auto", display: "flex", alignItems: "center", gap: 8 }}>
            <span style={{ fontSize: 13, color: token.colorTextSecondary }}>
              {localStorage.getItem("mb_user")}
            </span>
            <Button
              type="text"
              icon={<LogoutOutlined />}
              onClick={() => {
                localStorage.removeItem("mb_token");
                localStorage.removeItem("mb_user");
                window.location.href = "/login";
              }}
              title="Выйти"
            />
          </div>
        </Header>
        <Content style={{ padding: 12 }}>
          <Outlet />
        </Content>
      </Layout>
    </Layout>
  );
}
