import { useState } from "react";
import { Outlet, useNavigate, useLocation } from "react-router";
import { Layout, Menu, theme } from "antd";
import {
  DashboardOutlined,
  FolderOutlined,
  MenuOutlined,
  MobileOutlined,
} from "@ant-design/icons";

const { Sider, Header, Content } = Layout;

export default function AppLayout() {
  const [collapsed, setCollapsed] = useState(false);
  const navigate = useNavigate();
  const location = useLocation();
  const { token } = theme.useToken();

  const selectedKey = location.pathname === "/" ? "dashboard" : location.pathname.split("/")[1];

  return (
    <Layout style={{ minHeight: "100vh" }}>
      <Sider
        collapsible
        collapsed={collapsed}
        onCollapse={setCollapsed}
        theme="dark"
        width={240}
        breakpoint="lg"
        onBreakpoint={(broken) => setCollapsed(broken)}
      >
        <div
          style={{
            height: 64,
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            color: "#fff",
            fontSize: collapsed ? 18 : 20,
            fontWeight: 700,
            letterSpacing: 1,
          }}
        >
          {collapsed ? "MB" : "MenuBuilder"}
        </div>
        <Menu
          theme="dark"
          mode="inline"
          selectedKeys={[selectedKey]}
          onClick={({ key }) => navigate(key === "dashboard" ? "/" : `/${key}`)}
          items={[
            { key: "dashboard", icon: <DashboardOutlined />, label: "Дашборд" },
            { key: "groups", icon: <FolderOutlined />, label: "Группы" },
            { key: "terminals", icon: <MobileOutlined />, label: "Терминалы" },
          ]}
        />
      </Sider>
      <Layout>
        <Header
          style={{
            padding: "0 24px",
            background: token.colorBgContainer,
            display: "flex",
            alignItems: "center",
            boxShadow: "0 1px 4px rgba(0,0,0,.08)",
            position: "sticky",
            top: 0,
            zIndex: 10,
          }}
        >
          <MenuOutlined
            style={{ fontSize: 18, cursor: "pointer", marginRight: 16 }}
            onClick={() => setCollapsed(!collapsed)}
          />
        </Header>
        <Content style={{ margin: 24 }}>
          <Outlet />
        </Content>
      </Layout>
    </Layout>
  );
}
