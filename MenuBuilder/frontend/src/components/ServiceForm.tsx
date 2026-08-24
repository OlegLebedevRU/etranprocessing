import { useEffect, useState } from "react";
import {
  Button,
  Col,
  Form,
  Input,
  InputNumber,
  Modal,
  Row,
  Select,
  Space,
  Typography,
  message,
} from "antd";
import {
  InfoCircleOutlined,
  MobileOutlined,
  PrinterOutlined,
} from "@ant-design/icons";
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

  const nameWatch = Form.useWatch("name", form);
  const priceWatch = Form.useWatch("price", form);
  const printnameWatch = Form.useWatch("printname", form);

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
          .then((res) => {
            setFreeTsp(res.data);
            if (res.data.length > 0) {
              form.setFieldsValue({ tsp_code: res.data[0].tsp_code });
            }
          })
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
          name: values.name.trim(),
          printname: values.printname ? values.printname.trim() : undefined,
          price: values.price || 0,
          protypenumber: values.protypenumber || 0,
        };
        await updateService(service.id, data);
        message.success("Услуга обновлена");
      } else {
        const data: ServiceCreate = {
          group_id: groupId,
          tsp_code: values.tsp_code,
          name: values.name.trim(),
          printname: values.printname ? values.printname.trim() : undefined,
          price: values.price || 0,
          protypenumber: values.protypenumber || 0,
        };
        await createService(data);
        message.success("Услуга создана");
      }
      onSaved();
    } catch (e: any) {
      if (e.message) message.error(e.message);
    } finally {
      setSaving(false);
    }
  };

  const displayName = nameWatch?.trim() || "";
  const displayPrice =
    priceWatch !== undefined && priceWatch !== null && priceWatch !== ""
      ? `${priceWatch} руб.`
      : "0 руб.";
  const displayPrintname = printnameWatch?.trim() || "";

  return (
    <Modal
      title={service ? `Редактирование услуги ТСП #${service.tsp_code}` : "Добавление услуги ТСП"}
      open={open}
      onCancel={onClose}
      onOk={handleOk}
      confirmLoading={saving}
      width={720}
      okText="Сохранить"
      cancelText="Отмена"
      destroyOnClose
      styles={{
        body: { paddingTop: 8 },
      }}
    >
      <Form form={form} layout="vertical">
        <Row gutter={24}>
          {/* LEFT: Inputs */}
          <Col xs={24} sm={14}>
            <Form.Item
              name="tsp_code"
              label="TSP-код"
              rules={[{ required: true, message: "Выберите или введите TSP-код" }]}
              style={{ marginBottom: 12 }}
            >
              {service ? (
                <Input disabled value={service.tsp_code} />
              ) : (
                <Select
                  showSearch
                  placeholder="Выберите свободный TSP-код"
                  options={freeTsp.map((t) => ({ value: t.tsp_code, label: String(t.tsp_code) }))}
                  optionFilterProp="label"
                />
              )}
            </Form.Item>

            <Form.Item
              name="name"
              label="Название для кнопки"
              rules={[{ required: true, message: "Введите название для кнопки" }]}
              tooltip="Текст на экранной кнопке терминала (до 4 строк с переносом по словам)"
              style={{ marginBottom: 12 }}
            >
              <Input.TextArea
                rows={3}
                autoSize={{ minRows: 2, maxRows: 5 }}
                placeholder="Например: 2. Стрижка со сменой насадок"
                style={{ fontSize: 13 }}
              />
            </Form.Item>

            <Form.Item
              name="printname"
              label="Наименование для чека"
              tooltip="Наименование услуги, печатаемое в чеке ККТ (если не заполнено, будет использоваться название для кнопки)"
              style={{ marginBottom: 12 }}
            >
              <Input.TextArea
                rows={2}
                autoSize={{ minRows: 2, maxRows: 3 }}
                placeholder="Наименование в фискальном чеке..."
                style={{ fontSize: 13 }}
              />
            </Form.Item>

            <Row gutter={12}>
              <Col span={12}>
                <Form.Item name="price" label="Сумма / Цена (₽)" style={{ marginBottom: 12 }}>
                  <InputNumber
                    min={0}
                    step={10}
                    style={{ width: "100%" }}
                    addonAfter="₽"
                    placeholder="0"
                  />
                </Form.Item>
              </Col>
              <Col span={12}>
                <Form.Item name="protypenumber" label="Номер Прототипа" style={{ marginBottom: 12 }}>
                  <InputNumber min={0} style={{ width: "100%" }} placeholder="0" />
                </Form.Item>
              </Col>
            </Row>
          </Col>

          {/* RIGHT: Live Terminal Button Preview */}
          <Col xs={24} sm={10}>
            <div
              style={{
                background: "#f8fafc",
                borderRadius: 10,
                border: "1px solid #e2e8f0",
                padding: "16px 14px",
                height: "100%",
                display: "flex",
                flexDirection: "column",
                alignItems: "center",
                justifyContent: "flex-start",
              }}
            >
              <Space size={4} style={{ marginBottom: 12, color: "#475569" }}>
                <MobileOutlined style={{ fontSize: 14 }} />
                <Text strong style={{ fontSize: 12, color: "#334155" }}>
                  Предпросмотр на терминале
                </Text>
              </Space>

              {/* Terminal Button Box */}
              <div
                style={{
                  width: "100%",
                  maxWidth: 240,
                  minHeight: 145,
                  background: "linear-gradient(180deg, #ffffff 0%, #eff6ff 100%)",
                  border: "2px solid #93c5fd",
                  borderRadius: 10,
                  padding: "12px 10px",
                  display: "flex",
                  flexDirection: "column",
                  justifyContent: "space-between",
                  alignItems: "center",
                  textAlign: "center",
                  boxShadow: "0 4px 10px rgba(37, 99, 235, 0.08)",
                  position: "relative",
                  userSelect: "none",
                }}
              >
                <div
                  style={{
                    fontSize: 13,
                    fontWeight: 600,
                    color: "#0f172a",
                    lineHeight: 1.35,
                    whiteSpace: "pre-line",
                    wordBreak: "break-word",
                    display: "-webkit-box",
                    WebkitLineClamp: 4,
                    WebkitBoxOrient: "vertical",
                    overflow: "hidden",
                    textOverflow: "ellipsis",
                    width: "100%",
                    minHeight: 72,
                  }}
                >
                  {displayName ? (
                    displayName
                  ) : (
                    <span
                      style={{
                        color: "#94a3b8",
                        fontWeight: 400,
                        fontStyle: "italic",
                        fontSize: 12,
                      }}
                    >
                      Название для кнопки (до 4 строк)...
                    </span>
                  )}
                </div>

                <div
                  style={{
                    width: "100%",
                    marginTop: 6,
                    paddingTop: 6,
                    borderTop: "1px dashed #bfdbfe",
                    fontSize: 15,
                    fontWeight: 700,
                    color: "#1d4ed8",
                    whiteSpace: "nowrap",
                  }}
                >
                  {displayPrice}
                </div>
              </div>

              <div style={{ marginTop: 14, width: "100%", fontSize: 11, color: "#64748b" }}>
                <div
                  style={{
                    display: "flex",
                    gap: 6,
                    alignItems: "flex-start",
                    marginBottom: 6,
                  }}
                >
                  <InfoCircleOutlined
                    style={{ color: "#3b82f6", marginTop: 2, flexShrink: 0 }}
                  />
                  <span>
                    На кнопке терминала отображается название (перенос по словам до 4 строк) и сумма с «руб.».
                  </span>
                </div>

                {displayPrintname && (
                  <div
                    style={{
                      background: "#ffffff",
                      border: "1px dashed #cbd5e1",
                      borderRadius: 6,
                      padding: "6px 8px",
                      marginTop: 6,
                      fontSize: 11,
                      color: "#334155",
                      wordBreak: "break-word",
                    }}
                  >
                    <Space size={4} style={{ marginBottom: 2 }}>
                      <PrinterOutlined style={{ color: "#64748b" }} />
                      <Text strong style={{ fontSize: 11 }}>
                        Чек ККТ:
                      </Text>
                    </Space>
                    <div>{displayPrintname}</div>
                  </div>
                )}
              </div>
            </div>
          </Col>
        </Row>
      </Form>
    </Modal>
  );
}
