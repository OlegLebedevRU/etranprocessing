import { useState, useEffect, useCallback } from "react";
import { Table, Tag, Button, Space, Typography, Tooltip, Modal, message, Popconfirm } from "antd";
import type { ColumnsType } from "antd/es/table";
import {
  SyncOutlined,
  ClockCircleOutlined,
  CheckCircleOutlined,
  CloseCircleOutlined,
  PlusOutlined,
  ReloadOutlined,
  EyeOutlined,
  DeleteOutlined,
} from "@ant-design/icons";
import {
  getDeviceTasks,
  getTaskDetail,
  deleteDeviceTask,
  type TaskItem,
} from "../../api/devices";
import { METHOD_CATALOG } from "./types";
import CreateTaskModal from "./CreateTaskModal";

const { Text } = Typography;

interface DeviceTasksTabProps {
  deviceId: number;
  sn: string;
  orgId: number;
}

export default function DeviceTasksTab({ deviceId, sn, orgId }: DeviceTasksTabProps) {
  const [loading, setLoading] = useState(false);
  const [tasks, setTasks] = useState<TaskItem[]>([]);
  const [total, setTotal] = useState(0);
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(20);

  const [createModalOpen, setCreateModalOpen] = useState(false);
  const [detailModalOpen, setDetailModalOpen] = useState(false);
  const [selectedTaskDetail, setSelectedTaskDetail] = useState<TaskItem | null>(null);
  const [detailLoading, setDetailLoading] = useState(false);

  const fetchTasks = useCallback(async () => {
    setLoading(true);
    try {
      const res = await getDeviceTasks(orgId, deviceId, page, pageSize);
      setTasks(res.items || []);
      setTotal(res.total || 0);
    } catch (err: any) {
      message.error(err.message || "Ошибка загрузки списка задач");
    } finally {
      setLoading(false);
    }
  }, [orgId, deviceId, page, pageSize]);

  useEffect(() => {
    fetchTasks();
  }, [fetchTasks]);

  const handleOpenDetail = async (taskId: string) => {
    setDetailLoading(true);
    setDetailModalOpen(true);
    try {
      const detail = await getTaskDetail(orgId, taskId);
      setSelectedTaskDetail(detail);
    } catch (err: any) {
      message.error(err.message || "Ошибка загрузки деталей задачи");
    } finally {
      setDetailLoading(false);
    }
  };

  const handleDeleteTask = async (taskId: string) => {
    try {
      await deleteDeviceTask(orgId, taskId);
      message.success("Задача отменена/удалена");
      fetchTasks();
    } catch (err: any) {
      message.error(err.message || "Ошибка удаления задачи");
    }
  };

  const getStatusTag = (status: number) => {
    switch (status) {
      case 0:
      case 1:
      case 2:
        return (
          <Tooltip title="В процессе обработки / очереди">
            <Tag icon={<SyncOutlined spin />} color="processing">
              В процессе
            </Tag>
          </Tooltip>
        );
      case 3:
        return (
          <Tooltip title="Задача успешно выполнена">
            <Tag icon={<CheckCircleOutlined />} color="success">
              Выполнено
            </Tag>
          </Tooltip>
        );
      case 4:
        return (
          <Tooltip title="Истек срок жизни TTL">
            <Tag icon={<ClockCircleOutlined />} color="warning">
              Таймаут
            </Tag>
          </Tooltip>
        );
      case 5:
        return (
          <Tooltip title="Задача отменена пользователем">
            <Tag icon={<DeleteOutlined />} color="default">
              Отменена
            </Tag>
          </Tooltip>
        );
      case 6:
        return (
          <Tooltip title="Ошибка при выполнении">
            <Tag icon={<CloseCircleOutlined />} color="error">
              Ошибка
            </Tag>
          </Tooltip>
        );
      default:
        return <Tag color="default">Статус {status}</Tag>;
    }
  };

  const getMethodLabel = (code: number) => {
    const item = METHOD_CATALOG.find((m) => m.code === code);
    return item ? item.label : `Метод #${code}`;
  };

  const columns: ColumnsType<TaskItem> = [
    {
      title: "Дата создания",
      dataIndex: "created_at",
      key: "created_at",
      width: 170,
      render: (val) => (
        <span style={{ fontSize: 13 }}>
          {val ? new Date(val).toLocaleString("ru-RU") : "—"}
        </span>
      ),
    },
    {
      title: "Метод / Команда",
      dataIndex: "method_code",
      key: "method_code",
      render: (code) => (
        <Text strong style={{ fontSize: 13 }}>
          {getMethodLabel(code)}
        </Text>
      ),
    },
    {
      title: "Статус",
      dataIndex: "status",
      key: "status",
      width: 140,
      render: (status) => getStatusTag(status),
    },
    {
      title: "Приоритет / TTL",
      key: "ttl",
      width: 140,
      render: (_, record) => (
        <span style={{ fontSize: 12, color: "#595959" }}>
          Приоритет: {record.priority ?? 0} | TTL: {record.ttl_minutes ?? 60}м
        </span>
      ),
    },
    {
      title: "Действия",
      key: "actions",
      width: 120,
      align: "center",
      render: (_, record) => (
        <Space size={4}>
          <Tooltip title="Детали и результат задачи">
            <Button
              type="text"
              size="small"
              icon={<EyeOutlined />}
              onClick={() => handleOpenDetail(record.id)}
            />
          </Tooltip>
          {record.status <= 2 && (
            <Popconfirm
              title="Отменить задачу?"
              onConfirm={() => handleDeleteTask(record.id)}
              okText="Да"
              cancelText="Нет"
            >
              <Tooltip title="Отменить задачу">
                <Button type="text" danger size="small" icon={<DeleteOutlined />} />
              </Tooltip>
            </Popconfirm>
          )}
        </Space>
      ),
    },
  ];

  return (
    <div>
      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          alignItems: "center",
          marginBottom: 16,
          flexWrap: "wrap",
          gap: 8,
        }}
      >
        <Text type="secondary">
          Журнал асинхронных задач и RPC-команд для устройства #{deviceId}
        </Text>
        <Space>
          <Button icon={<ReloadOutlined />} onClick={fetchTasks} loading={loading}>
            Обновить
          </Button>
          <Button
            type="primary"
            icon={<PlusOutlined />}
            onClick={() => setCreateModalOpen(true)}
          >
            Новая задача
          </Button>
        </Space>
      </div>

      <Table
        rowKey="id"
        columns={columns}
        dataSource={tasks}
        loading={loading}
        size="small"
        pagination={{
          current: page,
          pageSize,
          total,
          showSizeChanger: true,
          pageSizeOptions: ["10", "20", "50", "100"],
          onChange: (p, ps) => {
            setPage(p);
            setPageSize(ps);
          },
        }}
      />

      <CreateTaskModal
        open={createModalOpen}
        onCancel={() => setCreateModalOpen(false)}
        onSuccess={() => {
          setCreateModalOpen(false);
          fetchTasks();
        }}
        deviceId={deviceId}
        sn={sn}
        orgId={orgId}
      />

      <Modal
        title={`Детали задачи #${selectedTaskDetail?.id || ""}`}
        open={detailModalOpen}
        onCancel={() => setDetailModalOpen(false)}
        footer={[
          <Button key="close" onClick={() => setDetailModalOpen(false)}>
            Закрыть
          </Button>,
        ]}
        width={600}
      >
        {detailLoading ? (
          <div style={{ textAlign: "center", padding: 24 }}>Загрузка...</div>
        ) : selectedTaskDetail ? (
          <div style={{ display: "flex", flexDirection: "column", gap: 12 }}>
            <div>
              <Text type="secondary">Метод: </Text>
              <Text strong>{getMethodLabel(selectedTaskDetail.method_code)}</Text>
            </div>
            <div>
              <Text type="secondary">Статус: </Text>
              {getStatusTag(selectedTaskDetail.status)}
            </div>
            <div>
              <Text type="secondary">Создана: </Text>
              <Text>{new Date(selectedTaskDetail.created_at).toLocaleString("ru-RU")}</Text>
            </div>

            {selectedTaskDetail.params && (
              <div>
                <Text type="secondary">Параметры вызова (Payload):</Text>
                <pre
                  style={{
                    background: "#f5f5f5",
                    padding: 8,
                    borderRadius: 4,
                    fontSize: 12,
                    maxHeight: 160,
                    overflowY: "auto",
                  }}
                >
                  {JSON.stringify(selectedTaskDetail.params, null, 2)}
                </pre>
              </div>
            )}

            <div>
              <Text type="secondary">Результат выполнения (Ответ устройства):</Text>
              <pre
                style={{
                  background: "#f0f5ff",
                  border: "1px solid #d6e4ff",
                  padding: 8,
                  borderRadius: 4,
                  fontSize: 12,
                  maxHeight: 200,
                  overflowY: "auto",
                }}
              >
                {selectedTaskDetail.results
                  ? JSON.stringify(selectedTaskDetail.results, null, 2)
                  : "Ожидается ответ от устройства..."}
              </pre>
            </div>
          </div>
        ) : null}
      </Modal>
    </div>
  );
}
