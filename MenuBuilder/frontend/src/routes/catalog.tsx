import { useEffect, useMemo, useState } from "react";
import {
  Button,
  Card,
  Col,
  Empty,
  Form,
  Input,
  InputNumber,
  Modal,
  Popconfirm,
  Row,
  Select,
  Space,
  Table,
  Tag,
  Tree,
  Typography,
  message,
  Grid,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import type { DataNode } from "antd/es/tree";
import {
  AppstoreOutlined,
  DeleteOutlined,
  EditOutlined,
  FolderAddOutlined,
  FolderOutlined,
  PlusOutlined,
  ReloadOutlined,
  SearchOutlined,
  SyncOutlined,
} from "@ant-design/icons";

import {
  CatalogCategory,
  CatalogItem,
  createCatalogCategory,
  createCatalogItem,
  deleteCatalogCategory,
  deleteCatalogItem,
  getCatalogCategories,
  getCatalogItems,
  getFreeCatalogTsp,
  propagateCatalogToMenus,
  updateCatalogCategory,
  updateCatalogItem,
} from "../api/catalog";

const { Title, Text } = Typography;
const { useBreakpoint } = Grid;

export default function CatalogPage() {
  const screens = useBreakpoint();
  const isMobile = !screens.md;

  const [categories, setCategories] = useState<CatalogCategory[]>([]);
  const [items, setItems] = useState<CatalogItem[]>([]);
  const [loading, setLoading] = useState(false);
  const [itemsLoading, setItemsLoading] = useState(false);

  // Selection
  const [selectedCatId, setSelectedCatId] = useState<number | null>(null);
  const [searchText, setSearchText] = useState("");

  // Category Modal
  const [catModalVisible, setCatModalVisible] = useState(false);
  const [editingCategory, setEditingCategory] = useState<CatalogCategory | null>(null);
  const [catParentId, setCatParentId] = useState<number | null>(null);
  const [catForm] = Form.useForm();
  const [catSubmitting, setCatSubmitting] = useState(false);

  // Item Modal
  const [itemModalVisible, setItemModalVisible] = useState(false);
  const [editingItem, setEditingItem] = useState<CatalogItem | null>(null);
  const [freeTspCodes, setFreeTspCodes] = useState<number[]>([]);
  const [itemForm] = Form.useForm();
  const [itemSubmitting, setItemSubmitting] = useState(false);

  // Propagate Modal / Loading
  const [propagateLoading, setPropagateLoading] = useState(false);

  const loadCategories = async () => {
    setLoading(true);
    try {
      const res = await getCatalogCategories();
      setCategories(res.data);
    } catch (e: any) {
      message.error(e?.response?.data?.detail || "Ошибка загрузки категорий каталога");
    } finally {
      setLoading(false);
    }
  };

  const loadItems = async (catId?: number | null, search?: string) => {
    setItemsLoading(true);
    try {
      const res = await getCatalogItems(catId ?? undefined, search);
      setItems(res.data);
    } catch (e: any) {
      message.error(e?.response?.data?.detail || "Ошибка загрузки услуг каталога");
    } finally {
      setItemsLoading(false);
    }
  };

  useEffect(() => {
    loadCategories();
  }, []);

  useEffect(() => {
    loadItems(selectedCatId, searchText);
  }, [selectedCatId, searchText]);

  // Flattened category list for dropdowns
  const flatCategories = useMemo(() => {
    const list: { id: number; name: string; depth: number }[] = [];
    const traverse = (cats: CatalogCategory[]) => {
      for (const c of cats) {
        list.push({ id: c.id, name: c.name, depth: c.depth });
        if (c.children && c.children.length > 0) {
          traverse(c.children);
        }
      }
    };
    traverse(categories);
    return list;
  }, [categories]);

  // Find category by id
  const findCategory = (id: number): CatalogCategory | null => {
    const searchIn = (cats: CatalogCategory[]): CatalogCategory | null => {
      for (const c of cats) {
        if (c.id === id) return c;
        if (c.children) {
          const found = searchIn(c.children);
          if (found) return found;
        }
      }
      return null;
    };
    return searchIn(categories);
  };

  // Convert categories tree to Ant Design Tree DataNode
  const treeData = useMemo<DataNode[]>(() => {
    const transform = (cats: CatalogCategory[]): DataNode[] =>
      cats.map((c) => ({
        key: String(c.id),
        title: (
          <div
            style={{
              display: "flex",
              justifyContent: "space-between",
              alignItems: "center",
              width: "100%",
              paddingRight: 8,
            }}
          >
            <span>
              <FolderOutlined style={{ marginRight: 6, color: "#1890ff" }} />
              {c.name}
            </span>
            <Tag color="blue" style={{ marginLeft: 8, fontSize: 11 }}>
              {c.items_count}
            </Tag>
          </div>
        ),
        children: c.children ? transform(c.children) : [],
      }));

    return [
      {
        key: "all",
        title: (
          <span style={{ fontWeight: 600 }}>
            <AppstoreOutlined style={{ marginRight: 6 }} />
            Все разделы каталога
          </span>
        ),
        children: transform(categories),
      },
    ];
  }, [categories]);

  // --- Category Actions ---
  const handleAddRootCategory = () => {
    setEditingCategory(null);
    setCatParentId(null);
    catForm.resetFields();
    catForm.setFieldsValue({ name: "", sort_order: 0 });
    setCatModalVisible(true);
  };

  const handleAddSubCategory = (parent: CatalogCategory) => {
    if (parent.depth >= 3) {
      message.warning("Максимальная глубина категорий — 3 уровня");
      return;
    }
    setEditingCategory(null);
    setCatParentId(parent.id);
    catForm.resetFields();
    catForm.setFieldsValue({ name: "", sort_order: 0 });
    setCatModalVisible(true);
  };

  const handleEditCategory = (cat: CatalogCategory) => {
    setEditingCategory(cat);
    setCatParentId(cat.parent_id);
    catForm.resetFields();
    catForm.setFieldsValue({
      name: cat.name,
      sort_order: cat.sort_order,
    });
    setCatModalVisible(true);
  };

  const handleDeleteCategory = async (catId: number) => {
    try {
      await deleteCatalogCategory(catId);
      message.success("Категория удалена");
      if (selectedCatId === catId) setSelectedCatId(null);
      loadCategories();
      loadItems(null, searchText);
    } catch (e: any) {
      message.error(e?.response?.data?.detail || "Ошибка удаления категории");
    }
  };

  const handleSaveCategory = async () => {
    try {
      const values = await catForm.validateFields();
      setCatSubmitting(true);
      if (editingCategory) {
        await updateCatalogCategory(editingCategory.id, {
          name: values.name,
          sort_order: values.sort_order,
        });
        message.success("Категория обновлена");
      } else {
        await createCatalogCategory({
          name: values.name,
          parent_id: catParentId,
          sort_order: values.sort_order,
        });
        message.success("Категория создана");
      }
      setCatModalVisible(false);
      loadCategories();
    } catch (e: any) {
      if (e?.errorFields) return;
      message.error(e?.response?.data?.detail || "Ошибка сохранения категории");
    } finally {
      setCatSubmitting(false);
    }
  };

  // --- Item Actions ---
  const handleAddItem = async () => {
    setEditingItem(null);
    itemForm.resetFields();

    try {
      const tspRes = await getFreeCatalogTsp();
      const codes = tspRes.data.map((c) => c.tsp_code);
      setFreeTspCodes(codes);

      itemForm.setFieldsValue({
        category_id: selectedCatId || (flatCategories.length > 0 ? flatCategories[0].id : undefined),
        tsp_code: codes[0] || 1000301,
        name: "",
        printname: "",
        price: 0,
        protypenumber: 0,
      });
      setItemModalVisible(true);
    } catch (e: any) {
      message.error("Ошибка получения свободных кодов ТСП");
    }
  };

  const handleEditItem = (item: CatalogItem) => {
    setEditingItem(item);
    setFreeTspCodes([item.tsp_code]);
    itemForm.resetFields();
    itemForm.setFieldsValue({
      category_id: item.category_id,
      tsp_code: item.tsp_code,
      name: item.name,
      printname: item.printname || "",
      price: item.price,
      protypenumber: item.protypenumber,
    });
    setItemModalVisible(true);
  };

  const handleDeleteItem = async (itemId: number) => {
    try {
      await deleteCatalogItem(itemId);
      message.success("Услуга удалена из каталога");
      loadCategories();
      loadItems(selectedCatId, searchText);
    } catch (e: any) {
      message.error(e?.response?.data?.detail || "Ошибка удаления услуги");
    }
  };

  const handleSaveItem = async () => {
    try {
      const values = await itemForm.validateFields();
      setItemSubmitting(true);
      if (editingItem) {
        await updateCatalogItem(editingItem.id, {
          category_id: values.category_id,
          name: values.name,
          printname: values.printname || null,
          price: values.price,
          protypenumber: values.protypenumber,
        });
        message.success("Услуга каталога обновлена");
      } else {
        await createCatalogItem({
          category_id: values.category_id,
          tsp_code: values.tsp_code,
          name: values.name,
          printname: values.printname || null,
          price: values.price,
          protypenumber: values.protypenumber,
        });
        message.success("Услуга добавлена в каталог");
      }
      setItemModalVisible(false);
      loadCategories();
      loadItems(selectedCatId, searchText);
    } catch (e: any) {
      if (e?.errorFields) return;
      message.error(e?.response?.data?.detail || "Ошибка сохранения услуги");
    } finally {
      setItemSubmitting(false);
    }
  };

  // --- Propagate changes to menus ---
  const handlePropagate = async () => {
    setPropagateLoading(true);
    try {
      const res = await propagateCatalogToMenus();
      const { updated_variants_count, updated_services_count, affected_variant_names } = res.data;
      if (updated_variants_count === 0) {
        message.info("Все связанные меню уже актуальны, обновлений не требуется.");
      } else {
        Modal.success({
          title: "Изменения каталога применены!",
          content: (
            <div>
              <p>
                Успешно обновлено <strong>{updated_services_count}</strong> услуг в{" "}
                <strong>{updated_variants_count}</strong> вариантах меню:
              </p>
              <ul>
                {affected_variant_names.map((name, i) => (
                  <li key={i}>{name}</li>
                ))}
              </ul>
              <p style={{ color: "#fa8c16" }}>
                На сервере созданы новые версии меню. При необходимости обновите меню на
                терминалах.
              </p>
            </div>
          ),
        });
      }
    } catch (e: any) {
      message.error(e?.response?.data?.detail || "Ошибка применения изменений каталога");
    } finally {
      setPropagateLoading(false);
    }
  };

  const selectedCategoryObj = selectedCatId ? findCategory(selectedCatId) : null;

  const itemColumns: ColumnsType<CatalogItem> = [
    {
      title: "Код ТСП",
      dataIndex: "tsp_code",
      key: "tsp_code",
      width: 110,
      render: (code: number) => (
        <Tag color="cyan" style={{ fontFamily: "monospace", fontSize: 13 }}>
          {code}
        </Tag>
      ),
      sorter: (a, b) => a.tsp_code - b.tsp_code,
    },
    {
      title: "Название",
      dataIndex: "name",
      key: "name",
      ellipsis: true,
      render: (name: string) => <span style={{ fontWeight: 500 }}>{name}</span>,
    },
    {
      title: "Печатное наименование",
      dataIndex: "printname",
      key: "printname",
      ellipsis: true,
      render: (p: string | null) => p || <Text type="secondary">—</Text>,
    },
    {
      title: "Цена",
      dataIndex: "price",
      key: "price",
      width: 110,
      align: "right",
      render: (price: number) => (price > 0 ? `${price} ₽` : "0 ₽"),
      sorter: (a, b) => a.price - b.price,
    },
    {
      title: "Прототип",
      dataIndex: "protypenumber",
      key: "protypenumber",
      width: 100,
      align: "center",
      render: (p: number) => (
        <span style={{ fontFamily: "monospace", color: "#8c8c8c" }}>{p}</span>
      ),
    },
    {
      title: "Раздел каталога",
      dataIndex: "category_name",
      key: "category_name",
      ellipsis: true,
      render: (catName: string | null) =>
        catName ? <Tag>{catName}</Tag> : <Text type="secondary">—</Text>,
    },
    {
      title: "Действия",
      key: "actions",
      width: 120,
      align: "center",
      render: (_, record) => (
        <Space size="small">
          <Button
            type="text"
            icon={<EditOutlined />}
            size="small"
            onClick={() => handleEditItem(record)}
          />
          <Popconfirm
            title="Удалить услугу из каталога?"
            okText="Да, удалить"
            cancelText="Отмена"
            onConfirm={() => handleDeleteItem(record.id)}
          >
            <Button type="text" danger icon={<DeleteOutlined />} size="small" />
          </Popconfirm>
        </Space>
      ),
    },
  ];

  return (
    <div style={{ padding: "0 0 24px 0" }}>
      {/* Top Header Card */}
      <Card style={{ marginBottom: 16 }}>
        <Row justify="space-between" align="middle" gutter={[16, 16]}>
          <Col>
            <Title level={4} style={{ margin: 0 }}>
              Каталог предустановленных услуг
            </Title>
            <Text type="secondary">
              Единый корпоративный реестр типовых услуг организации (диапазон ТСП 1000301..1000999)
            </Text>
          </Col>
          <Col>
            <Space wrap>
              <Button
                icon={<ReloadOutlined />}
                onClick={() => {
                  loadCategories();
                  loadItems(selectedCatId, searchText);
                }}
              >
                Обновить
              </Button>
              <Popconfirm
                title="Применить изменения каталога в связанных меню?"
                description={
                  <div style={{ maxWidth: 360 }}>
                    Для всех вариантов меню, содержащих измененные элементы каталога, будут
                    созданы <strong>новые версии на сервере</strong>.
                    <br />
                    <span style={{ color: "#ff4d4f", fontWeight: 600 }}>
                      Внимание: операция не может быть отменена!
                    </span>
                  </div>
                }
                okText="Применить изменения"
                cancelText="Отмена"
                okButtonProps={{ danger: true, loading: propagateLoading }}
                onConfirm={handlePropagate}
              >
                <Button
                  type="primary"
                  danger
                  icon={<SyncOutlined spin={propagateLoading} />}
                >
                  Обновить изменения каталога в связанных меню
                </Button>
              </Popconfirm>
              <Button
                type="primary"
                icon={<PlusOutlined />}
                onClick={handleAddItem}
                disabled={flatCategories.length === 0}
              >
                + Добавить услугу в каталог
              </Button>
            </Space>
          </Col>
        </Row>
      </Card>

      <Row gutter={16}>
        {/* Left Column: Categories Tree */}
        <Col xs={24} md={8} lg={7}>
          <Card
            title={
              <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
                <span>Структура категорий</span>
                <Button
                  type="link"
                  size="small"
                  icon={<FolderAddOutlined />}
                  onClick={handleAddRootCategory}
                >
                  + Раздел
                </Button>
              </div>
            }
            loading={loading}
            style={{ height: "100%", minHeight: 520 }}
          >
            {categories.length === 0 ? (
              <Empty
                description="Нет категорий в каталоге"
                style={{ margin: "40px 0" }}
              >
                <Button
                  type="primary"
                  icon={<FolderAddOutlined />}
                  onClick={handleAddRootCategory}
                >
                  Создать первый раздел
                </Button>
              </Empty>
            ) : (
              <div>
                <Tree
                  showLine={{ showLeafIcon: false }}
                  defaultExpandAll
                  selectedKeys={selectedCatId ? [String(selectedCatId)] : ["all"]}
                  onSelect={(keys) => {
                    const key = keys[0];
                    if (!key || key === "all") {
                      setSelectedCatId(null);
                    } else {
                      setSelectedCatId(Number(key));
                    }
                  }}
                  treeData={treeData}
                />

                {selectedCategoryObj && (
                  <div
                    style={{
                      marginTop: 20,
                      padding: "12px",
                      background: "#fafafa",
                      borderRadius: 6,
                      border: "1px solid #f0f0f0",
                    }}
                  >
                    <div style={{ fontWeight: 600, marginBottom: 8 }}>
                      Управление: {selectedCategoryObj.name}
                    </div>
                    <Space wrap size="small">
                      <Button
                        size="small"
                        icon={<PlusOutlined />}
                        disabled={selectedCategoryObj.depth >= 3}
                        onClick={() => handleAddSubCategory(selectedCategoryObj)}
                      >
                        + Подраздел
                      </Button>
                      <Button
                        size="small"
                        icon={<EditOutlined />}
                        onClick={() => handleEditCategory(selectedCategoryObj)}
                      >
                        Изменить
                      </Button>
                      <Popconfirm
                        title="Удалить этот раздел каталога со всеми подразделами и услугами?"
                        okText="Да, удалить"
                        cancelText="Отмена"
                        onConfirm={() => handleDeleteCategory(selectedCategoryObj.id)}
                      >
                        <Button size="small" danger icon={<DeleteOutlined />}>
                          Удалить
                        </Button>
                      </Popconfirm>
                    </Space>
                    {selectedCategoryObj.depth >= 3 && (
                      <div style={{ marginTop: 6, fontSize: 11, color: "#8c8c8c" }}>
                        Достигнут лимит вложенности (3 уровня)
                      </div>
                    )}
                  </div>
                )}
              </div>
            )}
          </Card>
        </Col>

        {/* Right Column: Items Table */}
        <Col xs={24} md={16} lg={17}>
          <Card
            title={
              <Row justify="space-between" align="middle" gutter={16}>
                <Col>
                  <span>
                    {selectedCategoryObj
                      ? `Услуги раздела: ${selectedCategoryObj.name}`
                      : "Все услуги каталога"}
                  </span>
                  <Tag style={{ marginLeft: 8 }}>{items.length}</Tag>
                </Col>
                <Col xs={24} sm={12} md={10} style={{ marginTop: isMobile ? 8 : 0 }}>
                  <Input
                    placeholder="Поиск по названию или ТСП..."
                    prefix={<SearchOutlined />}
                    value={searchText}
                    onChange={(e) => setSearchText(e.target.value)}
                    allowClear
                    style={{ width: "100%" }}
                  />
                </Col>
              </Row>
            }
          >
            <Table
              className="compact-table"
              dataSource={items}
              columns={itemColumns}
              rowKey="id"
              loading={itemsLoading}
              scroll={{ x: 700 }}
              pagination={{
                defaultPageSize: 15,
                showSizeChanger: true,
                pageSizeOptions: ["10", "15", "30", "50"],
                size: "small",
                simple: isMobile,
              }}
              size="small"
            />
          </Card>
        </Col>
      </Row>

      {/* Modal: Category Create/Edit */}
      <Modal
        title={
          editingCategory
            ? "Редактирование раздела каталога"
            : catParentId
            ? "Добавление подраздела"
            : "Создание корневого раздела"
        }
        open={catModalVisible}
        onOk={handleSaveCategory}
        onCancel={() => setCatModalVisible(false)}
        confirmLoading={catSubmitting}
        okText="Сохранить"
        cancelText="Отмена"
        style={{ maxWidth: "calc(100vw - 16px)" }}
      >
        <Form form={catForm} layout="vertical">
          <Form.Item
            name="name"
            label="Название раздела"
            rules={[{ required: true, message: "Введите название раздела" }]}
          >
            <Input placeholder="Например: Шиномонтаж или Экспресс-услуги" />
          </Form.Item>
          <Form.Item name="sort_order" label="Порядок сортировки">
            <InputNumber min={0} style={{ width: "100%" }} />
          </Form.Item>
        </Form>
      </Modal>

      {/* Modal: Item Create/Edit */}
      <Modal
        title={editingItem ? "Редактирование услуги каталога" : "Добавление услуги в каталог"}
        open={itemModalVisible}
        onOk={handleSaveItem}
        onCancel={() => setItemModalVisible(false)}
        confirmLoading={itemSubmitting}
        okText="Сохранить"
        cancelText="Отмена"
        width={560}
        style={{ maxWidth: "calc(100vw - 16px)" }}
      >
        <Form form={itemForm} layout="vertical">
          <Form.Item
            name="category_id"
            label="Раздел каталога"
            rules={[{ required: true, message: "Выберите раздел каталога" }]}
          >
            <Select placeholder="Выберите категорию">
              {flatCategories.map((c) => (
                <Select.Option key={c.id} value={c.id}>
                  {"— ".repeat(c.depth - 1) + c.name}
                </Select.Option>
              ))}
            </Select>
          </Form.Item>

          <Row gutter={16}>
            <Col span={12}>
              <Form.Item
                name="tsp_code"
                label="Код ТСП (1000301..1000999)"
                rules={[{ required: true, message: "Выберите или введите код ТСП" }]}
                extra="Диапазон кодов каталога"
              >
                {editingItem ? (
                  <InputNumber disabled style={{ width: "100%" }} />
                ) : (
                  <Select
                    showSearch
                    placeholder="Выберите свободный код"
                    options={freeTspCodes.map((code) => ({
                      value: code,
                      label: String(code),
                    }))}
                  />
                )}
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="price" label="Цена (руб.)">
                <InputNumber min={0} style={{ width: "100%" }} />
              </Form.Item>
            </Col>
          </Row>

          <Form.Item
            name="name"
            label="Наименование услуги"
            rules={[{ required: true, message: "Введите наименование услуги" }]}
          >
            <Input placeholder="Например: Мойка кузова и ковриков" />
          </Form.Item>

          <Form.Item name="printname" label="Печатное наименование (для чека)">
            <Input placeholder="Если не заполнено, совпадает с основным" />
          </Form.Item>

          <Form.Item name="protypenumber" label="Код прототипа">
            <InputNumber min={0} style={{ width: "100%" }} />
          </Form.Item>
        </Form>
      </Modal>
    </div>
  );
}
