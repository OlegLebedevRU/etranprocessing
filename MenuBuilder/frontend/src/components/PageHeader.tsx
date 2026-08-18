import type { ReactNode } from "react";
import { Space, Typography } from "antd";

const { Title, Text } = Typography;

interface PageHeaderProps {
  title: ReactNode;
  subtitle?: ReactNode;
  extra?: ReactNode;
}

/** Consistent page title bar shared by every section page. */
export default function PageHeader({
  title,
  subtitle,
  extra,
}: PageHeaderProps) {
  return (
    <div
      style={{
        display: "flex",
        alignItems: "center",
        justifyContent: "space-between",
        gap: 12,
        marginBottom: 12,
        flexWrap: "wrap",
      }}
    >
      <Space direction="vertical" size={0}>
        <Title level={5} style={{ margin: 0, fontWeight: 600 }}>
          {title}
        </Title>
        {subtitle && (
          <Text type="secondary" style={{ fontSize: 12 }}>
            {subtitle}
          </Text>
        )}
      </Space>
      {extra && <Space wrap>{extra}</Space>}
    </div>
  );
}
