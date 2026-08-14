import { useEffect, useState, useCallback } from "react";
import { useNavigate } from "react-router";
import {
  Button,
  Card,
  Col,
  Dropdown,
  Empty,
  Input,
  message,
  Modal,
  Row,
  Select,
  Space,
  Spin,
  Tree,
  Typography,
} from "antd";
import {
  PlusOutlined,
  FolderOutlined,
  FolderOpenOutlined,
  EditOutlined,
  DeleteOutlined,
  CopyOutlined,
  MenuOutlined,
} from "@ant-design/icons";
import type { DataNode, TreeProps } from "antd/es/tree";
import { getGroups, deleteGroup, Group } from "../api/groups";
import {
  getMenuVariants,
  createMenuVariant,
  duplicateMenuVariant,
  deleteMenuVariant,
  MenuVariant,
} from "../api/menu-variants";
import GroupForm from "../components/GroupForm";

const { Title, Text } = Typography;

function buildTree(groups: Group[], parentId: number | null = null): DataNode[] {
  return groups
    .filter((g) => g.parent_id === parentId)
    .sort((a, b) => a.number - b.number)
    .map((g) => ({
      key: g.id,
      title: (
        <span>
          <Text strong>{g.number}.</Text> {g.name}
        </span>
      ),
      icon: ({ expanded }: { expanded?: boolean }) =>
        expanded ? <FolderOpenOutlined /> : <FolderOutlined />,
      children: buildTree(groups, g.id),
    }));
}

export default function GroupsPage() {
  const [groups, setGroups] = useState<Group[]>([]);
  const [variants, setVariants] = useState<MenuVariant[]>([]);
  const [variantId, setVariantId] = useState<number | null>(null);
  const [loading, setLoading] = useState(true);
  const [search, setSearch] = useState("");
  const [selected, setSelected] = useState<Group | null>(null);
  const [formOpen, setFormOpen] = useState(false);
  const [editGroup, setEditGroup] = useState<Group | null>(null);
  const [parentId, setParentId] = useState<number | null>(null);
  const navigate = useNavigate();

  const loadVariants = useCallback(async () => {
    try {
      const res = await getMenuVariants();
      setVariants(res.data);
      if (res.data.length > 0 && variantId === null) {
        setVariantId(res.data[0].id);
      }
    } catch {
      message.error("Ошибка загрузки вариантов меню");
    }
  }, [variantId]);

  const loadGroups = useCallback(async () => {
    if (!variantId) { setGroups([]); return; }
    setLoading(true);
    try {
      const res = await getGroups(variantId);
      setGroups(res.data);
    } catch {
      message.error("Ошибка загрузки групп");
    } finally {
      setLoading(false);
    }
  }, [variantId]);

  useEffect(() => { loadVariants(); }, []);
  useEffect(() => { loadGroups(); setSelected(null); }, [variantId]);

  const treeData = buildTree(
    search
      ? groups.filter((g) => g.name.toLowerCase().includes(search.toLowerCase()))
      : groups
  );

  const handleDelete = async (id: number) => {
    try {
      await deleteGroup(id);
      message.success("Группа удалена");
      if (selected?.id === id) setSelected(null);
      loadGroups();
    } catch (e: any) {
      message.error(e.message);
    }
  };

  const onDrop: TreeProps["onDrop"] = async (info) => {
    const dragId = info.dragNode.key as number;
    const dropId = info.node.key as number;
    const dropToGap = info.dropToGap;
    try {
      if (dropToGap) {
        await (await import("../api/groups")).updateGroup(dragId, { parent_id: null });
      } else {
        await (await import("../api/groups")).updateGroup(dragId, { parent_id: dropId });
      }
      message.success("Группа перемещена");
      loadGroups();
    } catch (e: any) {
      message.error(e.message);
    }
  };

  const handleCreateVariant = async () => {
    Modal.confirm({
      title: "Новый вариант меню",
      content: (
        <Input id="new-variant-name" placeholder="Название" />
      ),
      onOk: async () => {
        const input = document.getElementById("new-variant-name") as HTMLInputElement;
        const name = input?.value?.trim();
        if (!name) { message.warning("Введите название"); throw new Error(); }
        try {
          await createMenuVariant({ name });
          message.success("Вариант создан");
          loadVariants();
        } catch (e: any) {
          message.error(e.message);
        }
      },
    });
  };

  const handleDuplicate = async () => {
    if (!variantId) return;
    const source = variants.find((v) => v.id === variantId);
    if (!source) return;
    Modal.confirm({
      title: "Дублировать вариант",
      content: (
        <div>
          <p>Копировать «{source.name}»?</p>
          <Input id="dup-variant-name" placeholder="Название (оставьте пустым для авто)" />
        </div>
      ),
      onOk: async () => {
        const input = document.getElementById("dup-variant-name") as HTMLInputElement;
        const name = input?.value?.trim() || undefined;
        try {
          const res = await duplicateMenuVariant({ source_variant_id: variantId, new_name: name });
          message.success(`Создан «${res.data.name}»`);
          setVariantId(res.data.id);
          loadVariants();
        } catch (e: any) {
          message.error(e.message);
        }
      },
    });
  };

  const handleDeleteVariant = async () => {
    if (!variantId) return;
    const source = variants.find((v) => v.id === variantId);
    if (!source) return;
    try {
      await deleteMenuVariant(variantId);
      message.success(`Вариант «${source.name}» удалён`);
      setVariantId(null);
      loadVariants();
    } catch (e: any) {
      message.error(e.message);
    }
  };

  const selectedGroup = groups.find((g) => g.id === selected?.id);

  return (
    <>
      <div style={{ display: "flex", justifyContent: "space-between", marginBottom: 16 }}>
        <Title level={3} style={{ margin: 0 }}>
          Группы
        </Title>
        <Space>
          <Select
            placeholder="Выберите вариант меню"
            value={variantId}
            onChange={setVariantId}
            style={{ width: 240 }}
            options={variants.map((v) => ({ value: v.id, label: v.name }))}
            showSearch
            optionFilterProp="label"
          />
          <Button icon={<PlusOutlined />} onClick={handleCreateVariant}>
            Новый вариант
          </Button>
          <Button icon={<CopyOutlined />} onClick={handleDuplicate} disabled={!variantId}>
            Дублировать
          </Button>
          <Button danger icon={<DeleteOutlined />} onClick={handleDeleteVariant} disabled={!variantId}>
            Удалить вариант
          </Button>
        </Space>
      </div>

      {variantId && (
        <div style={{ display: "flex", justifyContent: "space-between", marginBottom: 16 }}>
          <div />
          <Button
            type="primary"
            icon={<PlusOutlined />}
            onClick={() => { setEditGroup(null); setParentId(null); setFormOpen(true); }}
          >
            Добавить группу
          </Button>
        </div>
      )}

      {!variantId ? (
        <Card>
          <Empty description="Создайте или выберите вариант меню" />
        </Card>
      ) : (
        <Row gutter={24}>
          <Col xs={24} lg={10}>
            <Card
              styles={{ body: { padding: 12 } }}
              extra={
                <Input.Search
                  placeholder="Поиск..."
                  allowClear
                  onChange={(e) => setSearch(e.target.value)}
                  style={{ width: 200 }}
                />
              }
            >
              {loading ? (
                <Spin style={{ display: "block", margin: "40px auto" }} />
              ) : treeData.length === 0 ? (
                <Empty description="Нет групп" />
              ) : (
                <Tree
                  showIcon
                  draggable
                  defaultExpandAll
                  treeData={treeData}
                  onSelect={(keys) => {
                    const id = keys[0] as number;
                    setSelected(groups.find((g) => g.id === id) || null);
                  }}
                  onDrop={onDrop}
                  selectedKeys={selected ? [selected.id] : []}
                />
              )}
            </Card>
          </Col>

          <Col xs={24} lg={14}>
            {selectedGroup ? (
              <Card
                title={`${selectedGroup.number}. ${selectedGroup.name}`}
                extra={
                  <Space>
                    <Button
                      icon={<PlusOutlined />}
                      onClick={() => { setParentId(selectedGroup.id); setEditGroup(null); setFormOpen(true); }}
                    >
                      Подгруппа
                    </Button>
                    <Button
                      icon={<EditOutlined />}
                      onClick={() => { setEditGroup(selectedGroup); setParentId(null); setFormOpen(true); }}
                    >
                      Редактировать
                    </Button>
                    <Button
                      danger
                      icon={<DeleteOutlined />}
                      onClick={() => handleDelete(selectedGroup.id)}
                    >
                      Удалить
                    </Button>
                    <Button type="primary" onClick={() => navigate(`/groups/${selectedGroup.id}`)}>
                      Услуги →
                    </Button>
                  </Space>
                }
              >
                <p><Text type="secondary">ID:</Text> {selectedGroup.id}</p>
                <p><Text type="secondary">Организация:</Text> {selectedGroup.org_id}</p>
                <p><Text type="secondary">Номер:</Text> {selectedGroup.number}</p>
                <p><Text type="secondary">Родитель:</Text> {selectedGroup.parent_id ?? "—"}</p>
              </Card>
            ) : (
              <Card>
                <Empty description="Выберите группу из дерева" />
              </Card>
            )}
          </Col>
        </Row>
      )}

      <GroupForm
        open={formOpen}
        group={editGroup}
        parentId={parentId}
        menuVariantId={variantId}
        groups={groups}
        onClose={() => setFormOpen(false)}
        onSaved={() => { setFormOpen(false); loadGroups(); }}
      />
    </>
  );
}
