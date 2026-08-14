import { useEffect, useState } from "react";
import { Card, Col, Row, Statistic, Spin, Typography } from "antd";
import {
  FolderOutlined,
  AppstoreOutlined,
  NumberOutlined,
  DollarOutlined,
} from "@ant-design/icons";
import { getStats } from "../api/stats";

const { Title } = Typography;

interface Stats {
  groups: number;
  services: number;
  tsp_codes: number;
  avg_price: number;
}

const cards = [
  { key: "groups", title: "Групп", icon: <FolderOutlined />, color: "#1677ff" },
  { key: "services", title: "Услуг", icon: <AppstoreOutlined />, color: "#52c41a" },
  { key: "tsp_codes", title: "TSP-кодов", icon: <NumberOutlined />, color: "#fa8c16" },
  { key: "avg_price", title: "Средняя цена", icon: <DollarOutlined />, color: "#722ed1" },
];

export default function DashboardPage() {
  const [stats, setStats] = useState<Stats | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    getStats()
      .then((res) => setStats(res.data))
      .catch(() => {})
      .finally(() => setLoading(false));
  }, []);

  if (loading) return <Spin size="large" style={{ display: "block", margin: "100px auto" }} />;

  return (
    <>
      <Title level={3} style={{ marginBottom: 24 }}>
        Обзор
      </Title>
      <Row gutter={[24, 24]}>
        {cards.map((c) => (
          <Col xs={24} sm={12} lg={6} key={c.key}>
            <Card
              hoverable
              style={{ borderRadius: 12, borderTop: `3px solid ${c.color}` }}
            >
              <Statistic
                title={c.title}
                value={stats?.[c.key as keyof Stats] ?? 0}
                prefix={c.icon}
                valueStyle={{ color: c.color }}
              />
            </Card>
          </Col>
        ))}
      </Row>
    </>
  );
}
