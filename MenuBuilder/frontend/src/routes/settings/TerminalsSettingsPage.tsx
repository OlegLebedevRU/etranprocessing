import { useEffect, useState } from "react";
import {
  Alert,
  Button,
  Card,
  Form,
  Input,
  Modal,
  Pagination,
  Select,
  Space,
  Table,
  Tag,
  Typography,
  message,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import {
  CheckCircleOutlined,
  ClockCircleOutlined,
  CloseCircleOutlined,
  DesktopOutlined,
  EditOutlined,
  ReloadOutlined,
} from "@ant-design/icons";
import {
  TerminalSettingsItem,
  listTerminalsSettings,
  updateTerminalSettings,
} from "../../api/settings";
import { useSession } from "../../session/SessionContext";

const { Text, Title, Paragraph } = Typography;

const COMMON_TIMEZONES = [
  { value: "Europe/Kaliningrad", label: "Europe/Kaliningrad (UTC+2)" },
  { value: "Europe/Moscow", label: "Europe/Moscow (UTC+3, МСК)" },
  { value: "Europe/Samara", label: "Europe/Samara (UTC+4)" },
  { value: "Asia/Yekaterinburg", label: "Asia/Yekaterinburg (UTC+5)" },
  { value: "Asia/Omsk", label: "Asia/Omsk (UTC+6)" },
  { value: "Asia/Novosibirsk", label: "Asia/Novosibirsk (UTC+7)" },
  { value: "Asia/Krasnoyarsk", label: "Asia/Krasnoyarsk (UTC+7)" },
  { value: "Asia/Irkutsk", label: "Asia/Irkutsk (UTC+8)" },
  { value: "Asia/Yakutsk", label: "Asia/Yakutsk (UTC+9)" },
  { value: "Asia/Vladivostok", label: "Asia/Vladivostok (UTC+10)" },
  { value: "Asia/Magadan", label: "Asia/Magadan (UTC+11)" },
  { value: "Asia/Kamchatka", label: "Asia/Kamchatka (UTC+12)" },
];

export default function TerminalsSettingsPage() {
  const { user } = useSession();
  const [loading, setLoading] = useState(true);
  const [terminals, setTerminals] = useState<TerminalSettingsItem[]>([]);
  const [editingTerminal, setEditingTerminal] = useState<TerminalSettingsItem | null>(null);
  const [modalVisible, setModalVisible] = useState(false);
  const [saving, setSaving] = useState(false);
  const [form] = Form.useForm();

  // Smart pagination, search, sorting
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(50);
  const [totalCount, setTotalCount] = useState(0);
  const [search, setSearch] = useState("");
  const [searchInput, setSearchInput] = useState("");
  const [sortBy, setSortBy] = useState("device_id");
  const [sortOrder, setSortOrder] = useState<"asc" | "desc">("asc");

  const isSuperuser = Boolean(user?.is_superuser || user?.role_id === 1);
  const isRole4 = user?.role_id === 4;
  const isReadOnly = isSuperuser || isRole4;

  const fetchTerminals = async () => {
    setLoading(true);
    try {
      const data = await listTerminalsSettings({
        org_id: user?.org_id || undefined,
        search: search.trim() || undefined,
        sort_by: sortBy,
        sort_order: sortOrder,
        page,
        page_size: pageSize,
      });
      setTerminals(data.items);
      setTotalCount(data.total_count);
    } catch (err: any) {
      message.error(err.response?.data?.detail || "Ошибка загрузки списка терминалов");
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchTerminals();
  }, [user?.org_id, page, pageSize, search, sortBy, sortOrder]);

  const handleTableChange = (
    _pagination: any,
    _filters: any,
    sorter: any,
  ) => {
    if (sorter && sorter.field) {
      const field = String(sorter.field);
      const order = sorter.order === "descend" ? "desc" : "asc";
      setSortBy(field);
      setSortOrder(order);
    }
  };

  const handleEditClick = (term: TerminalSettingsItem) => {
    setEditingTerminal(term);
    form.setFieldsValue({
      address: term.address || "",
      note: term.note || "",
      timezone: term.timezone || undefined,
    });
    setModalVisible(true);
  };

  const handleSaveTerminal = async (values: any) => {
    if (!editingTerminal || isReadOnly) return;
    setSaving(true);
    try {
      const updated = await updateTerminalSettings(editingTerminal.id, {
        address: values.address,
        note: values.note,
        timezone: values.timezone,
      });
      message.success(`Настройки терминала ${updated.sn} обновлены`);
      setModalVisible(false);
      setEditingTerminal(null);
      fetchTerminals();
    } catch (err: any) {
      message.error(err.response?.data?.detail || "Ошибка сохранения терминала");
    } finally {
      setSaving(false);
    }
  };

  const columns: ColumnsType<TerminalSettingsItem> = [
    {
      title: "Device ID",
      dataIndex: "device_id",
      key: "device_id",
      width: 120,
      sorter: true,
      sortOrder:
        sortBy === "device_id"
          ? sortOrder === "asc"
            ? "ascend"
            : "descend"
          : null,
      render: (id: number) => <Text strong>{id}</Text>,
    },
    {
      title: "Серийный номер (SN)",
      dataIndex: "sn",
      key: "sn",
      width: 200,
      render: (sn: string) => (
        <Space>
          <DesktopOutlined style={{ color: "#1677ff" }} />
          <Text copyable>{sn}</Text>
        </Space>
      ),
    },
    {
      title: "Адрес установки",
      dataIndex: "address",
      key: "address",
      ellipsis: true,
      render: (val: string | null) => val || <Text type="secondary">—</Text>,
    },
    {
      title: "Примечание",
      dataIndex: "note",
      key: "note",
      ellipsis: true,
      render: (val: string | null) => val || <Text type="secondary">—</Text>,
    },
    {
      title: "Часовой пояс",
      dataIndex: "timezone",
      key: "timezone",
      width: 180,
      render: (tz: string | null) =>
        tz ? (
          <Space size={4}>
            <ClockCircleOutlined style={{ color: "#8c8c8c" }} />
            <span>{tz}</span>
          </Space>
        ) : (
          <Text type="secondary">По умолчанию</Text>
        ),
    },
    {
      title: "Статус",
      dataIndex: "is_active",
      key: "is_active",
      width: 110,
      render: (active: boolean) =>
        active ? (
          <Tag icon={<CheckCircleOutlined />} color="success">
            Активен
          </Tag>
        ) : (
          <Tag icon={<CloseCircleOutlined />} color="error">
            Отключен
          </Tag>
        ),
    },
    {
      title: "Действия",
      key: "actions",
      width: 140,
      render: (_, record) => (
        <Button
          size="small"
          icon={<EditOutlined />}
          onClick={() => handleEditClick(record)}
        >
          {isReadOnly ? "Просмотр" : "Изменить"}
        </Button>
      ),
    },
  ];

  return (
    <div style={{ maxWidth: 1200, margin: "0 auto", paddingBottom: 40 }}>
      {isReadOnly && (
        <Alert
          type="info"
          showIcon
          style={{ marginBottom: 16 }}
          message="Режим только для чтения"
          description="Просмотр параметров терминалов доступен в режиме только для чтения без возможности внесения изменений."
        />
      )}

      <Card
        title={
          <Space>
            <DesktopOutlined />
            <span>Терминалы организации</span>
            <Tag color="default">{totalCount} шт.</Tag>
          </Space>
        }
        extra={
          <Button
            icon={<ReloadOutlined />}
            onClick={fetchTerminals}
            loading={loading}
          >
            Обновить
          </Button>
        }
      >
        <Paragraph type="secondary" style={{ marginBottom: 16 }}>
          В данном подразделе для каждого терминала вашей организации можно настраивать адрес
          установки, служебное примечание и индивидуальный часовой пояс.
        </Paragraph>

        <div
          style={{
            display: "flex",
            justifyContent: "space-between",
            alignItems: "center",
            marginBottom: 16,
            flexWrap: "wrap",
            gap: 12,
          }}
        >
          <Input.Search
            placeholder="Поиск по номерам (через запятую: 101, 102), SN, адресу, примечанию"
            allowClear
            value={searchInput}
            onChange={(e) => setSearchInput(e.target.value)}
            onSearch={(val) => {
              setSearch(val.trim());
              setPage(1);
            }}
            style={{ maxWidth: 460, flex: "1 1 300px" }}
          />
          <Pagination
            size="small"
            current={page}
            pageSize={pageSize}
            total={totalCount}
            showSizeChanger
            pageSizeOptions={["50", "100", "200"]}
            onChange={(p, ps) => {
              setPage(p);
              setPageSize(ps);
            }}
            showTotal={(total, range) => `${range[0]}–${range[1]} из ${total}`}
          />
        </div>

        <Table
          columns={columns}
          dataSource={terminals}
          rowKey="id"
          loading={loading}
          onChange={handleTableChange}
          pagination={false}
          locale={{ emptyText: "Нет зарегистрированных терминалов для выбранной организации" }}
        />
      </Card>

      {/* Edit Modal */}
      <Modal
        title={`${isReadOnly ? "Просмотр параметров терминала" : "Редактирование терминала"} ${editingTerminal?.sn || ""}`}
        open={modalVisible}
        onCancel={() => setModalVisible(false)}
        footer={null}
        destroyOnClose
      >
        <Alert
          type="info"
          showIcon
          style={{ marginBottom: 16 }}
          message={isReadOnly ? "Режим только для чтения" : "Ограничение параметров"}
          description={
            isReadOnly
              ? "Параметры терминала доступны только для просмотра."
              : "В данном разделе можно изменять только адрес, примечание и часовой пояс терминала."
          }
        />

        <Form
          form={form}
          layout="vertical"
          onFinish={handleSaveTerminal}
          disabled={isReadOnly}
        >
          <Form.Item
            name="address"
            label="Адрес установки"
            tooltip="Фактический адрес размещения терминала самообслуживания"
          >
            <Input.TextArea
              rows={2}
              placeholder="г. Москва, ул. Примерная, д. 10, ТЦ 'Пример'"
              maxLength={500}
              showCount
            />
          </Form.Item>

          <Form.Item
            name="note"
            label="Примечание"
            tooltip="Внутренние заметки, контакты инкассатора или арендодателя"
          >
            <Input.TextArea
              rows={2}
              placeholder="Терминал у главного входа, контакт: +7 (900) 000-00-00"
              maxLength={500}
              showCount
            />
          </Form.Item>

          <Form.Item
            name="timezone"
            label="Часовой пояс терминала"
            tooltip="Часовой пояс для Z-отчетов и смен данного терминала"
          >
            <Select
              options={COMMON_TIMEZONES}
              showSearch
              allowClear
              optionFilterProp="label"
              placeholder="По умолчанию (часовой пояс организации)"
            />
          </Form.Item>

          <div style={{ textAlign: "right", marginTop: 24 }}>
            <Space>
              <Button onClick={() => setModalVisible(false)}>
                {isReadOnly ? "Закрыть" : "Отмена"}
              </Button>
              {!isReadOnly && (
                <Button
                  type="primary"
                  htmlType="submit"
                  loading={saving}
                >
                  Сохранить
                </Button>
              )}
            </Space>
          </div>
        </Form>
      </Modal>
    </div>
  );
}
