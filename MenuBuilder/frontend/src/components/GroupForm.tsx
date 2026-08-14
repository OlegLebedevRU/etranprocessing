import { useEffect, useState } from "react";
import { Drawer, Form, Input, InputNumber, message, Select, Space, Button } from "antd";
import { Group, GroupCreate, createGroup, updateGroup, GroupUpdate } from "../api/groups";

interface Props {
  open: boolean;
  group: Group | null;
  parentId: number | null;
  groups: Group[];
  onClose: () => void;
  onSaved: () => void;
}

export default function GroupForm({ open, group, parentId, groups, onClose, onSaved }: Props) {
  const [form] = Form.useForm();
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    if (open) {
      if (group) {
        form.setFieldsValue({ name: group.name, parent_id: group.parent_id, number: group.number });
      } else {
        form.resetFields();
        if (parentId) form.setFieldsValue({ parent_id: parentId });
      }
    }
  }, [open, group, parentId, form]);

  const handleOk = async () => {
    try {
      const values = await form.validateFields();
      setSaving(true);
      if (group) {
        const data: GroupUpdate = { name: values.name, parent_id: values.parent_id || null, number: values.number };
        await updateGroup(group.id, data);
        message.success("Группа обновлена");
      } else {
        const data: GroupCreate = { org_id: 1, name: values.name, parent_id: values.parent_id || null, number: values.number || 0 };
        await createGroup(data);
        message.success("Группа создана");
      }
      onSaved();
    } catch (e: any) {
      if (e.message) message.error(e.message);
    } finally {
      setSaving(false);
    }
  };

  return (
    <Drawer
      title={group ? "Редактировать группу" : "Новая группа"}
      open={open}
      onClose={onClose}
      width={400}
      extra={
        <Space>
          <Button onClick={onClose}>Отмена</Button>
          <Button type="primary" loading={saving} onClick={handleOk}>
            Сохранить
          </Button>
        </Space>
      }
    >
      <Form form={form} layout="vertical">
        <Form.Item name="name" label="Название" rules={[{ required: true, message: "Введите название" }]}>
          <Input />
        </Form.Item>
        <Form.Item name="number" label="Номер">
          <InputNumber min={0} style={{ width: "100%" }} />
        </Form.Item>
        <Form.Item name="parent_id" label="Родительская группа">
          <Select
            allowClear
            placeholder="Нет (корневая)"
            options={groups
              .filter((g) => g.id !== group?.id)
              .map((g) => ({ value: g.id, label: `${g.number}. ${g.name}` }))}
            showSearch
            optionFilterProp="label"
          />
        </Form.Item>
      </Form>
    </Drawer>
  );
}
