import type { ReactNode } from "react";
import { Outlet, useLocation, useNavigate } from "react-router";
import { Menu, theme } from "antd";
import type { MenuProps } from "antd";

interface SectionLayoutProps {
  items: NonNullable<MenuProps["items"]>;
  /** Maps a menu key to the route to navigate to. */
  resolvePath: (key: string) => string;
  /** Derives the selected menu key from the current pathname. */
  resolveKey: (pathname: string) => string;
  title?: ReactNode;
}

/**
 * Two-pane section shell: a slim vertical menu on the left and the nested
 * route content on the right.
 */
export default function SectionLayout({
  items,
  resolvePath,
  resolveKey,
  title,
}: SectionLayoutProps) {
  const navigate = useNavigate();
  const location = useLocation();
  const { token } = theme.useToken();

  return (
    <div style={{ display: "flex", gap: 12, alignItems: "flex-start" }}>
      <div
        style={{
          width: 170,
          flex: "0 0 170px",
          background: token.colorBgContainer,
          border: `1px solid ${token.colorBorderSecondary}`,
          borderRadius: token.borderRadiusLG,
          padding: 6,
          position: "sticky",
          top: 52,
        }}
      >
        {title && (
          <div
            style={{
              fontSize: 11,
              fontWeight: 600,
              letterSpacing: 0.6,
              textTransform: "uppercase",
              color: token.colorTextTertiary,
              padding: "6px 10px 8px",
            }}
          >
            {title}
          </div>
        )}
        <Menu
          mode="inline"
          selectedKeys={[resolveKey(location.pathname)]}
          onClick={({ key }) => navigate(resolvePath(key))}
          items={items}
          style={{ borderInlineEnd: "none", background: "transparent" }}
        />
      </div>
      <div style={{ flex: 1, minWidth: 0 }}>
        <Outlet />
      </div>
    </div>
  );
}
