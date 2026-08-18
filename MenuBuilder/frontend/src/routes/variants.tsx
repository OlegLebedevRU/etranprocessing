import { useEffect, useState, useCallback } from "react";
import {
  Button,
  Card,
  Empty,
  Input,
  InputNumber,
  message,
  Modal,
  Popconfirm,
  Space,
  Spin,
  Table,
  Tag,
  Tree,
  Typography,
} from "antd";
import {
  PlusOutlined,
  CopyOutlined,
  DeleteOutlined,
  EditOutlined,
  FolderOutlined,
  FolderOpenOutlined,
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

function buildTree(groups: Group[], parentId: number | null = null): DataNode[] {
  return groups
    .filter((g) => g.parent_id === parentId)
    .sort((a, b) => a.number - b.number)
    .map((g) => ({
      key: g.id,
      title: (
        <span style={{ fontSize: 12 }}>
          <Text strong style={{ fontSize: 11 }}>{g.number}.</Text> {g.name}
        </span>
      ),
      icon: ({ expanded }: { expanded?: boolean }) =>
        expanded ? <FolderOpenOutlined style={{ fontSize: 13 }} /> : <FolderOutlined style={{ fontSize: 13 }} />,
      children: buildTree(groups, g.id),
    }));
}

export default function VariantsPage() {
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
          org_id: 1,
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

  const selectedGroup = groups.find((g) => g.id === selectedGroupId);

  const treeData = buildTree(
    groupSearch
      ? groups.filter((g) => g.name.toLowerCase().includes(groupSearch.toLowerCase()))
      : groups
  );

  const serviceColumns = [
    {
      title: "TSP",
      dataIndex: "tsp_code",
      key: "tsp_code",
      render: (v: number) => <Tag color="blue" style={{ margin: 0, fontSize: 10 }}>{v}</Tag>,
    },
    {
      title: "Название для кнопки",
      dataIndex: "name",
      key: "name",
      ellipsis: true,
      render: (v: string) => <Text style={{ fontSize: 12 }}>{v}</Text>,
    },
    {
      title: "Наименование для чека",
      dataIndex: "printname",
      key: "printname",
      ellipsis: true,
      render: (v: string | null) => v ? <Text style={{ fontSize: 12 }}>{v}</Text> : <Text type="secondary" style={{ fontSize: 11 }}>—</Text>,
    },
    {
      title: "₽",
      dataIndex: "price",
      key: "price",
      align: "right" as const,
      render: (v: number) => v ? <Text style={{ fontSize: 12 }}>{v}</Text> : <Text type="secondary" style={{ fontSize: 11 }}>—</Text>,
    },
    {
      title: "",
      key: "actions",
      render: (_: any, record: Service) => (
        <Space size={0}>
          <Button type="text" size="small" icon={<EditOutlined />} onClick={() => { setEditService(record); setServiceFormOpen(true); }} />
          <Popconfirm title="Удалить?" onConfirm={() => handleDeleteService(record.id)} okText="Да" cancelText="Нет">
            <Button type="text" size="small" danger icon={<DeleteOutlined />} />
          </Popconfirm>
        </Space>
      ),
    },
  ];

  return (
    <div style={{ display: "flex", gap: 8, height: "calc(100vh - 80px)" }}>
      {/* LEFT: Variants */}
      <Card
        size="small"
        style={{ width: 180, flexShrink: 0, overflow: "auto" }}
        styles={{ body: { padding: "8px 6px" } }}
        title={<Text strong style={{ fontSize: 12 }}>Варианты меню</Text>}
        extra={
          <Space size={0}>
            <Button type="text" size="small" icon={<PlusOutlined />} onClick={handleCreateVariant} />
            <Button type="text" size="small" icon={<CopyOutlined />} onClick={handleDuplicate} disabled={!selectedVariantId} />
            <Popconfirm title="Удалить?" onConfirm={handleDeleteVariant} okText="Да" cancelText="Нет" disabled={!selectedVariantId}>
              <Button type="text" size="small" danger icon={<DeleteOutlined />} disabled={!selectedVariantId} />
            </Popconfirm>
          </Space>
        }
      >
        {variants.length === 0 ? (
          <Empty description="Нет" image={Empty.PRESENTED_IMAGE_SIMPLE} />
        ) : (
          <div style={{ display: "flex", flexDirection: "column", gap: 2 }}>
            {variants.map((v) => (
              <div
                key={v.id}
                onClick={() => setSelectedVariantId(v.id)}
                style={{
                  padding: "4px 8px",
                  borderRadius: 4,
                  cursor: "pointer",
                  background: v.id === selectedVariantId ? "#e6f4ff" : "transparent",
                  border: v.id === selectedVariantId ? "1px solid #91caff" : "1px solid transparent",
                  fontSize: 12,
                  fontWeight: v.id === selectedVariantId ? 600 : 400,
                  whiteSpace: "nowrap",
                  overflow: "hidden",
                  textOverflow: "ellipsis",
                }}
              >
                {v.name}
              </div>
            ))}
          </div>
        )}
      </Card>

      {/* MIDDLE: Groups tree */}
      <Card
        size="small"
        style={{ width: 260, flexShrink: 0, overflow: "auto" }}
        styles={{ body: { padding: "4px 6px" } }}
        title={<Text strong style={{ fontSize: 12 }}>Группы</Text>}
        extra={
          <Space size={0}>
            <Button type="text" size="small" icon={<PlusOutlined />} onClick={() => openGroupForm()} disabled={!selectedVariantId} />
            {selectedGroup && (
              <>
                <Button type="text" size="small" icon={<EditOutlined />} onClick={() => openGroupForm(selectedGroup)} />
                <Popconfirm title="Удалить?" onConfirm={() => handleDeleteGroup(selectedGroup.id)} okText="Да" cancelText="Нет">
                  <Button type="text" size="small" danger icon={<DeleteOutlined />} />
                </Popconfirm>
                <Button type="text" size="small" icon={<PlusOutlined />} onClick={() => openGroupForm(undefined, selectedGroup.id)} title="Подгруппа" />
              </>
            )}
          </Space>
        }
      >
        {!selectedVariantId ? (
          <Empty description="Выберите вариант" image={Empty.PRESENTED_IMAGE_SIMPLE} />
        ) : groupsLoading ? (
          <Spin size="small" style={{ display: "block", margin: "20px auto" }} />
        ) : (
          <>
            <Input.Search
              size="small"
              placeholder="Поиск..."
              allowClear
              onChange={(e) => setGroupSearch(e.target.value)}
              style={{ marginBottom: 4 }}
            />
            {treeData.length === 0 ? (
              <Empty description="Нет групп" image={Empty.PRESENTED_IMAGE_SIMPLE} />
            ) : (
              <Tree
                showIcon
                draggable
                defaultExpandAll
                treeData={treeData}
                selectedKeys={selectedGroupId ? [selectedGroupId] : []}
                onSelect={(keys) => setSelectedGroupId((keys[0] as number) || null)}
                onDrop={onDrop}
                style={{ fontSize: 12 }}
              />
            )}
          </>
        )}
      </Card>

      {/* RIGHT: Services */}
      <Card
        size="small"
        style={{ flex: 1, overflow: "auto" }}
        styles={{ body: { padding: "4px 6px" } }}
        title={
          selectedGroup ? (
            <Text strong style={{ fontSize: 12 }}>
              {selectedGroup.number}. {selectedGroup.name}
            </Text>
          ) : (
            <Text type="secondary" style={{ fontSize: 12 }}>Услуги</Text>
          )
        }
        extra={
          selectedGroup && (
            <Button type="primary" size="small" icon={<PlusOutlined />} onClick={() => { setEditService(null); setServiceFormOpen(true); }}>
              Добавить
            </Button>
          )
        }
      >
        {!selectedGroupId ? (
          <Empty description="Выберите группу" image={Empty.PRESENTED_IMAGE_SIMPLE} />
        ) : (
          <Table
            dataSource={services}
            columns={serviceColumns}
            rowKey="id"
            loading={servicesLoading}
            size="small"
            tableLayout="auto"
            pagination={false}
            locale={{ emptyText: "Нет услуг" }}
          />
        )}
      </Card>

      {/* Group form modal */}
      <Modal
        title={editGroup ? "Редактировать группу" : "Новая группа"}
        open={groupFormOpen}
        onCancel={() => setGroupFormOpen(false)}
        onOk={handleSaveGroup}
        confirmLoading={groupFormSaving}
        width={340}
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
              value={groupFormNumber}
              onChange={(v) => setGroupFormNumber(v || 0)}
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
