import { useEffect, useState, useCallback, useMemo } from "react";
import {
  Button,
  Card,
  Col,
  Empty,
  Input,
  InputNumber,
  message,
  Modal,
  Popconfirm,
  Row,
  Space,
  Spin,
  Table,
  Tag,
  Tree,
  Typography,
  Grid,
  Segmented,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import {
  PlusOutlined,
  CopyOutlined,
  DeleteOutlined,
  EditOutlined,
  FolderOutlined,
  FolderOpenOutlined,
  FolderAddOutlined,
  AppstoreOutlined,
  UnorderedListOutlined,
  SearchOutlined,
} from "@ant-design/icons";
import type { DataNode, TreeProps } from "antd/es/tree";
import {
  getMenuVariants,
  createMenuVariant,
  duplicateMenuVariant,
  deleteMenuVariant,
  MenuVariant,
} from "../api/menu-variants";
import {
  getGroups,
  createGroup,
  updateGroup,
  deleteGroup,
  Group,
  GroupCreate,
} from "../api/groups";
import {
  getServices,
  createService,
  updateService,
  deleteService,
  Service,
  ServiceCreate,
} from "../api/services";
import ServiceForm from "../components/ServiceForm";

const { Text } = Typography;
const { useBreakpoint } = Grid;

function buildTree(groups: Group[], parentId: number | null = null): DataNode[] {
  return groups
    .filter((g) => g.parent_id === parentId)
    .sort((a, b) => a.number - b.number)
    .map((g) => ({
      key: g.id,
      title: (
        <span style={{ fontSize: 13 }}>
          <Text strong style={{ fontSize: 12, marginRight: 4 }}>{g.number}.</Text> {g.name}
        </span>
      ),
      icon: <FolderOutlined style={{ fontSize: 14 }} />,
      children: buildTree(groups, g.id),
    }));
}

export default function VariantsPage() {
  const screens = useBreakpoint();
  const isMobile = !screens.md;
  const [mobileTab, setMobileTab] = useState<"variants" | "groups" | "services">("variants");

  // Left panel: variants
  const [variants, setVariants] = useState<MenuVariant[]>([]);
  const [selectedVariantId, setSelectedVariantId] = useState<number | null>(null);

  // Middle panel: groups
  const [groups, setGroups] = useState<Group[]>([]);
  const [groupsLoading, setGroupsLoading] = useState(false);
  const [selectedGroupId, setSelectedGroupId] = useState<number | null>(null);
  const [groupSearch, setGroupSearch] = useState("");

  // Right panel: services
  const [services, setServices] = useState<Service[]>([]);
  const [servicesLoading, setServicesLoading] = useState(false);
  const [serviceSearchText, setServiceSearchText] = useState("");
  const [serviceFormOpen, setServiceFormOpen] = useState(false);
  const [editService, setEditService] = useState<Service | null>(null);

  // Group form modal
  const [groupFormOpen, setGroupFormOpen] = useState(false);
  const [editGroup, setEditGroup] = useState<Group | null>(null);
  const [groupFormName, setGroupFormName] = useState("");
  const [groupFormNumber, setGroupFormNumber] = useState(0);
  const [groupFormParentId, setGroupFormParentId] = useState<number | null>(null);
  const [groupFormSaving, setGroupFormSaving] = useState(false);

  const loadVariants = useCallback(async () => {
    try {
      const res = await getMenuVariants();
      setVariants(res.data);
      if (res.data.length > 0 && selectedVariantId === null) {
        setSelectedVariantId(res.data[0].id);
      }
    } catch { message.error("Ошибка загрузки вариантов"); }
  }, [selectedVariantId]);

  const loadGroups = useCallback(async () => {
    if (!selectedVariantId) { setGroups([]); return; }
    setGroupsLoading(true);
    try {
      const res = await getGroups(selectedVariantId);
      setGroups(res.data);
    } catch { message.error("Ошибка загрузки групп"); }
    finally { setGroupsLoading(false); }
  }, [selectedVariantId]);

  const loadServices = useCallback(async () => {
    if (!selectedGroupId) { setServices([]); return; }
    setServicesLoading(true);
    try {
      const res = await getServices(selectedGroupId);
      setServices(res.data);
    } catch { message.error("Ошибка загрузки услуг"); }
    finally { setServicesLoading(false); }
  }, [selectedGroupId]);

  useEffect(() => { loadVariants(); }, []);
  useEffect(() => { loadGroups(); setSelectedGroupId(null); }, [selectedVariantId]);
  useEffect(() => { loadServices(); }, [selectedGroupId]);

  // --- Variant actions ---
  const handleCreateVariant = () => {
    Modal.confirm({
      title: "Новый вариант",
      content: <Input id="nv-name" placeholder="Название" autoFocus />,
      onOk: async () => {
        const name = (document.getElementById("nv-name") as HTMLInputElement)?.value?.trim();
        if (!name) { message.warning("Введите название"); throw new Error(); }
        try {
          const res = await createMenuVariant({ name });
          message.success("Создано");
          setSelectedVariantId(res.data.id);
          loadVariants();
        } catch (e: any) { message.error(e.message); }
      },
    });
  };

  const handleDuplicate = async () => {
    if (!selectedVariantId) return;
    const src = variants.find((v) => v.id === selectedVariantId);
    if (!src) return;
    Modal.confirm({
      title: `Копировать «${src.name}»`,
      content: <Input id="dv-name" placeholder="Название (авто если пусто)" />,
      onOk: async () => {
        const name = (document.getElementById("dv-name") as HTMLInputElement)?.value?.trim() || undefined;
        try {
          const res = await duplicateMenuVariant({ source_variant_id: selectedVariantId, new_name: name });
          message.success(`Создано «${res.data.name}»`);
          setSelectedVariantId(res.data.id);
          loadVariants();
        } catch (e: any) { message.error(e.message); }
      },
    });
  };

  const handleDeleteVariant = async () => {
    if (!selectedVariantId) return;
    try {
      await deleteMenuVariant(selectedVariantId);
      message.success("Удалено");
      setSelectedVariantId(null);
      loadVariants();
    } catch (e: any) { message.error(e.message); }
  };

  // --- Group actions ---
  const openGroupForm = (group?: Group, parentId?: number | null) => {
    setEditGroup(group || null);
    setGroupFormName(group?.name || "");
    setGroupFormNumber(group?.number || 0);
    setGroupFormParentId(group?.parent_id ?? parentId ?? null);
    setGroupFormOpen(true);
  };

  const handleSaveGroup = async () => {
    if (!groupFormName.trim()) { message.warning("Введите название"); return; }
    setGroupFormSaving(true);
    try {
      if (editGroup) {
        await updateGroup(editGroup.id, { name: groupFormName, number: groupFormNumber, parent_id: groupFormParentId });
        message.success("Обновлено");
      } else {
        if (!selectedVariantId) return;
        const data: GroupCreate = {
          menu_variant_id: selectedVariantId,
          name: groupFormName,
          number: groupFormNumber || 0,
          parent_id: groupFormParentId,
        };
        await createGroup(data);
        message.success("Создано");
      }
      setGroupFormOpen(false);
      loadGroups();
    } catch (e: any) { message.error(e.message); }
    finally { setGroupFormSaving(false); }
  };

  const handleDeleteGroup = async (id: number) => {
    try {
      await deleteGroup(id);
      message.success("Удалено");
      if (selectedGroupId === id) setSelectedGroupId(null);
      loadGroups();
    } catch (e: any) { message.error(e.message); }
  };

  const onDrop: TreeProps["onDrop"] = async (info) => {
    const dragId = info.dragNode.key as number;
    const dropId = info.node.key as number;
    try {
      await updateGroup(dragId, { parent_id: info.dropToGap ? null : dropId });
      message.success("Перемещено");
      loadGroups();
    } catch (e: any) { message.error(e.message); }
  };

  // --- Service actions ---
  const handleDeleteService = async (id: number) => {
    try {
      await deleteService(id);
      message.success("Удалено");
      loadServices();
    } catch (e: any) { message.error(e.message); }
  };

  const selectedVariant = variants.find((v) => v.id === selectedVariantId);
  const selectedGroup = groups.find((g) => g.id === selectedGroupId);

  const treeData = buildTree(
    groupSearch
      ? groups.filter((g) => g.name.toLowerCase().includes(groupSearch.toLowerCase()))
      : groups
  );

  const filteredServices = useMemo(() => {
    if (!serviceSearchText.trim()) return services;
    const q = serviceSearchText.toLowerCase();
    return services.filter(
      (s) =>
        s.name.toLowerCase().includes(q) ||
        (s.printname && s.printname.toLowerCase().includes(q)) ||
        String(s.tsp_code).includes(q)
    );
  }, [services, serviceSearchText]);

  const serviceColumns: ColumnsType<Service> = [
    {
      title: "ТСП",
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
      sorter: (a, b) => (a.price ?? 0) - (b.price ?? 0),
    },
    {
      title: "Прототип",
      dataIndex: "protypenumber",
      key: "protypenumber",
      width: 100,
      align: "center",
      render: (p: number) => (
        <span style={{ fontFamily: "monospace", color: "#8c8c8c" }}>
          {p !== undefined && p !== null ? p : "—"}
        </span>
      ),
      sorter: (a, b) => (a.protypenumber ?? 0) - (b.protypenumber ?? 0),
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
            onClick={() => {
              setEditService(record);
              setServiceFormOpen(true);
            }}
          />
          <Popconfirm
            title="Удалить услугу?"
            okText="Да, удалить"
            cancelText="Отмена"
            onConfirm={() => handleDeleteService(record.id)}
          >
            <Button type="text" danger icon={<DeleteOutlined />} size="small" />
          </Popconfirm>
        </Space>
      ),
    },
  ];

  const variantsCard = (
    <Card
      style={{
        width: isMobile ? "100%" : 210,
        flexShrink: 0,
        overflow: "auto",
        height: isMobile ? "calc(100vh - 160px)" : "100%",
        minHeight: 520,
      }}
      title={<span style={{ fontWeight: 600 }}>Варианты меню</span>}
      extra={
        <Space size={0}>
          <Button type="text" size="small" icon={<PlusOutlined />} onClick={handleCreateVariant} title="Создать вариант" />
          <Button type="text" size="small" icon={<CopyOutlined />} onClick={handleDuplicate} disabled={!selectedVariantId} title="Копировать вариант" />
          <Popconfirm title="Удалить вариант меню?" onConfirm={handleDeleteVariant} okText="Да, удалить" cancelText="Отмена" disabled={!selectedVariantId}>
            <Button type="text" size="small" danger icon={<DeleteOutlined />} disabled={!selectedVariantId} title="Удалить вариант" />
          </Popconfirm>
        </Space>
      }
    >
      {variants.length === 0 ? (
        <Empty description="Нет вариантов" image={Empty.PRESENTED_IMAGE_SIMPLE} />
      ) : (
        <div style={{ display: "flex", flexDirection: "column", gap: 4 }}>
          {variants.map((v) => (
            <div
              key={v.id}
              onClick={() => {
                setSelectedVariantId(v.id);
                if (isMobile) setMobileTab("groups");
              }}
              style={{
                padding: "8px 10px",
                borderRadius: 6,
                cursor: "pointer",
                background: v.id === selectedVariantId ? "#e6f4ff" : "transparent",
                border: v.id === selectedVariantId ? "1px solid #91caff" : "1px solid #f0f0f0",
                fontSize: 13,
                fontWeight: v.id === selectedVariantId ? 600 : 400,
                display: "flex",
                alignItems: "center",
                justifyContent: "space-between",
                gap: 6,
                transition: "all 0.2s",
              }}
            >
              <span
                style={{
                  whiteSpace: "nowrap",
                  overflow: "hidden",
                  textOverflow: "ellipsis",
                  flex: 1,
                }}
              >
                {v.name}
              </span>
              <Tag
                color="geekblue"
                style={{
                  margin: 0,
                  fontSize: 10,
                  padding: "0 4px",
                  lineHeight: "16px",
                }}
              >
                v{v.version ?? 1}
              </Tag>
            </div>
          ))}
        </div>
      )}
    </Card>
  );

  const groupsCard = (
    <Card
      style={{
        width: isMobile ? "100%" : 300,
        flexShrink: 0,
        overflow: "auto",
        height: isMobile ? "calc(100vh - 160px)" : "100%",
        minHeight: 520,
      }}
      title={
        <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
          <span>Структура групп</span>
          <Button
            type="link"
            size="small"
            icon={<FolderAddOutlined />}
            onClick={() => openGroupForm()}
            disabled={!selectedVariantId}
          >
            + Группа
          </Button>
        </div>
      }
      loading={groupsLoading}
    >
      {!selectedVariantId ? (
        <Empty description="Выберите вариант меню" image={Empty.PRESENTED_IMAGE_SIMPLE} />
      ) : (
        <>
          <Input
            placeholder="Поиск по группам..."
            prefix={<SearchOutlined />}
            size="small"
            allowClear
            value={groupSearch}
            onChange={(e) => setGroupSearch(e.target.value)}
            style={{ marginBottom: 12 }}
          />
          {treeData.length === 0 ? (
            <Empty description="Нет групп в меню" image={Empty.PRESENTED_IMAGE_SIMPLE}>
              <Button
                type="primary"
                size="small"
                icon={<FolderAddOutlined />}
                onClick={() => openGroupForm()}
              >
                Создать первую группу
              </Button>
            </Empty>
          ) : (
            <div>
              <Tree
                showLine={{ showLeafIcon: false }}
                showIcon
                draggable
                defaultExpandAll
                treeData={treeData}
                selectedKeys={selectedGroupId ? [selectedGroupId] : []}
                onSelect={(keys) => {
                  const nextKey = (keys[0] as number) || null;
                  setSelectedGroupId(nextKey);
                  if (isMobile && nextKey) setMobileTab("services");
                }}
                onDrop={onDrop}
              />

              {selectedGroup && (
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
                    Управление: {selectedGroup.name}
                  </div>
                  <Space wrap size="small">
                    <Button
                      size="small"
                      icon={<PlusOutlined />}
                      onClick={() => openGroupForm(undefined, selectedGroup.id)}
                    >
                      + Подгруппа
                    </Button>
                    <Button
                      size="small"
                      icon={<EditOutlined />}
                      onClick={() => openGroupForm(selectedGroup)}
                    >
                      Изменить
                    </Button>
                    <Popconfirm
                      title="Удалить эту группу со всеми подгруппами и услугами?"
                      okText="Да, удалить"
                      cancelText="Отмена"
                      onConfirm={() => handleDeleteGroup(selectedGroup.id)}
                    >
                      <Button size="small" danger icon={<DeleteOutlined />}>
                        Удалить
                      </Button>
                    </Popconfirm>
                  </Space>
                </div>
              )}
            </div>
          )}
        </>
      )}
    </Card>
  );

  const servicesCard = (
    <Card
      style={{
        flex: 1,
        width: isMobile ? "100%" : "auto",
        overflow: "auto",
        height: isMobile ? "calc(100vh - 160px)" : "100%",
        minHeight: 520,
      }}
      title={
        <Row justify="space-between" align="middle" gutter={16}>
          <Col>
            <span>
              {selectedGroup
                ? `Услуги группы: ${selectedGroup.name}`
                : "Услуги"}
            </span>
            <Tag style={{ marginLeft: 8 }}>{filteredServices.length}</Tag>
          </Col>
          <Col xs={24} sm={12} md={10} style={{ marginTop: isMobile ? 8 : 0 }}>
            <Input
              placeholder="Поиск по названию или ТСП..."
              prefix={<SearchOutlined />}
              value={serviceSearchText}
              onChange={(e) => setServiceSearchText(e.target.value)}
              allowClear
              style={{ width: "100%" }}
            />
          </Col>
        </Row>
      }
      extra={
        <Button
          type="primary"
          icon={<PlusOutlined />}
          onClick={() => {
            setEditService(null);
            setServiceFormOpen(true);
          }}
          disabled={!selectedGroupId}
        >
          + Добавить услугу
        </Button>
      }
    >
      <Table
        className="compact-table"
        dataSource={filteredServices}
        columns={serviceColumns}
        rowKey="id"
        loading={servicesLoading}
        size="small"
        scroll={{ x: 700 }}
        pagination={{
          defaultPageSize: 15,
          showSizeChanger: true,
          pageSizeOptions: ["10", "15", "30", "50"],
          size: "small",
          simple: isMobile,
        }}
        locale={{
          emptyText: !selectedGroupId ? "Выберите группу для просмотра услуг" : "Нет услуг в группе",
        }}
      />
    </Card>
  );

  return (
    <div>
      {isMobile && (
        <div style={{ marginBottom: 8 }}>
          <Segmented
            block
            size="small"
            value={mobileTab}
            onChange={(val) => setMobileTab(val as any)}
            options={[
              {
                value: "variants",
                label: (
                  <span style={{ fontSize: 11, fontWeight: 500 }}>
                    <AppstoreOutlined /> Варианты {selectedVariant ? `(${selectedVariant.name.slice(0, 10)})` : ""}
                  </span>
                ),
              },
              {
                value: "groups",
                label: (
                  <span style={{ fontSize: 11, fontWeight: 500 }}>
                    <FolderOutlined /> Группы {selectedGroup ? `(${selectedGroup.name.slice(0, 10)})` : ""}
                  </span>
                ),
              },
              {
                value: "services",
                label: (
                  <span style={{ fontSize: 11, fontWeight: 500 }}>
                    <UnorderedListOutlined /> Услуги ({services.length})
                  </span>
                ),
              },
            ]}
          />
        </div>
      )}

      {isMobile ? (
        <div>
          {mobileTab === "variants" && variantsCard}
          {mobileTab === "groups" && groupsCard}
          {mobileTab === "services" && servicesCard}
        </div>
      ) : (
        <div style={{ display: "flex", gap: 16, height: "calc(100vh - 80px)", minHeight: 560 }}>
          {variantsCard}
          {groupsCard}
          {servicesCard}
        </div>
      )}

      {/* Group form modal */}
      <Modal
        title={editGroup ? "Редактировать группу" : "Новая группа"}
        open={groupFormOpen}
        onCancel={() => setGroupFormOpen(false)}
        onOk={handleSaveGroup}
        confirmLoading={groupFormSaving}
        width={340}
        style={{ maxWidth: "calc(100vw - 16px)" }}
        okText="Сохранить"
        cancelText="Отмена"
      >
        <div style={{ display: "flex", flexDirection: "column", gap: 8, marginTop: 8 }}>
          <div>
            <Text type="secondary" style={{ fontSize: 11 }}>Название</Text>
            <Input
              value={groupFormName}
              onChange={(e) => setGroupFormName(e.target.value)}
              size="small"
              autoFocus
            />
          </div>
          <div>
            <Text type="secondary" style={{ fontSize: 11 }}>Номер</Text>
            <InputNumber
              value={groupFormNumber || undefined}
              onChange={(v) => setGroupFormNumber(v || 0)}
              placeholder="Авто (от 801)"
              size="small"
              style={{ width: "100%" }}
              min={0}
            />
          </div>
          <div>
            <Text type="secondary" style={{ fontSize: 11 }}>Родитель</Text>
            <Input
              value={groupFormParentId ? groups.find((g) => g.id === groupFormParentId)?.name || "" : "— Корневая"}
              disabled
              size="small"
            />
          </div>
        </div>
      </Modal>

      {/* Service form drawer */}
      <ServiceForm
        open={serviceFormOpen}
        service={editService}
        groupId={selectedGroupId || 0}
        onClose={() => setServiceFormOpen(false)}
        onSaved={() => { setServiceFormOpen(false); loadServices(); }}
      />
    </div>
  );
}
