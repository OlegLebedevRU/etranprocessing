import type { ReactNode } from "react";
import { Outlet, useLocation, useNavigate } from "react-router";
import { Grid, Menu, Segmented, theme } from "antd";
import type { MenuProps } from "antd";

const { useBreakpoint } = Grid;

interface SectionLayoutProps {
  items: NonNullable<MenuProps["items"]>;
  /** Maps a menu key to the route to navigate to. */
  resolvePath: (key: string) => string;
  /** Derives the selected menu key from the current pathname. */
  resolveKey: (pathname: string) => string;
  title?: ReactNode;
}

/**
 * Two-pane section shell: a slim vertical menu on the left on desktop,
 * and a compact top Segmented navigation on mobile screens.
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
  const screens = useBreakpoint();
  const isMobile = !screens.md;

  const currentKey = resolveKey(location.pathname);

  // Prepare options for mobile Segmented bar
  const segmentedOptions = (items || [])
    .map((item: any) => {
      if (!item || typeof item !== "object" || !("key" in item)) return null;
      return {
        value: String(item.key),
        label: (
          <span
            style={{
              display: "inline-flex",
              alignItems: "center",
              gap: 6,
              fontSize: 12,
              fontWeight: 500,
            }}
          >
            {item.icon}
            <span>{item.label}</span>
          </span>
        ),
      };
    })
    .filter((opt): opt is NonNullable<typeof opt> => opt !== null);

  if (isMobile) {
    return (
      <div style={{ display: "flex", flexDirection: "column", gap: 10 }}>
        <div
          style={{
            background: token.colorBgContainer,
            border: `1px solid ${token.colorBorderSecondary}`,
            borderRadius: token.borderRadiusLG,
            padding: "8px 10px",
            display: "flex",
            flexDirection: "column",
            gap: 6,
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
              }}
            >
              {title}
            </div>
          )}
          <Segmented
            block
            size="middle"
            value={currentKey}
            onChange={(val) => navigate(resolvePath(String(val)))}
            options={segmentedOptions}
          />
        </div>
        <div style={{ minWidth: 0, width: "100%" }}>
          <Outlet />
        </div>
      </div>
    );
  }

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
          top: 64,
          maxHeight: "calc(100vh - 80px)",
          overflowY: "auto",
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
          selectedKeys={[currentKey]}
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
