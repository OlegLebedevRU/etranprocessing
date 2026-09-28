import { useEffect, useState } from "react";
import { Alert, Button, Form, Input, Modal, Select, Space, message } from "antd";
import { updateTerminalSettings, type TerminalSettingsItem } from "../api/settings";
import { TIMEZONE_OPTIONS } from "../utils/timezone";

type Values = { address?: string; note?: string; timezone?: string };

export default function TerminalSettingsEditModal({
  terminal,
  readOnly = false,
  onClose,
  onSaved,
}: {
  terminal: TerminalSettingsItem | null;
  readOnly?: boolean;
  onClose: () => void;
  onSaved: () => void;
}) {
  const [form] = Form.useForm<Values>();
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    if (terminal) {
      form.setFieldsValue({
        address: terminal.address || "",
        note: terminal.note || "",
        timezone: terminal.timezone || undefined,
      });
    }
  }, [form, terminal]);

  const save = async (values: Values) => {
    if (!terminal || readOnly) return;
    setSaving(true);
    try {
      await updateTerminalSettings(terminal.id, {
        address: values.address?.trim() || null,
        note: values.note?.trim() || null,
        timezone: values.timezone || null,
      });
      message.success(`Терминал ${terminal.device_id} обновлён`);
      onSaved();
      onClose();
    } catch (error: any) {
      message.error(error.response?.data?.detail || "Ошибка сохранения терминала");
    } finally {
      setSaving(false);
    }
  };

  return (
    <Modal
      title={`${readOnly ? "Просмотр" : "Редактирование"} терминала ${terminal?.device_id ?? ""}`}
      open={Boolean(terminal)}
      onCancel={onClose}
      footer={null}
      destroyOnClose
    >
      <Alert
        type="info"
        showIcon
        style={{ marginBottom: 16 }}
        message={readOnly ? "Режим только для чтения" : "Параметры терминала"}
        description={readOnly ? "Параметры доступны только для просмотра." : "Можно изменить адрес, примечание и часовой пояс."}
      />
      <Form form={form} layout="vertical" onFinish={save} disabled={readOnly}>
        <Form.Item name="address" label="Адрес установки">
          <Input.TextArea rows={2} maxLength={500} showCount />
        </Form.Item>
        <Form.Item name="note" label="Примечание">
          <Input.TextArea rows={2} maxLength={500} showCount />
        </Form.Item>
        <Form.Item name="timezone" label="Часовой пояс терминала">
          <Select
            options={TIMEZONE_OPTIONS.map(({ value, label }) => ({ value, label }))}
            showSearch
            allowClear
            optionFilterProp="label"
            placeholder="По умолчанию (часовой пояс организации)"
          />
        </Form.Item>
        <div style={{ textAlign: "right" }}>
          <Space>
            <Button onClick={onClose}>{readOnly ? "Закрыть" : "Отмена"}</Button>
            {!readOnly && <Button type="primary" htmlType="submit" loading={saving}>Сохранить</Button>}
          </Space>
        </div>
      </Form>
    </Modal>
  );
}
