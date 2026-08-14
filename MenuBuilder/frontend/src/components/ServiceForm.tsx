import { useEffect, useState } from "react";
import { Drawer, Form, Input, InputNumber, message, Select, Space, Button, Typography } from "antd";
import {
  Service,
  ServiceCreate,
  ServiceUpdate,
  createService,
  updateService,
  getFreeTsp,
} from "../api/services";

const { Text } = Typography;

interface Props {
  open: boolean;
  service: Service | null;
  groupId: number;
  onClose: () => void;
  onSaved: () => void;
}

export default function ServiceForm({ open, service, groupId, onClose, onSaved }: Props) {
  const [form] = Form.useForm();
  const [saving, setSaving] = useState(false);
  const [freeTsp, setFreeTsp] = useState<{ tsp_code: number }[]>([]);

  useEffect(() => {
    if (open) {
      if (service) {
        form.setFieldsValue({
          tsp_code: service.tsp_code,
          name: service.name,
          printname: service.printname,
          price: service.price,
          protypenumber: service.protypenumber,
        });
      } else {
        form.resetFields();
        getFreeTsp(groupId)
          .then((res) => setFreeTsp(res.data))
          .catch(() => setFreeTsp([]));
      }
    }
  }, [open, service, groupId, form]);

  const handleOk = async () => {
    try {
      const values = await form.validateFields();
      setSaving(true);
      if (service) {
        const data: ServiceUpdate = {
          name: values.name,
          printname: values.printname,
          price: values.price,
          protypenumber: values.protypenumber,
        };
        await updateService(service.id, data);
        message.success("Обновлено");
      } else {
        const data: ServiceCreate = {
          group_id: groupId,
          tsp_code: values.tsp_code,
          name: values.name,
          printname: values.printname,
          price: values.price || 0,
          protypenumber: values.protypenumber || 0,
        };
        await createService(data);
        message.success("Создано");
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
      title={service ? "Редактировать" : "Новая услуга"}
      open={open}
      onClose={onClose}
      width={320}
      extra={
        <Space>
          <Button size="small" onClick={onClose}>Отмена</Button>
          <Button size="small" type="primary" loading={saving} onClick={handleOk}>OK</Button>
        </Space>
      }
    >
      <Form form={form} layout="vertical" size="small">
        <Form.Item name="tsp_code" label="TSP-код" rules={[{ required: true }]}>
          {service ? (
            <Input disabled size="small" />
          ) : (
            <Select
              showSearch
              placeholder="Код"
              size="small"
              options={freeTsp.map((t) => ({ value: t.tsp_code, label: String(t.tsp_code) }))}
              optionFilterProp="label"
            />
          )}
        </Form.Item>
        <Form.Item name="name" label="Название" rules={[{ required: true }]}>
          <Input size="small" />
        </Form.Item>
        <Form.Item name="printname" label="Печатное имя">
          <Input size="small" />
        </Form.Item>
        <Form.Item name="price" label="Цена">
          <InputNumber min={0} size="small" style={{ width: "100%" }} addonAfter="₽" />
        </Form.Item>
        <Form.Item name="protypenumber" label="Тип номера">
          <InputNumber min={0} size="small" style={{ width: "100%" }} />
        </Form.Item>
      </Form>
    </Drawer>
  );
}
