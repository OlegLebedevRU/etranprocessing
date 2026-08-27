import { useState } from "react";
import { Modal, Form, Select, InputNumber, Input, Alert, message, Divider } from "antd";
import { METHOD_CATALOG } from "./types";
import { createDeviceTask, type TaskCreateInput } from "../../api/devices";

interface CreateTaskModalProps {
  open: boolean;
  onCancel: () => void;
  onSuccess: () => void;
  deviceId: number;
  sn: string;
  orgId: number;
}

export default function CreateTaskModal({
  open,
  onCancel,
  onSuccess,
  deviceId,
  sn,
  orgId,
}: CreateTaskModalProps) {
  const [form] = Form.useForm();
  const [submitting, setSubmitting] = useState(false);
  const [selectedMethodCode, setSelectedMethodCode] = useState<number>(1);

  const selectedMethod = METHOD_CATALOG.find((m) => m.code === selectedMethodCode);

  const handleSubmit = async (values: any) => {
    setSubmitting(true);
    try {
      let params: Record<string, any> = {};

      if (values.params_json && values.params_json.trim()) {
        try {
          params = JSON.parse(values.params_json.trim());
        } catch {
          message.error("Некорректный формат JSON в параметрах");
          setSubmitting(false);
          return;
        }
      } else if (selectedMethod?.fields) {
        selectedMethod.fields.forEach((f) => {
          if (values[f.name] !== undefined) {
            params[f.name] = values[f.name];
          }
        });
      }

      const payload: TaskCreateInput = {
        device_id: deviceId,
        method_code: Number(values.method_code),
        priority: Number(values.priority || 0),
        ttl_minutes: Number(values.ttl_minutes || 60),
        params: Object.keys(params).length > 0 ? params : undefined,
      };

      await createDeviceTask(orgId, payload);
      message.success(`Команда ${payload.method_code} успешно поставлена в очередь для устройства #${deviceId}`);
      form.resetFields();
      onSuccess();
    } catch (err: any) {
      message.error(err.message || "Ошибка отправки команды");
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <Modal
      title={`Создание и отправка RPC-задачи (Устройство #${deviceId}, SN: ${sn})`}
      open={open}
      onCancel={onCancel}
      onOk={() => form.submit()}
      confirmLoading={submitting}
      okText="Отправить команду"
      cancelText="Отмена"
      width={560}
      destroyOnClose
    >
      <Form
        form={form}
        layout="vertical"
        initialValues={{
          method_code: 1,
          priority: 0,
          ttl_minutes: 60,
        }}
        onFinish={handleSubmit}
        style={{ marginTop: 16 }}
      >
        <Form.Item
          name="method_code"
          label="RPC Метод / Команда"
          rules={[{ required: true, message: "Выберите метод" }]}
        >
          <Select
            showSearch
            optionFilterProp="label"
            onChange={(val) => setSelectedMethodCode(Number(val))}
            options={METHOD_CATALOG.map((m) => ({
              value: m.code,
              label: m.label,
            }))}
          />
        </Form.Item>

        {selectedMethod && (
          <Alert
            type="info"
            showIcon
            message={selectedMethod.description}
            style={{ marginBottom: 16 }}
          />
        )}

        {/* Dynamic method fields */}
        {selectedMethod?.fields && selectedMethod.fields.map((field) => (
          <Form.Item
            key={field.name}
            name={field.name}
            label={field.label}
            tooltip={field.tooltip}
            initialValue={field.defaultValue}
          >
            {field.type === "number" ? (
              <InputNumber style={{ width: "100%" }} />
            ) : (
              <Input placeholder={`Введите ${field.label.toLowerCase()}...`} />
            )}
          </Form.Item>
        ))}

        {(!selectedMethod?.fields || selectedMethod.code === 99) && (
          <Form.Item
            name="params_json"
            label="Параметры payload (JSON, опционально)"
            tooltip="Произвольный JSON объект параметров"
          >
            <Input.TextArea
              rows={3}
              placeholder='{"key": "value"}'
              style={{ fontFamily: "monospace", fontSize: 13 }}
            />
          </Form.Item>
        )}

        <Divider style={{ margin: "12px 0" }} />

        <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
          <Form.Item
            name="priority"
            label="Приоритет (0..10)"
            tooltip="0 - обычный, 10 - наивысший"
          >
            <InputNumber min={0} max={10} style={{ width: "100%" }} />
          </Form.Item>

          <Form.Item
            name="ttl_minutes"
            label="Время жизни TTL (мин.)"
            tooltip="Срок действия задачи в очереди до отмены по таймауту"
          >
            <InputNumber min={1} max={1440} style={{ width: "100%" }} />
          </Form.Item>
        </div>
      </Form>
    </Modal>
  );
}
