import { useState } from "react";
import { Table, Button, Form, Input, Modal, message, Space, Typography, Tag, Popconfirm } from "antd";
import type { ColumnsType } from "antd/es/table";
import { PlusOutlined, EditOutlined, DeleteOutlined } from "@ant-design/icons";
import { updateDeviceTag, type DeviceTagItem } from "../../api/devices";

const { Text } = Typography;

interface DeviceTagsTabProps {
  deviceId: number;
  orgId: number;
  tags: DeviceTagItem[];
  onTagsUpdated: () => void;
}

export default function DeviceTagsTab({
  deviceId,
  orgId,
  tags,
  onTagsUpdated,
}: DeviceTagsTabProps) {
  const [modalOpen, setModalOpen] = useState(false);
  const [editingTag, setEditingTag] = useState<DeviceTagItem | null>(null);
  const [submitting, setSubmitting] = useState(false);
  const [form] = Form.useForm();

  const handleOpenAdd = () => {
    setEditingTag(null);
    form.resetFields();
    setModalOpen(true);
  };

  const handleOpenEdit = (record: DeviceTagItem) => {
    setEditingTag(record);
    form.setFieldsValue({
      tag: record.tag,
      value: record.value,
    });
    setModalOpen(true);
  };

  const handleSubmit = async (values: any) => {
    setSubmitting(true);
    try {
      await updateDeviceTag(orgId, deviceId, {
        tag: values.tag.trim(),
        value: values.value.trim(),
      });
      message.success(`Тег '${values.tag}' успешно сохранен`);
      setModalOpen(false);
      onTagsUpdated();
    } catch (err: any) {
      message.error(err.message || "Ошибка сохранения тега");
    } finally {
      setSubmitting(false);
    }
  };

  const handleDelete = async (tagName: string) => {
    try {
      // Empty value removes/clears the tag
      await updateDeviceTag(orgId, deviceId, {
        tag: tagName,
        value: "",
      });
      message.success(`Тег '${tagName}' удален`);
      onTagsUpdated();
    } catch (err: any) {
      message.error(err.message || "Ошибка удаления тега");
    }
  };

  const columns: ColumnsType<DeviceTagItem> = [
    {
      title: "Имя тега (Ключ)",
      dataIndex: "tag",
      key: "tag",
      width: 200,
      render: (tag) => (
        <Tag color="purple" style={{ fontSize: 13, padding: "2px 8px" }}>
          {tag}
        </Tag>
      ),
    },
    {
      title: "Значение",
      dataIndex: "value",
      key: "value",
      render: (val) => <Text code>{val}</Text>,
    },
    {
      title: "Действия",
      key: "actions",
      width: 100,
      align: "center",
      render: (_, record) => (
        <Space size={4}>
          <Button
            type="text"
            size="small"
            icon={<EditOutlined />}
            onClick={() => handleOpenEdit(record)}
          />
          <Popconfirm
            title="Удалить тег?"
            onConfirm={() => handleDelete(record.tag)}
            okText="Да"
            cancelText="Нет"
          >
            <Button type="text" danger size="small" icon={<DeleteOutlined />} />
          </Popconfirm>
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
          Метаданные, переменные конфигурации и служебные теги устройства #{deviceId}
        </Text>
        <Button type="primary" icon={<PlusOutlined />} onClick={handleOpenAdd}>
          Добавить тег
        </Button>
      </div>

      <Table
        className="compact-table"
        rowKey="tag"
        columns={columns}
        dataSource={tags}
        pagination={false}
        size="small"
        scroll={{ x: 380 }}
      />

      <Modal
        title={editingTag ? `Редактирование тега '${editingTag.tag}'` : "Добавление нового тега"}
        open={modalOpen}
        onCancel={() => setModalOpen(false)}
        onOk={() => form.submit()}
        confirmLoading={submitting}
        okText="Сохранить"
        cancelText="Отмена"
        destroyOnClose
      >
        <Form form={form} layout="vertical" onFinish={handleSubmit} style={{ marginTop: 16 }}>
          <Form.Item
            name="tag"
            label="Имя тега (Ключ)"
            rules={[{ required: true, message: "Введите имя тега" }]}
          >
            <Input
              placeholder="например: app, cmd, description, location"
              disabled={Boolean(editingTag)}
            />
          </Form.Item>

          <Form.Item
            name="value"
            label="Значение тега"
            rules={[{ required: true, message: "Введите значение тега" }]}
          >
            <Input placeholder="Значение..." />
          </Form.Item>
        </Form>
      </Modal>
    </div>
  );
}
