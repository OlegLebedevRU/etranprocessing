import { useEffect, useState, useCallback } from "react";
import { useParams, useNavigate } from "react-router";
import {
  Button,
  Card,
  message,
  Popconfirm,
  Space,
  Spin,
  Table,
  Tag,
  Typography,
} from "antd";
import {
  ArrowLeftOutlined,
  PlusOutlined,
  EditOutlined,
  DeleteOutlined,
} from "@ant-design/icons";
import { getGroup, Group } from "../api/groups";
import { getServices, deleteService, Service } from "../api/services";
import ServiceForm from "../components/ServiceForm";

const { Title } = Typography;

export default function ServicesPage() {
  const { groupId } = useParams<{ groupId: string }>();
  const navigate = useNavigate();
  const [group, setGroup] = useState<Group | null>(null);
  const [services, setServices] = useState<Service[]>([]);
  const [loading, setLoading] = useState(true);
  const [formOpen, setFormOpen] = useState(false);
  const [editService, setEditService] = useState<Service | null>(null);

  const gid = Number(groupId);

  const load = useCallback(() => {
    setLoading(true);
    Promise.all([getGroup(gid), getServices(gid)])
      .then(([g, s]) => { setGroup(g.data); setServices(s.data); })
      .catch(() => message.error("Ошибка загрузки"))
      .finally(() => setLoading(false));
  }, [gid]);

  useEffect(() => { load(); }, [load]);

  const handleDelete = async (id: number) => {
    try {
      await deleteService(id);
      message.success("Услуга удалена");
      load();
    } catch (e: any) {
      message.error(e.message);
    }
  };

  const columns = [
    {
      title: "TSP-код",
      dataIndex: "tsp_code",
      key: "tsp_code",
      sorter: (a: Service, b: Service) => a.tsp_code - b.tsp_code,
      render: (v: number) => <Tag color="blue">{v}</Tag>,
    },
    {
      title: "Название",
      dataIndex: "name",
      key: "name",
      sorter: (a: Service, b: Service) => a.name.localeCompare(b.name),
    },
    {
      title: "Печатное имя",
      dataIndex: "printname",
      key: "printname",
      render: (v: string | null) => v || "—",
    },
    {
      title: "Цена",
      dataIndex: "price",
      key: "price",
      sorter: (a: Service, b: Service) => a.price - b.price,
      render: (v: number) => `${v} ₽`,
    },
    {
      title: "Номер прототипа",
      dataIndex: "protypenumber",
      key: "protypenumber",
      sorter: (a: Service, b: Service) => a.protypenumber - b.protypenumber,
    },
    {
      title: "",
      key: "actions",
      width: 100,
      render: (_: any, record: Service) => (
        <Space>
          <Button
            type="text"
            icon={<EditOutlined />}
            onClick={() => { setEditService(record); setFormOpen(true); }}
          />
          <Popconfirm
            title="Удалить услугу?"
            onConfirm={() => handleDelete(record.id)}
            okText="Да"
            cancelText="Нет"
          >
            <Button type="text" danger icon={<DeleteOutlined />} />
          </Popconfirm>
        </Space>
      ),
    },
  ];

  if (loading) return <Spin size="large" style={{ display: "block", margin: "100px auto" }} />;

  return (
    <>
      <div style={{ display: "flex", justifyContent: "space-between", marginBottom: 16 }}>
        <Space>
          <Button icon={<ArrowLeftOutlined />} onClick={() => navigate("/groups")}>
            Назад
          </Button>
          <Title level={3} style={{ margin: 0 }}>
            {group?.name || "Группа"}
          </Title>
        </Space>
        <Button
          type="primary"
          icon={<PlusOutlined />}
          onClick={() => { setEditService(null); setFormOpen(true); }}
        >
          Добавить услугу
        </Button>
      </div>

      <Card>
        <Table
          dataSource={services}
          columns={columns}
          rowKey="id"
          pagination={{ pageSize: 20 }}
          locale={{ emptyText: "Нет услуг" }}
        />
      </Card>

      <ServiceForm
        open={formOpen}
        service={editService}
        groupId={gid}
        onClose={() => setFormOpen(false)}
        onSaved={() => { setFormOpen(false); load(); }}
      />
    </>
  );
}
