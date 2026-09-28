import { useEffect, useState } from "react";
import {
  Alert,
  Button,
  Card,
  Form,
  Input,
  Modal,
  Pagination,
  Popconfirm,
  Select,
  Space,
  Table,
  Tag,
  Tooltip,
  Typography,
  message,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import {
  CheckCircleOutlined,
  ClockCircleOutlined,
  CloseCircleOutlined,
  CopyOutlined,
  DeleteOutlined,
  DesktopOutlined,
  DownloadOutlined,
  EditOutlined,
  KeyOutlined,
  PlusOutlined,
  ReloadOutlined,
  SafetyCertificateOutlined,
  SyncOutlined,
} from "@ant-design/icons";
import {
  TerminalOnboardResponse,
  TerminalSettingsItem,
  deleteTerminal,
  listTerminalsSettings,
  onboardTerminal,
  retryTerminalOnboarding,
  updateTerminalSettings,
} from "../../api/settings";
import { useSession } from "../../session/SessionContext";
import OnboardingWizardModal from "../../components/OnboardingWizardModal";

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

  // Onboarding & PIN delivery states
  const [wizardOpen, setWizardOpen] = useState(false);
  const [onboardModalVisible, setOnboardModalVisible] = useState(false);
  const [onboardLoading, setOnboardLoading] = useState(false);
  const [pinDeliveryModalVisible, setPinDeliveryModalVisible] = useState(false);
  const [pinDeliveryData, setPinDeliveryData] = useState<TerminalOnboardResponse | null>(null);
  const [retryingId, setRetryingId] = useState<number | null>(null);
  const [deletingId, setDeletingId] = useState<number | null>(null);
  const [onboardForm] = Form.useForm();

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

  // Onboard new terminal
  const handleOpenOnboardModal = () => {
    onboardForm.resetFields();
    onboardForm.setFieldsValue({
      timezone: "Europe/Moscow",
    });
    setOnboardModalVisible(true);
  };

  const handleOnboardSubmit = async (values: any) => {
    setOnboardLoading(true);
    try {
      const res = await onboardTerminal(
        {
          name: values.name?.trim() || undefined,
          address: values.address?.trim() || undefined,
          note: values.note?.trim() || undefined,
          timezone: values.timezone || "Europe/Moscow",
        },
        user?.org_id || undefined
      );
      message.success(`Терминал ${res.sn} успешно подключен`);
      setOnboardModalVisible(false);
      onboardForm.resetFields();
      setPinDeliveryData(res);
      setPinDeliveryModalVisible(true);
      fetchTerminals();
    } catch (err: any) {
      message.error(err.response?.data?.detail || "Ошибка подключения терминала");
    } finally {
      setOnboardLoading(false);
    }
  };

  // Retry partial failure
  const handleRetry = async (record: TerminalSettingsItem) => {
    setRetryingId(record.id);
    try {
      const res = await retryTerminalOnboarding(record.id);
      message.success(`Повторный запрос для терминала ${res.sn} выполнен`);
      if (res.pin) {
        setPinDeliveryData(res);
        setPinDeliveryModalVisible(true);
      }
      fetchTerminals();
    } catch (err: any) {
      message.error(err.response?.data?.detail || "Ошибка выполнения повторного запроса");
    } finally {
      setRetryingId(null);
    }
  };

  // Delete terminal
  const handleDelete = async (record: TerminalSettingsItem) => {
    setDeletingId(record.id);
    try {
      const res = await deleteTerminal(record.id);
      message.success(res.message || `Терминал ${record.sn} удален`);
      fetchTerminals();
    } catch (err: any) {
      message.error(err.response?.data?.detail || "Ошибка удаления терминала");
    } finally {
      setDeletingId(null);
    }
  };

  const columns: ColumnsType<TerminalSettingsItem> = [
    {
      title: "ID / №",
      key: "device_id",
      width: 100,
      sorter: true,
      sortOrder:
        sortBy === "device_id"
          ? sortOrder === "asc"
            ? "ascend"
            : "descend"
          : null,
      render: (_, record) => (
        <Space direction="vertical" size={0}>
          <Text strong>{record.device_id}</Text>
          {record.ordinal != null && (
            <Text type="secondary" style={{ fontSize: 11 }}>
              №{record.ordinal}
            </Text>
          )}
        </Space>
      ),
    },
    {
      title: "Серийный номер (SN)",
      dataIndex: "sn",
      key: "sn",
      width: 190,
      render: (sn: string) => (
        <Space>
          <DesktopOutlined style={{ color: "#1677ff" }} />
          <Text copyable>{sn}</Text>
        </Space>
      ),
    },
    {
      title: "Тариф / Льгота",
      key: "is_free",
      width: 130,
      render: (_, record) =>
        record.is_free ? (
          <Tooltip title="Бесплатный первый терминал тенанта по тарифному плану">
            <Tag color="gold" style={{ fontWeight: 600 }}>
              0 ₽ (Льгота)
            </Tag>
          </Tooltip>
        ) : (
          <Tag color="default">
            Платный
          </Tag>
        ),
    },
    {
      title: "Четыре готовности (Readiness)",
      key: "readiness",
      width: 320,
      render: (_, record) => {
        const r = record.readiness;
        const certState =
          r?.certificate || (record.pin_state as any) || (record.cert_serial ? "consumed" : "pending");
        const iotState = r?.iot || (record.provisioning_state as any) || "pending";
        const onlineState = r?.online || (record.is_active ? "offline" : "offline");

        return (
          <Space size={[4, 4]} wrap>
            <Tooltip title="База данных: бизнес-запись терминала зафиксирована в БД">
              <Tag color="success">Запись: ОК</Tag>
            </Tooltip>

            <Tooltip title="Сертификат / PIN: статус ключей безопасности">
              {certState === "issued" ? (
                <Tag color="processing" icon={<KeyOutlined />}>
                  PIN готов
                </Tag>
              ) : certState === "consumed" ? (
                <Tag color="success" icon={<SafetyCertificateOutlined />}>
                  Сертификат ОК
                </Tag>
              ) : certState === "expired" ? (
                <Tag color="default">PIN истёк</Tag>
              ) : certState === "failed" ? (
                <Tag color="error">PIN ошибка</Tag>
              ) : (
                <Tag color="warning">PIN ожидает</Tag>
              )}
            </Tooltip>

            <Tooltip title="IoT Платформа: регистрация устройства в Leo4 IoT">
              {iotState === "ready" ? (
                <Tag color="success">IoT: ОК</Tag>
              ) : iotState === "failed" ? (
                <Tag color="error">IoT ошибка</Tag>
              ) : (
                <Tag color="warning">IoT ожидает</Tag>
              )}
            </Tooltip>

            <Tooltip title="Сеть: онлайн-статус агента терминала">
              {onlineState === "online" ? (
                <Tag icon={<CheckCircleOutlined />} color="success">
                  В сети
                </Tag>
              ) : (
                <Tag color="default">Офлайн</Tag>
              )}
            </Tooltip>
          </Space>
        );
      },
    },
    {
      title: "Адрес установки",
      dataIndex: "address",
      key: "address",
      ellipsis: true,
      render: (val: string | null) => val || <Text type="secondary">—</Text>,
    },
    {
      title: "Часовой пояс",
      dataIndex: "timezone",
      key: "timezone",
      width: 170,
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
      width: 100,
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
      width: 160,
      render: (_, record) => {
        const needsRetry =
          record.readiness?.certificate === "failed" ||
          record.readiness?.iot === "failed" ||
          record.pin_state === "failed" ||
          record.provisioning_state === "failed";
        const isRetrying = retryingId === record.id;
        const isDeleting = deletingId === record.id;

        return (
          <Space size="small">
            <Tooltip title="Редактировать параметры терминала">
              <Button
                size="small"
                icon={<EditOutlined />}
                onClick={() => handleEditClick(record)}
              />
            </Tooltip>

            {needsRetry && !isReadOnly && (
              <Tooltip title="Повторить запрос провиженинга / выпуска PIN">
                <Button
                  size="small"
                  type="primary"
                  danger
                  icon={isRetrying ? <SyncOutlined spin /> : <ReloadOutlined />}
                  loading={isRetrying}
                  onClick={() => handleRetry(record)}
                >
                  Повторить
                </Button>
              </Tooltip>
            )}

            {!isReadOnly && (
              <Popconfirm
                title="Удалить терминал?"
                description="Терминал будет отключен. Льгота бесплатного терминала автоматически перейдёт к следующему активному терминалу без ретро-пересчёта."
                okText="Удалить"
                cancelText="Отмена"
                okButtonProps={{ danger: true, loading: isDeleting }}
                onConfirm={() => handleDelete(record)}
              >
                <Button
                  size="small"
                  danger
                  icon={<DeleteOutlined />}
                  loading={isDeleting}
                />
              </Popconfirm>
            )}
          </Space>
        );
      },
    },
  ];

  return (
    <div style={{ maxWidth: 1400, margin: "0 auto", paddingBottom: 40 }}>
      <Card>
        <div
          style={{
            display: "flex",
            justifyContent: "space-between",
            alignItems: "center",
            flexWrap: "wrap",
            gap: 16,
            marginBottom: 20,
          }}
        >
          <div>
            <Title level={4} style={{ margin: 0 }}>
              Настройки терминалов
            </Title>
            <Paragraph type="secondary" style={{ margin: 0, marginTop: 4 }}>
              Управление подключением терминалов, доставкой PIN-кодов и мониторингом готовности
            </Paragraph>
          </div>

          <Space>
            <Button
              icon={<ReloadOutlined />}
              onClick={fetchTerminals}
              loading={loading}
            >
              Обновить
            </Button>
            {!isReadOnly && (
              <Button
                type="primary"
                icon={<PlusOutlined />}
                onClick={() => setWizardOpen(true)}
              >
                Подключить терминал (Мастер)
              </Button>
            )}
          </Space>
        </div>

        {/* Search and Pagination Header */}
        <div
          style={{
            display: "flex",
            justifyContent: "space-between",
            alignItems: "center",
            flexWrap: "wrap",
            gap: 12,
            marginBottom: 16,
          }}
        >
          <Input.Search
            placeholder="Поиск по ID, SN, адресу, примечанию..."
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

      {/* Onboard Terminal Modal */}
      <Modal
        title="Подключение нового терминала"
        open={onboardModalVisible}
        onCancel={() => !onboardLoading && setOnboardModalVisible(false)}
        footer={null}
        destroyOnClose
      >
        <Alert
          type="info"
          showIcon
          style={{ marginBottom: 16 }}
          message="Единый процесс onboarding"
          description="Создание терминала регистрирует бизнес-запись, выполняет провиженинг в IoT платформе и генерирует одноразовый PIN-код для агента."
        />

        <Form
          form={onboardForm}
          layout="vertical"
          onFinish={handleOnboardSubmit}
        >
          <Form.Item
            name="name"
            label="Название терминала"
            tooltip="Серийный номер и device_id назначаются сервером автоматически"
          >
            <Input placeholder="Например: POS-01" maxLength={500} />
          </Form.Item>

          <Form.Item
            name="address"
            label="Адрес установки"
            tooltip="Фактический адрес размещения терминала"
          >
            <Input.TextArea
              rows={2}
              placeholder="г. Москва, ул. Примерная, д. 10"
              maxLength={500}
              showCount
            />
          </Form.Item>

          <Form.Item
            name="note"
            label="Примечание"
            tooltip="Дополнительные служебные заметки"
          >
            <Input.TextArea
              rows={2}
              placeholder="Терминал у главного входа"
              maxLength={500}
              showCount
            />
          </Form.Item>

          <Form.Item
            name="timezone"
            label="Часовой пояс"
            initialValue="Europe/Moscow"
          >
            <Select
              options={COMMON_TIMEZONES}
              showSearch
              optionFilterProp="label"
            />
          </Form.Item>

          <div style={{ textAlign: "right", marginTop: 24 }}>
            <Space>
              <Button
                disabled={onboardLoading}
                onClick={() => setOnboardModalVisible(false)}
              >
                Отмена
              </Button>
              <Button
                type="primary"
                htmlType="submit"
                loading={onboardLoading}
                disabled={onboardLoading}
              >
                Создать и выпустить PIN
              </Button>
            </Space>
          </div>
        </Form>
      </Modal>

      {/* PIN Delivery Modal */}
      <Modal
        title="Доставка PIN-кода для Агента"
        open={pinDeliveryModalVisible}
        onCancel={() => setPinDeliveryModalVisible(false)}
        footer={[
          <Button
            key="close"
            type="primary"
            onClick={() => setPinDeliveryModalVisible(false)}
          >
            Понятно, закрыть
          </Button>,
        ]}
        width={560}
      >
        {pinDeliveryData && (
          <div>
            <Alert
              type="warning"
              showIcon
              style={{ marginBottom: 20 }}
              message="Одноразовый PIN-код"
              description="PIN-код необходим для первой авторизации Агента на терминале. Сохраните его сейчас — после активации сертификата PIN больше не будет отображаться."
            />

            <div style={{ textAlign: "center", margin: "24px 0" }}>
              <Text type="secondary" style={{ display: "block", marginBottom: 8 }}>
                Терминал: <Text strong>{pinDeliveryData.sn}</Text> (ID: {pinDeliveryData.terminal_id})
              </Text>
              {pinDeliveryData.pin ? (
                <div
                  style={{
                    display: "inline-block",
                    padding: "16px 36px",
                    background: "#f6ffed",
                    border: "2px dashed #52c41a",
                    borderRadius: 8,
                  }}
                >
                  <Title
                    level={1}
                    style={{
                      margin: 0,
                      letterSpacing: 8,
                      color: "#389e0d",
                      fontFamily: "monospace",
                    }}
                  >
                    {pinDeliveryData.pin}
                  </Title>
                  <Button
                    size="small"
                    icon={<CopyOutlined />}
                    style={{ marginTop: 8 }}
                    onClick={() => {
                      if (pinDeliveryData.pin) {
                        navigator.clipboard.writeText(pinDeliveryData.pin);
                        message.success("PIN скопирован в буфер обмена");
                      }
                    }}
                  >
                    Копировать PIN
                  </Button>
                </div>
              ) : (
                <div style={{ padding: 16, background: "#fafafa", borderRadius: 8 }}>
                  <Text type="secondary">
                    PIN-код уже активирован или истёк ({pinDeliveryData.pin_masked || "***"})
                  </Text>
                </div>
              )}
            </div>

            <Card
              size="small"
              style={{ background: "#f0f5ff", borderColor: "#adc6ff", marginBottom: 16 }}
            >
              <Space direction="vertical" style={{ width: "100%" }}>
                <Text strong>Установка и активация Агента:</Text>
                <Paragraph style={{ margin: 0, fontSize: 13 }}>
                  1. Скачайте установочный пакет Агента по ссылке ниже.
                  <br />
                  2. Запустите инсталлятор на целевом компьютере.
                  <br />
                  3. Введите указанный PIN-код при запросе мастера установки.
                  <br />
                  4. После выпуска сертификата терминал автоматически перейдёт в онлайн.
                </Paragraph>
                <div style={{ marginTop: 8 }}>
                  <Button
                    type="primary"
                    icon={<DownloadOutlined />}
                    href={pinDeliveryData.agent_release_url}
                    target="_blank"
                    rel="noopener noreferrer"
                  >
                    Скачать Агент L4Desk (v{pinDeliveryData.agent_version})
                  </Button>
                </div>
              </Space>
            </Card>

            <div style={{ fontSize: 12, color: "#8c8c8c" }}>
              Тариф: {pinDeliveryData.is_free ? "Льготный (0 ₽/цикл)" : "Стандартный"} | Ordinal: №{pinDeliveryData.ordinal}
            </div>
          </div>
        )}
      </Modal>

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

      {/* L4Desk Onboarding Wizard */}
      <OnboardingWizardModal
        open={wizardOpen}
        onClose={() => setWizardOpen(false)}
        onTerminalCreated={() => fetchTerminals()}
        orgId={user?.org_id || undefined}
      />
    </div>
  );
}
