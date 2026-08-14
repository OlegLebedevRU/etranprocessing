import { useEffect, useState } from "react";
import { Card, Col, Row, Select, Statistic, Spin, Typography } from "antd";
import {
  FolderOutlined,
  AppstoreOutlined,
  NumberOutlined,
  DollarOutlined,
} from "@ant-design/icons";
import { getStats } from "../api/stats";
import { getMenuVariants, MenuVariant } from "../api/menu-variants";

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
  const [variants, setVariants] = useState<MenuVariant[]>([]);
  const [variantId, setVariantId] = useState<number | null>(null);

  useEffect(() => {
    getMenuVariants()
      .then((res) => {
        setVariants(res.data);
        if (res.data.length > 0) setVariantId(res.data[0].id);
      })
      .catch(() => {});
  }, []);

  useEffect(() => {
    if (variantId === null) return;
    setLoading(true);
    getStats(variantId)
      .then((res) => setStats(res.data))
      .catch(() => {})
      .finally(() => setLoading(false));
  }, [variantId]);

  return (
    <>
      <div style={{ display: "flex", justifyContent: "space-between", marginBottom: 24 }}>
        <Title level={3} style={{ margin: 0 }}>
          Обзор
        </Title>
        <Select
          placeholder="Вариант меню"
          value={variantId}
          onChange={setVariantId}
          style={{ width: 240 }}
          options={variants.map((v) => ({ value: v.id, label: v.name }))}
          showSearch
          optionFilterProp="label"
        />
      </div>
      {loading ? (
        <Spin size="large" style={{ display: "block", margin: "100px auto" }} />
      ) : (
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
      )}
    </>
  );
}
