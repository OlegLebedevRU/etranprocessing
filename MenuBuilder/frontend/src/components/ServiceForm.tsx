import { useEffect, useState } from "react";
import {
  Checkbox,
  Col,
  Form,
  Input,
  InputNumber,
  Modal,
  Radio,
  Row,
  Select,
  Space,
  Tag,
  Typography,
  message,
} from "antd";
import {
  BookOutlined,
  FolderOutlined,
  InfoCircleOutlined,
  MobileOutlined,
  PlusCircleOutlined,
  PrinterOutlined,
} from "@ant-design/icons";
import {
  Service,
  ServiceCreate,
  ServiceUpdate,
  createService,
  getFreeTsp,
  updateService,
} from "../api/services";
import {
  CatalogCategory,
  CatalogItem,
  getCatalogCategories,
  getCatalogItems,
} from "../api/catalog";

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

  // Creation mode: "catalog" or "custom"
  const [createMode, setCreateMode] = useState<"catalog" | "custom">("catalog");

  // Free TSP codes
  const [freeTsp, setFreeTsp] = useState<{ tsp_code: number }[]>([]);

  // Catalog data
  const [categories, setCategories] = useState<CatalogCategory[]>([]);
  const [catalogItems, setCatalogItems] = useState<CatalogItem[]>([]);
  const [selectedCatId, setSelectedCatId] = useState<number | null>(null);
  const [selectedCatalogItem, setSelectedCatalogItem] = useState<CatalogItem | null>(null);

  // Watch fields
  const nameWatch = Form.useWatch("name", form);
  const priceWatch = Form.useWatch("price", form);
  const printnameWatch = Form.useWatch("printname", form);
  const addToCatalogWatch = Form.useWatch("add_to_catalog", form);

  // Flatten categories
  const flatCategories = categories.flatMap(function flatten(c): { id: number; name: string; depth: number }[] {
    return [{ id: c.id, name: c.name, depth: c.depth || 1 }, ...(c.children ? c.children.flatMap(flatten) : [])];
  });

  const loadCatalogData = async () => {
    try {
      const [catRes, itemRes] = await Promise.all([
        getCatalogCategories(),
        getCatalogItems(),
      ]);
      setCategories(catRes.data);
      setCatalogItems(itemRes.data);
    } catch {
      // Ignore
    }
  };

  const loadFreeTsp = (fromCatalog: boolean) => {
    getFreeTsp(groupId, fromCatalog)
      .then((res) => {
        setFreeTsp(res.data);
        if (res.data.length > 0 && !service && createMode === "custom") {
          form.setFieldsValue({ tsp_code: res.data[0].tsp_code });
        }
      })
      .catch(() => setFreeTsp([]));
  };

  useEffect(() => {
    if (open) {
      loadCatalogData();
      if (service) {
        form.setFieldsValue({
          tsp_code: service.tsp_code,
          name: service.name,
          printname: service.printname,
          price: service.price,
          protypenumber: service.protypenumber,
          catalog_item_id: service.catalog_item_id,
        });
      } else {
        form.resetFields();
        setSelectedCatalogItem(null);
        setSelectedCatId(null);
        setCreateMode("catalog");
        loadFreeTsp(false);
      }
    }
  }, [open, service, groupId, form]);

  useEffect(() => {
    if (open && !service && createMode === "custom") {
      loadFreeTsp(!!addToCatalogWatch);
    }
  }, [open, service, createMode, addToCatalogWatch]);

  // When a catalog item is selected in "catalog" mode
  const handleCatalogItemSelect = (itemId: number) => {
    const item = catalogItems.find((i) => i.id === itemId);
    if (item) {
      setSelectedCatalogItem(item);
      form.setFieldsValue({
        catalog_item_id: item.id,
        tsp_code: item.tsp_code,
        name: item.name,
        printname: item.printname || "",
        price: item.price,
        protypenumber: item.protypenumber,
      });
    }
  };

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
          catalog_item_id: values.catalog_item_id || undefined,
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
          catalog_item_id: createMode === "catalog" ? values.catalog_item_id : undefined,
          add_to_catalog: createMode === "custom" ? !!values.add_to_catalog : false,
          catalog_category_id: values.catalog_category_id || undefined,
        };
        await createService(data);
        message.success("Услуга создана");
      }
      onSaved();
    } catch (e: any) {
      if (e?.response?.data?.detail) {
        message.error(e.response.data.detail);
      } else if (e.message) {
        message.error(e.message);
      }
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

  // Filter catalog items by selected category
  const filteredCatalogItems = selectedCatId
    ? catalogItems.filter((i) => i.category_id === selectedCatId)
    : catalogItems;

  return (
    <Modal
      title={
        service ? (
          <Space>
            <span>Редактирование услуги ТСП #{service.tsp_code}</span>
            {service.catalog_item_id && <Tag color="blue">Из каталога</Tag>}
          </Space>
        ) : (
          "Добавление услуги в меню"
        )
      }
      open={open}
      onCancel={onClose}
      onOk={handleOk}
      confirmLoading={saving}
      width={780}
      okText="Сохранить"
      cancelText="Отмена"
      destroyOnClose
      styles={{
        body: { paddingTop: 8 },
      }}
    >
      {!service && (
        <div style={{ marginBottom: 16 }}>
          <Radio.Group
            value={createMode}
            onChange={(e) => {
              const mode = e.target.value;
              setCreateMode(mode);
              if (mode === "custom") {
                loadFreeTsp(false);
              }
            }}
            buttonStyle="solid"
            style={{ width: "100%", display: "flex" }}
          >
            <Radio.Button
              value="catalog"
              style={{ flex: 1, textAlign: "center", height: 38, lineHeight: "36px" }}
            >
              <BookOutlined style={{ marginRight: 6 }} />
              Выбрать из каталога
            </Radio.Button>
            <Radio.Button
              value="custom"
              style={{ flex: 1, textAlign: "center", height: 38, lineHeight: "36px" }}
            >
              <PlusCircleOutlined style={{ marginRight: 6 }} />
              Создать новую услугу
            </Radio.Button>
          </Radio.Group>
        </div>
      )}

      <Form form={form} layout="vertical">
        {/* Hidden field for catalog_item_id */}
        <Form.Item name="catalog_item_id" hidden>
          <Input />
        </Form.Item>

        <Row gutter={24}>
          {/* LEFT: Inputs */}
          <Col xs={24} sm={14}>
            {!service && createMode === "catalog" && (
              <div
                style={{
                  background: "#f0f5ff",
                  padding: "12px 14px",
                  borderRadius: 6,
                  border: "1px solid #adc6ff",
                  marginBottom: 16,
                }}
              >
                <div style={{ fontWeight: 600, marginBottom: 8, color: "#1d39c4" }}>
                  <FolderOutlined style={{ marginRight: 6 }} />
                  Выбор из справочника каталога
                </div>

                <Row gutter={8}>
                  <Col span={10}>
                    <Select
                      placeholder="Все разделы"
                      allowClear
                      style={{ width: "100%", marginBottom: 8 }}
                      value={selectedCatId}
                      onChange={(val) => setSelectedCatId(val || null)}
                    >
                      {flatCategories.map((c) => (
                        <Select.Option key={c.id} value={c.id}>
                          {"— ".repeat(c.depth - 1) + c.name}
                        </Select.Option>
                      ))}
                    </Select>
                  </Col>
                  <Col span={14}>
                    <Select
                      showSearch
                      placeholder="Выберите услугу..."
                      style={{ width: "100%", marginBottom: 8 }}
                      value={selectedCatalogItem?.id}
                      onChange={handleCatalogItemSelect}
                      optionFilterProp="label"
                      options={filteredCatalogItems.map((item) => ({
                        value: item.id,
                        label: `[${item.tsp_code}] ${item.name} (${item.price} ₽)`,
                      }))}
                    />
                  </Col>
                </Row>
              </div>
            )}

            {!service && createMode === "custom" && (
              <div
                style={{
                  background: "#fafafa",
                  padding: "10px 12px",
                  borderRadius: 6,
                  border: "1px solid #f0f0f0",
                  marginBottom: 12,
                }}
              >
                <Form.Item
                  name="add_to_catalog"
                  valuePropName="checked"
                  style={{ marginBottom: addToCatalogWatch ? 8 : 0 }}
                >
                  <Checkbox>Добавить услугу в каталог (диапазон ТСП 1000301..1000999)</Checkbox>
                </Form.Item>

                {addToCatalogWatch && (
                  <Form.Item
                    name="catalog_category_id"
                    label="Раздел каталога для сохранения"
                    rules={[{ required: true, message: "Выберите раздел каталога" }]}
                    style={{ marginBottom: 0 }}
                  >
                    <Select placeholder="Выберите категорию каталога">
                      {flatCategories.map((c) => (
                        <Select.Option key={c.id} value={c.id}>
                          {"— ".repeat(c.depth - 1) + c.name}
                        </Select.Option>
                      ))}
                    </Select>
                  </Form.Item>
                )}
              </div>
            )}

            <Form.Item
              name="tsp_code"
              label={
                <span>
                  Код ТСП{" "}
                  <Text type="secondary" style={{ fontSize: 12 }}>
                    {createMode === "catalog" || addToCatalogWatch
                      ? "(Каталог: 1000301..1000999)"
                      : "(Локальная: 1001301..1001999)"}
                  </Text>
                </span>
              }
              rules={[{ required: true, message: "Выберите или введите код ТСП" }]}
              style={{ marginBottom: 12 }}
            >
              {service ? (
                <Input disabled value={service.tsp_code} />
              ) : createMode === "catalog" ? (
                <InputNumber disabled style={{ width: "100%" }} />
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
                  width: 220,
                  minHeight: 110,
                  maxHeight: 140,
                  background: "#1e3a5f",
                  borderRadius: 12,
                  boxShadow: "0 4px 12px rgba(30, 58, 95, 0.25)",
                  border: "2px solid #2d5a88",
                  padding: "10px 12px",
                  display: "flex",
                  flexDirection: "column",
                  justifyContent: "space-between",
                  color: "#ffffff",
                  userSelect: "none",
                  cursor: "default",
                  boxSizing: "border-box",
                  overflow: "hidden",
                }}
              >
                {/* Service Name: up to 4 lines */}
                <div
                  style={{
                    fontSize: 13,
                    fontWeight: 600,
                    lineHeight: 1.3,
                    color: "#f1f5f9",
                    display: "-webkit-box",
                    WebkitLineClamp: 4,
                    WebkitBoxOrient: "vertical",
                    overflow: "hidden",
                    wordBreak: "break-word",
                    flex: 1,
                  }}
                >
                  {displayName || (
                    <span style={{ color: "#94a3b8", fontStyle: "italic", fontWeight: 400 }}>
                      Название услуги...
                    </span>
                  )}
                </div>

                {/* Bottom Bar: Price badge */}
                <div
                  style={{
                    display: "flex",
                    justifyContent: "flex-end",
                    alignItems: "center",
                    marginTop: 6,
                    paddingTop: 4,
                    borderTop: "1px solid rgba(255, 255, 255, 0.12)",
                  }}
                >
                  <span
                    style={{
                      background: "rgba(255, 255, 255, 0.18)",
                      borderRadius: 4,
                      padding: "1px 7px",
                      fontSize: 12,
                      fontWeight: 700,
                      color: "#93c5fd",
                      letterSpacing: "0.2px",
                    }}
                  >
                    {displayPrice}
                  </span>
                </div>
              </div>

              {/* Receipt Preview */}
              <div
                style={{
                  marginTop: 16,
                  width: 220,
                  background: "#fff",
                  border: "1px dashed #cbd5e1",
                  borderRadius: 6,
                  padding: "8px 10px",
                  fontSize: 11,
                  color: "#64748b",
                }}
              >
                <div style={{ display: "flex", alignItems: "center", gap: 4, marginBottom: 4 }}>
                  <PrinterOutlined style={{ fontSize: 11, color: "#94a3b8" }} />
                  <span style={{ fontWeight: 600, color: "#475569", fontSize: 10 }}>
                    В чеке ККТ:
                  </span>
                </div>
                <div
                  style={{
                    fontFamily: "monospace",
                    color: "#1e293b",
                    wordBreak: "break-word",
                    fontSize: 11,
                  }}
                >
                  {displayPrintname || displayName || "—"}
                </div>
              </div>

              {/* Hint */}
              <div
                style={{
                  marginTop: 12,
                  display: "flex",
                  alignItems: "flex-start",
                  gap: 4,
                  maxWidth: 220,
                }}
              >
                <InfoCircleOutlined
                  style={{ fontSize: 11, color: "#94a3b8", marginTop: 2, flexShrink: 0 }}
                />
                <Text style={{ fontSize: 10, color: "#94a3b8", lineHeight: 1.3 }}>
                  Внешний вид кнопки адаптирован под стандартное экранное меню терминала
                </Text>
              </div>
            </div>
          </Col>
        </Row>
      </Form>
    </Modal>
  );
}
