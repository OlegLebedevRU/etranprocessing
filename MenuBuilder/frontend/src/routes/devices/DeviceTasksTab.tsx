import { useState, useEffect, useCallback, useMemo } from "react";
import { Table, Tag, Button, Space, Typography, Tooltip, Modal, message, Popconfirm, Card, Divider } from "antd";
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
import {
  getMethodDefinition,
  getTaskStatusInfo,
  extractTaskResults,
  formatTimestamp,
  TaskStatus,
} from "./domain";
import CreateTaskModal from "./CreateTaskModal";

const { Text, Paragraph } = Typography;

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
    const info = getTaskStatusInfo(status);
    let icon = <SyncOutlined spin />;
    if (info.state === "success") icon = <CheckCircleOutlined />;
    else if (info.state === "timeout") icon = <ClockCircleOutlined />;
    else if (info.state === "failed") icon = <CloseCircleOutlined />;
    else if (info.state === "deleted") icon = <DeleteOutlined />;

    return (
      <Tooltip title={info.label}>
        <Tag icon={icon} color={info.color}>
          {info.shortLabel}
        </Tag>
      </Tooltip>
    );
  };

  const getMethodLabel = (code: number) => {
    const def = getMethodDefinition(code);
    return def ? def.label : `Метод #${code}`;
  };

  const extractedDetailResults = useMemo(() => {
    return extractTaskResults(selectedTaskDetail);
  }, [selectedTaskDetail]);

  const selectedStatusInfo = useMemo(() => {
    return getTaskStatusInfo(selectedTaskDetail?.status ?? TaskStatus.READY);
  }, [selectedTaskDetail?.status]);

  const columns: ColumnsType<TaskItem> = [
    {
      title: "Дата создания",
      dataIndex: "created_at",
      key: "created_at",
      width: 170,
      render: (val) => (
        <span style={{ fontSize: 13 }}>
          {formatTimestamp(val)}
        </span>
      ),
    },
    {
      title: "Внешний ID (ext_task_id)",
      dataIndex: "ext_task_id",
      key: "ext_task_id",
      width: 170,
      render: (val) => (
        <code style={{ fontSize: 12, color: "#1677ff" }}>
          {val || "—"}
        </code>
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
      width: 130,
      render: (status) => getStatusTag(status),
    },
    {
      title: "Приоритет / TTL",
      key: "ttl",
      width: 150,
      render: (_, record) => (
        <span style={{ fontSize: 12, color: "#595959" }}>
          Приоритет: {record.priority ?? 0} | TTL: {record.ttl ?? record.ttl_minutes ?? 60}м
        </span>
      ),
    },
    {
      title: "Действия",
      key: "actions",
      width: 100,
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
        title={`Детали RPC-задачи #${selectedTaskDetail?.id || ""}`}
        open={detailModalOpen}
        onCancel={() => setDetailModalOpen(false)}
        footer={[
          <Button key="close" type="primary" onClick={() => setDetailModalOpen(false)}>
            Закрыть
          </Button>,
        ]}
        width={680}
      >
        {detailLoading ? (
          <div style={{ textAlign: "center", padding: 24 }}>Загрузка...</div>
        ) : selectedTaskDetail ? (
          <div style={{ display: "flex", flexDirection: "column", gap: 12 }}>
            <Card size="small">
              <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 10 }}>
                <div>
                  <Text type="secondary">Метод: </Text>
                  <Text strong>{getMethodLabel(selectedTaskDetail.method_code)}</Text>
                </div>
                <div>
                  <Text type="secondary">Статус: </Text>
                  {getStatusTag(selectedTaskDetail.status)}
                </div>
                <div>
                  <Text type="secondary">Внешний ID: </Text>
                  <code>{selectedTaskDetail.ext_task_id || "—"}</code>
                </div>
                <div>
                  <Text type="secondary">Приоритет / TTL: </Text>
                  <Text>
                    {selectedTaskDetail.priority ?? 0} / {selectedTaskDetail.ttl ?? selectedTaskDetail.ttl_minutes ?? 60} мин.
                  </Text>
                </div>
                <div>
                  <Text type="secondary">Создана: </Text>
                  <Text>{formatTimestamp(selectedTaskDetail.created_at)}</Text>
                </div>
                <div>
                  <Text type="secondary">Принята (ACK): </Text>
                  <Text>{selectedTaskDetail.pending_at ? formatTimestamp(selectedTaskDetail.pending_at) : "—"}</Text>
                </div>
              </div>
            </Card>

            {/* Полезная нагрузка */}
            {(selectedTaskDetail.payload || selectedTaskDetail.params) && (
              <div>
                <Text type="secondary" strong>Параметры вызова (Payload):</Text>
                <Paragraph
                  copyable={{
                    text: JSON.stringify(selectedTaskDetail.payload || selectedTaskDetail.params, null, 2),
                  }}
                  style={{
                    backgroundColor: "#1e1e1e",
                    color: "#9cdcfe",
                    padding: "8px 12px",
                    borderRadius: 6,
                    fontFamily: "Consolas, Monaco, monospace",
                    fontSize: 12,
                    maxHeight: 140,
                    overflowY: "auto",
                    whiteSpace: "pre-wrap",
                    marginTop: 4,
                    marginBottom: 0,
                  }}
                >
                  {JSON.stringify(selectedTaskDetail.payload || selectedTaskDetail.params, null, 2)}
                </Paragraph>
              </div>
            )}

            {/* Результат выполнения */}
            <div>
              <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 4 }}>
                <Text type="secondary" strong>Результат выполнения (Ответ устройства):</Text>
                <Text type="secondary" style={{ fontSize: 12 }}>
                  {selectedStatusInfo.label}
                </Text>
              </div>
              <Paragraph
                copyable={{
                  text: JSON.stringify(
                    extractedDetailResults.hasResults
                      ? extractedDetailResults.resultsList.length > 0
                        ? extractedDetailResults.resultsList
                        : extractedDetailResults.primaryResult
                      : { status: selectedStatusInfo.label, message: extractedDetailResults.summary },
                    null,
                    2
                  ),
                }}
                style={{
                  backgroundColor: extractedDetailResults.hasResults ? "#0f1f14" : "#1e1e1e",
                  color: extractedDetailResults.hasResults ? "#73d13d" : "#faad14",
                  border: `1px solid ${extractedDetailResults.hasResults ? "#237804" : "#434343"}`,
                  padding: "8px 12px",
                  borderRadius: 6,
                  fontFamily: "Consolas, Monaco, monospace",
                  fontSize: 12,
                  maxHeight: 180,
                  overflowY: "auto",
                  whiteSpace: "pre-wrap",
                  marginBottom: 0,
                }}
              >
                {extractedDetailResults.hasResults
                  ? JSON.stringify(
                      extractedDetailResults.resultsList.length > 0
                        ? extractedDetailResults.resultsList
                        : extractedDetailResults.primaryResult,
                      null,
                      2
                    )
                  : `/* ${extractedDetailResults.summary} */`}
              </Paragraph>
            </div>
          </div>
        ) : null}
      </Modal>
    </div>
  );
}
