import { useEffect, useState, useCallback } from "react";
import { useNavigate } from "react-router";
import {
  Alert,
  Button,
  Card,
  DatePicker,
  Divider,
  Dropdown,
  Form,
  Input,
  InputNumber,
  Modal,
  Popconfirm,
  Select,
  Space,
  Switch,
  Table,
  Tag,
  Tooltip,
  Typography,
  message,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import {
  CalendarOutlined,
  CloudOutlined,
  CloudUploadOutlined,
  ClusterOutlined,
  CopyOutlined,
  DownOutlined,
  EditOutlined,
  KeyOutlined,
  PlusOutlined,
  PoweroffOutlined,
  ReloadOutlined,
  SearchOutlined,
  SwapOutlined,
} from "@ant-design/icons";
import {
  createAdminTerminal,
  generateTerminalPin,
  getAdminOrganizations,
  getAdminTerminals,
  getNextDeviceId,
  getTerminalTypes,
  provisionBatchTerminalsToIot,
  provisionTerminalToIot,
  setTerminalLicense,
  setTerminalStatus,
  updateAdminTerminal,
  type AdminOrg,
  type AdminTerminal,
  type AdminTerminalCreateInput,
  type AdminTerminalUpdateInput,
  type GeneratePinResponse,
  type TerminalType,
} from "../api/admin";

const { Title, Text, Paragraph } = Typography;

export default function AdminTerminalsPage() {
  const navigate = useNavigate();
  const [loading, setLoading] = useState(false);
  const [terminals, setTerminals] = useState<AdminTerminal[]>([]);
  const [total, setTotal] = useState(0);
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(50);

  // Filter states
  const [selectedOrgId, setSelectedOrgId] = useState<number | undefined>(() => {
    const stored = localStorage.getItem("mb_current_org_id");
    return stored ? Number(stored) : undefined;
  });
  const [selectedStatus, setSelectedStatus] = useState<boolean | undefined>(undefined);
  const [searchInput, setSearchInput] = useState("");
  const [searchQuery, setSearchQuery] = useState("");

  // Dictionaries
  const [orgs, setOrgs] = useState<AdminOrg[]>([]);
  const [terminalTypes, setTerminalTypes] = useState<TerminalType[]>([]);

  // Create Modal state
  const [createModalOpen, setCreateModalOpen] = useState(false);
  const [createForm] = Form.useForm();
  const [createSubmitting, setCreateSubmitting] = useState(false);
  const [generatingDeviceId, setGeneratingDeviceId] = useState(false);

  // Edit Modal state
  const [editModalOpen, setEditModalOpen] = useState(false);
  const [editingTerminal, setEditingTerminal] = useState<AdminTerminal | null>(null);
  const [editForm] = Form.useForm();
  const [editSubmitting, setEditSubmitting] = useState(false);

  // PIN Result Modal state
  const [pinModalOpen, setPinModalOpen] = useState(false);
  const [generatedPinData, setGeneratedPinData] = useState<GeneratePinResponse | null>(null);

  // Set License Modal state
  const [licenseModalOpen, setLicenseModalOpen] = useState(false);
  const [licenseTerminal, setLicenseTerminal] = useState<AdminTerminal | null>(null);
  const [licenseForm] = Form.useForm();
  const [licenseSubmitting, setLicenseSubmitting] = useState(false);

  // Leo4 IoT Provisioning state
  const [provisionModalOpen, setProvisionModalOpen] = useState(false);
  const [provisioningTerminal, setProvisioningTerminal] = useState<AdminTerminal | null>(null);
  const [provisionSubmitting, setProvisionSubmitting] = useState(false);
  const [selectedRowKeys, setSelectedRowKeys] = useState<React.Key[]>([]);
  const [batchProvisionLoading, setBatchProvisionLoading] = useState(false);

  // Load dictionaries
  useEffect(() => {
    getAdminOrganizations()
      .then((data) => {
        setOrgs(data);
        if (data.length > 0) {
          setSelectedOrgId((prev) => {
            if (prev !== undefined && data.some((o) => o.org_id === prev)) {
              return prev;
            }
            return data[0].org_id;
          });
        }
      })
      .catch(() => {});
    getTerminalTypes()
      .then((data) => setTerminalTypes(data))
      .catch(() => {});
  }, []);

  // Fetch terminals list
  const fetchTerminals = useCallback(async () => {
    if (selectedOrgId === undefined) return;
    setLoading(true);
    try {
      const res = await getAdminTerminals({
        org_id: selectedOrgId,
        is_active: selectedStatus,
        search: searchQuery || undefined,
        page,
        page_size: pageSize,
      });
      setTerminals(res.items);
      setTotal(res.total);
    } catch (err: any) {
      message.error(err.message || "Ошибка загрузки списка терминалов");
    } finally {
      setLoading(false);
    }
  }, [selectedOrgId, selectedStatus, searchQuery, page, pageSize]);

  useEffect(() => {
    fetchTerminals();
  }, [fetchTerminals]);

  const handleSearch = () => {
    setPage(1);
    setSearchQuery(searchInput.trim());
  };

  const handleResetFilters = () => {
    setSelectedStatus(undefined);
    setSearchInput("");
    setSearchQuery("");
    setPage(1);
  };

  // --- Create Terminal Handlers ---
  const handleOpenCreate = async () => {
    createForm.resetFields();
    createForm.setFieldsValue({
      device_id: undefined,
      org_id: selectedOrgId || (orgs.length > 0 ? orgs[0].org_id : undefined),
      terminal_type_id: 0,
      address: "",
      note: "",
      is_active: true,
      show_in_monitoring: true,
      iot_provisioned: false,
      license_months: 12,
      billing_period_months: 1,
    });
    setCreateModalOpen(true);
  };

  const handleGenerateNextDeviceId = async () => {
    setGeneratingDeviceId(true);
    try {
      const res = await getNextDeviceId();
      createForm.setFieldsValue({ device_id: res.next_device_id });
      message.success(`Свободный номер: ${res.next_device_id}`);
    } catch (err: any) {
      message.error(err.message || "Не удалось получить свободный номер");
    } finally {
      setGeneratingDeviceId(false);
    }
  };

  const handleCreateSubmit = async (values: any) => {
    setCreateSubmitting(true);
    try {
      const months = Number(values.license_months || 12);
      const expDate = new Date();
      expDate.setMonth(expDate.getMonth() + months);

      const payload: AdminTerminalCreateInput = {
        device_id: Number(values.device_id),
        org_id: Number(values.org_id),
        terminal_type_id: Number(values.terminal_type_id || 0),
        address: values.address?.trim() || undefined,
        note: values.note?.trim() || undefined,
        is_active: Boolean(values.is_active),
        show_in_monitoring: Boolean(values.show_in_monitoring !== undefined ? values.show_in_monitoring : true),
        iot_provisioned: Boolean(values.iot_provisioned),
        license_expires_at: expDate.toISOString(),
        billing_period_months: Number(values.billing_period_months || 1),
        renewal_enabled: true,
      };

      const created = await createAdminTerminal(payload);
      message.success(`Терминал #${created.device_id} (SN: ${created.sn}) успешно создан`);
      setCreateModalOpen(false);
      fetchTerminals();
    } catch (err: any) {
      message.error(err.message || "Ошибка создания терминала");
    } finally {
      setCreateSubmitting(false);
    }
  };

  // --- Edit Terminal Handlers ---
  const handleOpenEdit = (term: AdminTerminal) => {
    setEditingTerminal(term);
    editForm.resetFields();
    editForm.setFieldsValue({
      org_id: term.org_id,
      terminal_type_id: term.terminal_type_id,
      address: term.address || "",
      note: term.note || "",
      is_active: term.is_active,
      show_in_monitoring: term.show_in_monitoring ?? true,
      iot_provisioned: term.iot_provisioned ?? false,
      billing_period_months: term.billing_period_months || 1,
      renewal_enabled: term.renewal_enabled ?? true,
      monthly_price_override_rub: term.monthly_price_override_minor
        ? (term.monthly_price_override_minor / 100).toFixed(2)
        : undefined,
    });
    setEditModalOpen(true);
  };

  const handleEditSubmit = async (values: any) => {
    if (!editingTerminal) return;
    setEditSubmitting(true);
    try {
      const payload: AdminTerminalUpdateInput = {
        org_id: Number(values.org_id),
        terminal_type_id: Number(values.terminal_type_id),
        address: values.address?.trim() || "",
        note: values.note?.trim() || "",
        is_active: Boolean(values.is_active),
        show_in_monitoring: Boolean(values.show_in_monitoring),
        iot_provisioned: Boolean(values.iot_provisioned),
        billing_period_months: Number(values.billing_period_months || 1),
        renewal_enabled: Boolean(values.renewal_enabled),
        monthly_price_override_minor: values.monthly_price_override_rub
          ? Math.round(Number(values.monthly_price_override_rub) * 100)
          : null,
      };

      await updateAdminTerminal(editingTerminal.id, payload);
      message.success(`Терминал #${editingTerminal.device_id} обновлен`);
      setEditModalOpen(false);
      fetchTerminals();
    } catch (err: any) {
      message.error(err.message || "Ошибка обновления терминала");
    } finally {
      setEditSubmitting(false);
    }
  };

  // --- Generate PIN Handlers ---
  const handleGeneratePin = async (term: AdminTerminal) => {
    try {
      const res = await generateTerminalPin(term.id);
      setGeneratedPinData(res);
      setPinModalOpen(true);
      fetchTerminals();
    } catch (err: any) {
      message.error(err.message || "Ошибка генерации PIN");
    }
  };

  // --- Set License Handlers ---
  const handleOpenLicenseModal = (term: AdminTerminal) => {
    setLicenseTerminal(term);
    licenseForm.resetFields();
    licenseForm.setFieldsValue({
      is_active: true,
      renewal_enabled: term.renewal_enabled ?? true,
    });
    setLicenseModalOpen(true);
  };

  const handleSetLicenseSubmit = async (values: any) => {
    if (!licenseTerminal) return;
    if (!values.expires_at) {
      message.warning("Выберите дату окончания лицензии");
      return;
    }
    setLicenseSubmitting(true);
    try {
      const dateStr = values.expires_at.toISOString();
      await setTerminalLicense(licenseTerminal.id, {
        expires_at: dateStr,
        is_active: Boolean(values.is_active),
        renewal_enabled: Boolean(values.renewal_enabled),
      });
      message.success(`Лицензия для терминала #${licenseTerminal.device_id} обновлена`);
      setLicenseModalOpen(false);
      fetchTerminals();
    } catch (err: any) {
      message.error(err.message || "Ошибка установки лицензии");
    } finally {
      setLicenseSubmitting(false);
    }
  };

  // --- Status Toggle Handler ---
  const handleToggleStatus = async (term: AdminTerminal, newStatus: boolean) => {
    try {
      await setTerminalStatus(term.id, newStatus);
      message.success(
        `Состояние терминала #${term.device_id} изменено на: ${newStatus ? "Активен" : "Отключен"}`
      );
      fetchTerminals();
    } catch (err: any) {
      message.error(err.message || "Ошибка изменения состояния терминала");
    }
  };

  // --- Leo4 IoT Provisioning Handlers ---
  const handleOpenProvisionModal = (term: AdminTerminal) => {
    setProvisioningTerminal(term);
    setProvisionModalOpen(true);
  };

  const handleConfirmProvision = async () => {
    if (!provisioningTerminal) return;
    setProvisionSubmitting(true);
    try {
      const res = await provisionTerminalToIot(provisioningTerminal.id);
      if (res.success) {
        message.success(
          `Терминал #${provisioningTerminal.device_id} успешно зарегистрирован в Leo4 IoT`
        );
        setTerminals((prev) =>
          prev.map((t) =>
            t.id === provisioningTerminal.id
              ? {
                  ...t,
                  iot_provisioned: true,
                  iot_provisioned_at: res.iot_provisioned_at || new Date().toISOString(),
                  iot_is_online: res.iot_is_online,
                }
              : t
          )
        );
        setProvisionModalOpen(false);
      } else {
        message.error(`Ошибка провиженинга: ${res.error || "Неизвестная ошибка"}`);
      }
    } catch (err: any) {
      message.error(
        `Не удалось выполнить провиженинг: ${
          err?.response?.data?.detail || err.message || "Ошибка соединения"
        }`
      );
    } finally {
      setProvisionSubmitting(false);
    }
  };

  const handleBatchProvision = async () => {
    if (selectedRowKeys.length === 0) return;
    setBatchProvisionLoading(true);
    try {
      const res = await provisionBatchTerminalsToIot(selectedRowKeys as number[]);
      message.success(`Провиженинг выполнен для ${res.results.length} терминалов`);
      fetchTerminals();
      setSelectedRowKeys([]);
    } catch (err: any) {
      message.error(
        `Ошибка массового провиженинга: ${
          err?.response?.data?.detail || err.message || "Ошибка соединения"
        }`
      );
    } finally {
      setBatchProvisionLoading(false);
    }
  };

  const copyToClipboard = (text: string) => {
    navigator.clipboard.writeText(text);
    message.success("Скопировано в буфер обмена");
  };

  const columns: ColumnsType<AdminTerminal> = [
    {
      title: "№ / Device ID",
      dataIndex: "device_id",
      key: "device_id",
      width: 105,
      sorter: (a, b) => a.device_id - b.device_id,
      render: (val) => (
        <span style={{ whiteSpace: "nowrap" }}>
          <Text strong style={{ fontSize: 13 }}>#{val}</Text>
        </span>
      ),
    },
    {
      title: "Серийный номер (SN)",
      dataIndex: "sn",
      key: "sn",
      width: 190,
      render: (sn) => (
        <span style={{ whiteSpace: "nowrap" }}>
          <Text code copyable={{ text: sn }} style={{ fontSize: 11 }}>
            {sn}
          </Text>
        </span>
      ),
    },
    {
      title: "Организация",
      dataIndex: "org_name",
      key: "org_name",
      width: 170,
      render: (orgName, record) => (
        <div style={{ maxWidth: 160, wordBreak: "break-word", lineHeight: 1.35 }}>
          <div><Text strong style={{ fontSize: 12 }}>{orgName || `Организация #${record.org_id}`}</Text></div>
          <Text type="secondary" style={{ fontSize: 11, whiteSpace: "nowrap" }}>ID: {record.org_id}</Text>
        </div>
      ),
    },
    {
      title: "Тип",
      dataIndex: "terminal_type_name",
      key: "terminal_type_name",
      width: 140,
      render: (typeName, record) => {
        const typeId = record.terminal_type_id;
        const label = typeName || "Стандартный";
        return (
          <Tag color="geekblue" style={{ margin: 0, whiteSpace: "nowrap" }}>
            {typeId !== undefined && typeId !== null ? `${typeId}: ${label}` : label}
          </Tag>
        );
      },
    },
    {
      title: "Состояние",
      dataIndex: "is_active",
      key: "is_active",
      width: 130,
      render: (active, record) => (
        <Space direction="vertical" size={2} style={{ whiteSpace: "nowrap" }}>
          <Popconfirm
            title={active ? "Отключить терминал?" : "Активировать терминал?"}
            description={`Вы действительно хотите переключить состояние терминала #${record.device_id}?`}
            onConfirm={() => handleToggleStatus(record, !active)}
            okText="Да"
            cancelText="Отмена"
          >
            <Tag
              color={active ? "success" : "error"}
              style={{ cursor: "pointer", userSelect: "none", margin: 0, whiteSpace: "nowrap" }}
            >
              <Space size={4}>
                <PoweroffOutlined />
                {active ? "Активен" : "Отключен"}
              </Space>
            </Tag>
          </Popconfirm>
          {record.show_in_monitoring === false && (
            <Tooltip title="Скрыт из операционного мониторинга платежей">
              <Tag color="default" style={{ margin: 0, whiteSpace: "nowrap" }}>
                Скрыт в монит.
              </Tag>
            </Tooltip>
          )}
        </Space>
      ),
    },
    {
      title: "Лицензия",
      key: "license",
      width: 170,
      render: (_, record) => {
        if (!record.is_active) {
          return (
            <Tag color="default" style={{ margin: 0, whiteSpace: "nowrap" }}>
              Отключен
            </Tag>
          );
        }
        if (!record.license_expires_at) {
          return <Tag color="default" style={{ margin: 0, whiteSpace: "nowrap" }}>Нет лицензии</Tag>;
        }
        const expDate = new Date(record.license_expires_at);
        const isExpired = expDate < new Date();
        const dateFormatted = expDate.toLocaleDateString("ru-RU", {
          day: "2-digit",
          month: "2-digit",
          year: "numeric",
        });

        return (
          <Space direction="vertical" size={2} style={{ whiteSpace: "nowrap" }}>
            <Tag color={isExpired ? "error" : "success"} style={{ margin: 0, whiteSpace: "nowrap" }}>
              {isExpired ? "Истекла: " : "До: "}
              {dateFormatted}
            </Tag>
          </Space>
        );
      },
    },
    {
      title: "Сертификат / PIN",
      key: "cert_and_pin",
      width: 170,
      render: (_, record) => (
        <Space direction="vertical" size={2} style={{ whiteSpace: "nowrap" }}>
          {record.cert_serial ? (
            <Tooltip title={`Действителен до: ${record.cert_not_valid_after || "—"}`}>
              <Tag color="cyan" style={{ margin: 0, whiteSpace: "nowrap" }}>Сертификат привязан</Tag>
            </Tooltip>
          ) : (
            <Tag color="default" style={{ margin: 0, whiteSpace: "nowrap" }}>Без сертификата</Tag>
          )}

          {record.pending_pin && (
            <Tag
              color="volcano"
              icon={<KeyOutlined />}
              style={{ cursor: "pointer", margin: 0, whiteSpace: "nowrap" }}
              onClick={() => copyToClipboard(record.pending_pin!)}
            >
              PIN: {record.pending_pin}
            </Tag>
          )}
        </Space>
      ),
    },
    {
      title: "Leo4 IoT",
      key: "iot_status",
      width: 170,
      render: (_, record) => {
        if (!record.iot_provisioned) {
          return (
            <Tooltip title="Терминал не зарегистрирован в платформе Leo4 IoT">
              <span style={{ color: "#8c8c8c", whiteSpace: "nowrap" }}>—</span>
            </Tooltip>
          );
        }
        const provDate = record.iot_provisioned_at
          ? new Date(record.iot_provisioned_at).toLocaleDateString("ru-RU", {
              day: "2-digit",
              month: "2-digit",
              year: "numeric",
            })
          : "";
        const provDateTime = record.iot_provisioned_at
          ? new Date(record.iot_provisioned_at).toLocaleString("ru-RU")
          : "";
        const lastConnDateTime = record.iot_last_connected_at
          ? new Date(record.iot_last_connected_at).toLocaleString("ru-RU")
          : "";
        const lastSyncDateTime = record.iot_last_sync_at
          ? new Date(record.iot_last_sync_at).toLocaleString("ru-RU")
          : "";

        return (
          <Tooltip
            title={
              <div style={{ fontSize: 12 }}>
                <div>
                  <strong>Статус Leo4 IoT:</strong>{" "}
                  {record.iot_is_online ? "Онлайн" : "Зарегистрирован (Оффлайн)"}
                </div>
                {provDateTime && (
                  <div>
                    <strong>Дата регистрации:</strong> {provDateTime}
                  </div>
                )}
                {lastConnDateTime && (
                  <div>
                    <strong>Посл. подключение:</strong> {lastConnDateTime}
                  </div>
                )}
                {lastSyncDateTime && (
                  <div>
                    <strong>Посл. синхронизация:</strong> {lastSyncDateTime}
                  </div>
                )}
              </div>
            }
          >
            <Space direction="vertical" size={2} style={{ whiteSpace: "nowrap" }}>
              <Tag
                color={record.iot_is_online ? "success" : "blue"}
                icon={<CloudOutlined />}
                style={{ margin: 0, whiteSpace: "nowrap" }}
              >
                {record.iot_is_online ? "Онлайн" : "Зарегистрирован"}
              </Tag>
              {provDate && (
                <Text type="secondary" style={{ fontSize: 11, whiteSpace: "nowrap" }}>
                  Рег: {provDate}
                </Text>
              )}
            </Space>
          </Tooltip>
        );
      },
    },
    {
      title: "Адрес / Примечание",
      key: "info",
      width: 250,
      render: (_, record) => (
        <div style={{ maxWidth: 240, wordBreak: "break-word", lineHeight: 1.35 }}>
          {record.address && (
            <div>
              <Text style={{ fontSize: 12 }}>📍 {record.address}</Text>
            </div>
          )}
          {record.note && (
            <div>
              <Text type="secondary" style={{ fontSize: 11 }}>
                📝 {record.note}
              </Text>
            </div>
          )}
          {!record.address && !record.note && <Text type="secondary">—</Text>}
        </div>
      ),
    },
    {
      title: "Действия",
      key: "actions",
      width: 110,
      fixed: "right",
      align: "center",
      render: (_, record) => {
        const menuItems = [
          {
            key: "edit",
            icon: <EditOutlined />,
            label: "Редактировать параметры",
            onClick: () => handleOpenEdit(record),
          },
          {
            key: "goto_device_management",
            icon: <ClusterOutlined />,
            label: "Управление устройством (IoT)",
            onClick: () => navigate(`/devices?device_id=${record.device_id}&sn=${record.sn}`),
          },
          {
            key: "pin",
            icon: <KeyOutlined />,
            label: "Сгенерировать новый PIN",
            onClick: () => handleGeneratePin(record),
          },
          {
            key: "license",
            icon: <CalendarOutlined />,
            label: "Установить дату лицензии",
            onClick: () => handleOpenLicenseModal(record),
          },
          {
            key: "iot_provision",
            icon: <CloudOutlined />,
            label: record.iot_provisioned
              ? "Синхронизировать с Leo4 IoT"
              : "Зарегистрировать в Leo4 IoT",
            onClick: () => handleOpenProvisionModal(record),
          },
          {
            type: "divider" as const,
          },
          {
            key: "toggle",
            icon: <SwapOutlined />,
            label: record.is_active ? "Отключить терминал" : "Включить терминал",
            danger: record.is_active,
            onClick: () => handleToggleStatus(record, !record.is_active),
          },
        ];

        return (
          <Dropdown menu={{ items: menuItems }} trigger={["click"]}>
            <Button size="small">
              Действия <DownOutlined />
            </Button>
          </Dropdown>
        );
      },
    },
  ];

  return (
    <Card bordered={false} style={{ borderRadius: 8 }}>
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
        <div>
          <Title level={4} style={{ margin: 0 }}>
            Реестр и администрирование терминалов
          </Title>
          <Text type="secondary">
            Управление парком устройств, генерация PIN-кодов, назначение лицензий и переключение состояний
          </Text>
        </div>
        <Button
          type="primary"
          icon={<PlusOutlined />}
          onClick={handleOpenCreate}
        >
          Создать терминал
        </Button>
      </div>

      {/* Filter Toolbar */}
      <div
        style={{
          display: "flex",
          flexWrap: "wrap",
          gap: 10,
          marginBottom: 16,
          padding: "12px",
          background: "#fafafa",
          borderRadius: 6,
          alignItems: "center",
        }}
      >
        <Select
          placeholder="Выберите организацию"
          value={selectedOrgId}
          onChange={(val) => {
            if (val !== undefined) {
              setSelectedOrgId(val);
              setPage(1);
            }
          }}
          style={{ width: 240 }}
          showSearch
          optionFilterProp="label"
          options={orgs.map((o) => ({
            value: o.org_id,
            label: `${o.org_name} (#${o.org_id})`,
          }))}
        />

        <Select
          placeholder="Все состояния"
          allowClear
          value={selectedStatus}
          onChange={(val) => {
            setSelectedStatus(val);
            setPage(1);
          }}
          style={{ width: 150 }}
          options={[
            { value: undefined, label: "Все состояния" },
            { value: true, label: "Только активные" },
            { value: false, label: "Только отключенные" },
          ]}
        />

        <Input
          placeholder="Поиск (номер, список ID через запятую, SN, адрес, орг)..."
          prefix={<SearchOutlined />}
          value={searchInput}
          onChange={(e) => setSearchInput(e.target.value)}
          onPressEnter={handleSearch}
          style={{ width: 340 }}
          allowClear
        />

        <Button type="primary" ghost icon={<SearchOutlined />} onClick={handleSearch}>
          Найти
        </Button>

        {(selectedStatus !== undefined || searchQuery) && (
          <Button type="link" onClick={handleResetFilters} style={{ padding: 0 }}>
            Сбросить фильтры
          </Button>
        )}

        {selectedRowKeys.length > 0 && (
          <Button
            type="primary"
            icon={<CloudUploadOutlined />}
            loading={batchProvisionLoading}
            onClick={handleBatchProvision}
          >
            Провиженинг в Leo4 IoT ({selectedRowKeys.length})
          </Button>
        )}
      </div>

      <Table
        rowKey="id"
        rowSelection={{
          selectedRowKeys,
          onChange: (keys: React.Key[]) => setSelectedRowKeys(keys),
        }}
        loading={loading}
        columns={columns}
        dataSource={terminals}
        pagination={{
          position: ["topRight", "bottomRight"],
          current: page,
          pageSize,
          total,
          defaultPageSize: 50,
          showSizeChanger: true,
          pageSizeOptions: ["10", "20", "50", "100"],
          onChange: (p, ps) => {
            setPage(p);
            setPageSize(ps);
          },
          showTotal: (t) => `Всего терминалов: ${t}`,
        }}
        size="middle"
        scroll={{ x: 1600 }}
      />

      {/* Modal: Create Terminal */}
      <Modal
        title="Создание нового терминала"
        open={createModalOpen}
        onCancel={() => setCreateModalOpen(false)}
        onOk={() => createForm.submit()}
        confirmLoading={createSubmitting}
        okText="Создать терминал"
        cancelText="Отмена"
        width={560}
        destroyOnClose
      >
        <Form
          form={createForm}
          layout="vertical"
          onFinish={handleCreateSubmit}
          style={{ marginTop: 16 }}
        >
          <Alert
            type="info"
            showIcon
            message="Автоматическая генерация серийного номера"
            description="Серийный номер (SN) будет сгенерирован автоматически по формуле платформы при сохранении формы."
            style={{ marginBottom: 16 }}
          />

          <div style={{ display: "grid", gridTemplateColumns: "1fr auto", gap: 8, alignItems: "flex-start" }}>
            <Form.Item
              name="device_id"
              label="Номер терминала (Device ID)"
              tooltip="Числовой номер терминала (до 7 цифр: 1..9999999)"
              rules={[
                { required: true, message: "Введите или сгенерируйте номер терминала" },
                {
                  type: "number",
                  min: 1,
                  max: 9999999,
                  message: "Номер должен содержать не более 7 цифр (1..9999999)",
                },
              ]}
            >
              <InputNumber
                placeholder="1..9999999"
                min={1}
                max={9999999}
                style={{ width: "100%" }}
              />
            </Form.Item>
            <div style={{ paddingTop: 30 }}>
              <Button
                onClick={handleGenerateNextDeviceId}
                loading={generatingDeviceId}
                type="dashed"
              >
                Свободный номер
              </Button>
            </div>
          </div>

          <Form.Item
            name="org_id"
            label="Организация (владелец)"
            rules={[{ required: true, message: "Выберите организацию" }]}
          >
            <Select
              placeholder="Выберите организацию"
              showSearch
              optionFilterProp="label"
              options={orgs.map((o) => ({
                value: o.org_id,
                label: `${o.org_name} (#${o.org_id})`,
              }))}
            />
          </Form.Item>

          <Form.Item name="terminal_type_id" label="Тип терминала">
            <Select
              options={
                terminalTypes.length > 0
                  ? terminalTypes.map((t) => ({
                      value: t.id,
                      label: `${t.id}: ${t.name}`,
                    }))
                  : [{ value: 0, label: "0: Стандартный терминал" }]
              }
            />
          </Form.Item>

          <Form.Item name="address" label="Адрес установки">
            <Input.TextArea rows={2} placeholder="г. Москва, ул. Ленина, д. 1..." />
          </Form.Item>

          <Form.Item name="note" label="Примечание / комментарий">
            <Input placeholder="Любая служебная информация..." />
          </Form.Item>

          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Form.Item
              name="is_active"
              label="Состояние терминала"
              valuePropName="checked"
            >
              <Switch checkedChildren="Активен" unCheckedChildren="Отключен" />
            </Form.Item>

            <Form.Item
              name="show_in_monitoring"
              label="Показ в Мониторинге"
              valuePropName="checked"
            >
              <Switch checkedChildren="Включен" unCheckedChildren="Скрыт" />
            </Form.Item>
          </div>

          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Form.Item
              name="iot_provisioned"
              label="Провиженинг в Leo4 IoT"
              tooltip="Автоматически регистрирует устройство в бэкенде iot-rpc и RabbitMQ"
              valuePropName="checked"
            >
              <Switch checkedChildren="Включен" unCheckedChildren="Выключен" />
            </Form.Item>

            <Form.Item
              name="license_months"
              label="Первичная лицензия (мес.)"
            >
              <Select>
                <Select.Option value={1}>1 месяц</Select.Option>
                <Select.Option value={3}>3 месяца</Select.Option>
                <Select.Option value={6}>6 месяцев</Select.Option>
                <Select.Option value={12}>1 год (12 мес.)</Select.Option>
                <Select.Option value={24}>2 года</Select.Option>
              </Select>
            </Form.Item>
          </div>
        </Form>
      </Modal>

      {/* Modal: Edit Terminal */}
      <Modal
        title={`Редактирование терминала #${editingTerminal?.device_id}`}
        open={editModalOpen}
        onCancel={() => setEditModalOpen(false)}
        onOk={() => editForm.submit()}
        confirmLoading={editSubmitting}
        okText="Сохранить изменения"
        cancelText="Отмена"
        width={560}
        destroyOnClose
      >
        <Form
          form={editForm}
          layout="vertical"
          onFinish={handleEditSubmit}
          style={{ marginTop: 16 }}
        >
          {/* Key & Validation parameters: Read-only display */}
          <div
            style={{
              background: "#f5f5f5",
              padding: 12,
              borderRadius: 6,
              marginBottom: 16,
              fontSize: 13,
            }}
          >
            <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 8 }}>
              <div>
                <Text type="secondary">Номер (Device ID):</Text>{" "}
                <Text strong>#{editingTerminal?.device_id}</Text>
              </div>
              <div>
                <Text type="secondary">Серийный номер (SN):</Text>{" "}
                <Text code>{editingTerminal?.sn}</Text>
              </div>
              <div>
                <Text type="secondary">Сертификат (Serial):</Text>{" "}
                <Text>{editingTerminal?.cert_serial || "—"}</Text>
              </div>
              <div>
                <Text type="secondary">Действие сертификата:</Text>{" "}
                <Text>{editingTerminal?.cert_not_valid_after || "—"}</Text>
              </div>
            </div>
          </div>

          <Form.Item
            name="org_id"
            label="Организация"
            rules={[{ required: true, message: "Выберите организацию" }]}
          >
            <Select
              showSearch
              optionFilterProp="label"
              options={orgs.map((o) => ({
                value: o.org_id,
                label: `${o.org_name} (#${o.org_id})`,
              }))}
            />
          </Form.Item>

          <Form.Item name="terminal_type_id" label="Тип терминала">
            <Select
              options={
                terminalTypes.length > 0
                  ? terminalTypes.map((t) => ({
                      value: t.id,
                      label: `${t.id}: ${t.name}`,
                    }))
                  : [{ value: 0, label: "0: Стандартный" }]
              }
            />
          </Form.Item>

          <Form.Item name="address" label="Адрес установки">
            <Input.TextArea rows={2} />
          </Form.Item>

          <Form.Item name="note" label="Примечание">
            <Input />
          </Form.Item>

          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Form.Item
              name="is_active"
              label="Состояние терминала"
              valuePropName="checked"
            >
              <Switch checkedChildren="Активен" unCheckedChildren="Отключен" />
            </Form.Item>

            <Form.Item
              name="show_in_monitoring"
              label="Показ в Мониторинге"
              valuePropName="checked"
            >
              <Switch checkedChildren="Включен" unCheckedChildren="Скрыт" />
            </Form.Item>
          </div>

          <Form.Item
            name="iot_provisioned"
            label="Провиженинг в Leo4 IoT"
            tooltip="При включении синхронно регистрирует устройство в бэкенде iot-rpc"
            valuePropName="checked"
          >
            <Switch checkedChildren="Зарегистрирован" unCheckedChildren="Не зарегистрирован" />
          </Form.Item>

          <Divider style={{ margin: "16px 0 12px" }}>
            Настройки лицензии терминала
          </Divider>

          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Form.Item
              name="billing_period_months"
              label="Период биллинга (мес.)"
            >
              <InputNumber min={1} max={36} style={{ width: "100%" }} />
            </Form.Item>
            <Form.Item
              name="renewal_enabled"
              label="Автопродление лицензии"
              valuePropName="checked"
            >
              <Switch checkedChildren="Включено" unCheckedChildren="Выключено" />
            </Form.Item>
          </div>

          <Form.Item
            name="monthly_price_override_rub"
            label="Индивидуальная цена лицензии (₽) (опционально)"
            tooltip="Переопределяет стандартную стоимость организации для данного терминала"
          >
            <InputNumber
              placeholder="По умолчанию из организации"
              min={0}
              step={100}
              style={{ width: "100%" }}
            />
          </Form.Item>
        </Form>
      </Modal>

      {/* Modal: Set License Date */}
      <Modal
        title={`Установка даты лицензии для терминала #${licenseTerminal?.device_id}`}
        open={licenseModalOpen}
        onCancel={() => setLicenseModalOpen(false)}
        onOk={() => licenseForm.submit()}
        confirmLoading={licenseSubmitting}
        okText="Применить дату"
        cancelText="Отмена"
        width={440}
        destroyOnClose
      >
        <Form
          form={licenseForm}
          layout="vertical"
          onFinish={handleSetLicenseSubmit}
          style={{ marginTop: 16 }}
        >
          {licenseTerminal?.license_expires_at && (
            <div style={{ marginBottom: 12 }}>
              <Text type="secondary">Текущий срок действия: </Text>
              <Text strong>
                {new Date(licenseTerminal.license_expires_at).toLocaleDateString("ru-RU")}
              </Text>
            </div>
          )}

          <Form.Item
            name="expires_at"
            label="Новая дата окончания лицензии"
            rules={[{ required: true, message: "Выберите дату окончания лицензии" }]}
          >
            <DatePicker style={{ width: "100%" }} format="DD.MM.YYYY" />
          </Form.Item>

          <Form.Item
            name="is_active"
            label="Активность лицензии"
            valuePropName="checked"
          >
            <Switch checkedChildren="Активна" unCheckedChildren="Приостановлена" />
          </Form.Item>

          <Form.Item
            name="renewal_enabled"
            label="Разрешить автопродление"
            valuePropName="checked"
          >
            <Switch checkedChildren="Да" unCheckedChildren="Нет" />
          </Form.Item>
        </Form>
      </Modal>

      {/* Modal: PIN Generated Display */}
      <Modal
        title="Сгенерирован новый PIN-код"
        open={pinModalOpen}
        onCancel={() => setPinModalOpen(false)}
        footer={[
          <Button key="close" type="primary" onClick={() => setPinModalOpen(false)}>
            Понятно
          </Button>,
        ]}
        width={440}
      >
        <div style={{ textAlign: "center", padding: "16px 0" }}>
          <Text type="secondary">
            PIN-код для привязки сертификата терминала #{generatedPinData?.device_id}:
          </Text>
          <div
            style={{
              fontSize: 32,
              fontWeight: 700,
              letterSpacing: 4,
              color: "#1677ff",
              margin: "12px 0",
              background: "#e6f4ff",
              padding: "12px 24px",
              borderRadius: 8,
              display: "inline-block",
            }}
          >
            {generatedPinData?.pin}
          </div>
          <div>
            <Button
              icon={<CopyOutlined />}
              onClick={() => copyToClipboard(generatedPinData?.pin || "")}
            >
              Скопировать PIN-код
            </Button>
          </div>
          {generatedPinData?.expires_at && (
            <Paragraph type="secondary" style={{ marginTop: 12, fontSize: 12 }}>
              Срок действия: до{" "}
              {new Date(generatedPinData.expires_at).toLocaleString("ru-RU")}
            </Paragraph>
          )}
        </div>
      </Modal>

      {/* Modal: Leo4 IoT Provisioning Confirmation */}
      <Modal
        title="Провиженинг в Leo4 IoT Platform"
        open={provisionModalOpen}
        onCancel={() => setProvisionModalOpen(false)}
        onOk={handleConfirmProvision}
        confirmLoading={provisionSubmitting}
        okText="Подтвердить провиженинг"
        cancelText="Отмена"
        width={500}
        destroyOnClose
      >
        {provisioningTerminal && (
          <div style={{ marginTop: 12 }}>
            <Alert
              type="info"
              showIcon
              message="Регистрация в Leo4 IoT Platform"
              description="Терминал будет зарегистрирован в платформе Leo4 IoT с автоматическим созданием прав доступа для подключения к брокеру RabbitMQ по mTLS. Общий CA сертификатов обеспечивает сквозную взаимную аутентификацию по SN."
              style={{ marginBottom: 16 }}
            />
            <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
              <div>
                <Text type="secondary">Номер терминала (OU / device_id): </Text>
                <Text strong>#{provisioningTerminal.device_id}</Text>
              </div>
              <div>
                <Text type="secondary">Серийный номер (SN / CN): </Text>
                <Text code copyable>{provisioningTerminal.sn}</Text>
              </div>
              <div>
                <Text type="secondary">Организация: </Text>
                <Text strong>{provisioningTerminal.org_name || `ID ${provisioningTerminal.org_id}`}</Text>
              </div>
              <div>
                <Text type="secondary">Статус сертификата: </Text>
                {provisioningTerminal.cert_serial ? (
                  <Tag color="cyan">Привязан (до {provisioningTerminal.cert_not_valid_after || "—"})</Tag>
                ) : (
                  <Tag color="default">Не привязан (будет авторизован после ввода PIN)</Tag>
                )}
              </div>
              {provisioningTerminal.iot_provisioned && (
                <div>
                  <Text type="secondary">Текущий статус в Leo4: </Text>
                  <Tag color="blue">Уже зарегистрирован (будут обновлены права ACL)</Tag>
                </div>
              )}
            </div>
          </div>
        )}
      </Modal>
    </Card>
  );
}
