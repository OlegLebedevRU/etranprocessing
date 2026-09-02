import React, { useState, useEffect, useMemo, useCallback } from "react";
import {
  Modal,
  Form,
  Select,
  InputNumber,
  Input,
  Alert,
  message,
  Divider,
  Button,
  Space,
  Typography,
  Tag,
  Tooltip,
  Card,
  Spin,
} from "antd";
import {
  ReloadOutlined,
  SendOutlined,
  CheckCircleOutlined,
  ClockCircleOutlined,
  SyncOutlined,
  CloseCircleOutlined,
  PlusOutlined,
  DeleteOutlined,
  InfoCircleOutlined,
  ArrowLeftOutlined,
} from "@ant-design/icons";
import {
  getAvailableMethods,
  getMethodDefinition,
  CUSTOM_METHOD_CODE,
  generateExtTaskId,
  buildTaskCreatePayload,
  getTaskStatusInfo,
  extractTaskResults,
  formatTimestamp,
  TaskStatus,
  type MethodDefinition,
  type DeviceContext,
  type TaskCreatePayload,
} from "./domain";
import {
  createDeviceTask,
  getTaskDetail,
  type TaskItem,
  type TaskCreateInput,
} from "../../api/devices";

const { Text, Paragraph } = Typography;

interface CreateTaskModalProps {
  open: boolean;
  onCancel: () => void;
  onSuccess: () => void;
  deviceId: number;
  sn: string;
  orgId: number;
  deviceContext?: Partial<DeviceContext>;
}

export default function CreateTaskModal({
  open,
  onCancel,
  onSuccess,
  deviceId,
  sn,
  orgId,
  deviceContext,
}: CreateTaskModalProps) {
  const [form] = Form.useForm();
  const [submitting, setSubmitting] = useState(false);
  const [checkingResult, setCheckingResult] = useState(false);

  // Фаза: "form" (настройка и отправка) или "result" (просмотр созданной задачи и проверка ответа)
  const [phase, setPhase] = useState<"form" | "result">("form");

  // Сохраненная созданная задача
  const [createdTaskId, setCreatedTaskId] = useState<string | null>(null);
  const [taskDetail, setTaskDetail] = useState<TaskItem | null>(null);

  // Выбранный метод
  const [selectedMethodCode, setSelectedMethodCode] = useState<number>(20);

  // Доступные методы с учетом контекста
  const availableMethods = useMemo(() => {
    const ctx: DeviceContext = {
      deviceId,
      sn,
      orgId,
      ...deviceContext,
    };
    return getAvailableMethods(ctx);
  }, [deviceId, sn, orgId, deviceContext]);

  const selectedMethod: MethodDefinition = useMemo(() => {
    return getMethodDefinition(selectedMethodCode) || availableMethods[0];
  }, [selectedMethodCode, availableMethods]);

  // Состояние формы для Live JSON Preview
  const [formValues, setFormValues] = useState<any>({
    method_code: 20,
    priority: 0,
    ttl: 60,
    ext_task_id: "",
    mt: 4,
  });

  // Инициализация при открытии модального окна
  useEffect(() => {
    if (open) {
      const initialExtTaskId = generateExtTaskId();
      const initialValues: Record<string, any> = {
        method_code: 20,
        custom_method_code: 100,
        priority: 0,
        ttl: 60,
        ext_task_id: initialExtTaskId,
        mt: 4,
        action: "show",
        chunk_size: 1024,
        t: "str",
        ns: "cfg_eth",
        items: [{ cd: "", cl: 1, ns: "cfg_eth", k: "", t: "str", v: "" }],
      };
      form.setFieldsValue(initialValues);
      setFormValues(initialValues);
      setSelectedMethodCode(20);
      setPhase("form");
      setCreatedTaskId(null);
      setTaskDetail(null);
    }
  }, [open, form]);

  const handleValuesChange = (_changed: any, all: any) => {
    setFormValues({ ...all });
    if (_changed.method_code !== undefined) {
      setSelectedMethodCode(Number(_changed.method_code));
    }
  };

  const handleRegenerateExtTaskId = () => {
    const newId = generateExtTaskId();
    form.setFieldsValue({ ext_task_id: newId });
    setFormValues((prev: any) => ({ ...prev, ext_task_id: newId }));
  };

  // Live JSON Packet Preview
  const previewPayload = useMemo(() => {
    try {
      return buildTaskCreatePayload({
        deviceId,
        method: selectedMethod,
        customMethodCode: Number(formValues.custom_method_code || 0),
        extTaskId: formValues.ext_task_id,
        priority: formValues.priority,
        ttl: formValues.ttl,
        formValues,
      });
    } catch {
      return {
        ext_task_id: formValues.ext_task_id || "",
        device_id: deviceId,
        method_code: selectedMethod.code === CUSTOM_METHOD_CODE ? Number(formValues.custom_method_code || 0) : selectedMethod.code,
        priority: Number(formValues.priority || 0),
        ttl: Number(formValues.ttl || 60),
        payload: { dt: [] },
      };
    }
  }, [deviceId, selectedMethod, formValues]);

  const previewJsonString = useMemo(() => {
    return JSON.stringify(previewPayload, null, 2);
  }, [previewPayload]);

  // Отправка задачи
  const handleSubmit = async (values: any) => {
    setSubmitting(true);
    try {
      const payload: TaskCreatePayload = buildTaskCreatePayload({
        deviceId,
        method: selectedMethod,
        customMethodCode: Number(values.custom_method_code || 0),
        extTaskId: values.ext_task_id,
        priority: values.priority,
        ttl: values.ttl,
        formValues: values,
      });

      const input: TaskCreateInput = {
        ext_task_id: payload.ext_task_id,
        device_id: payload.device_id,
        method_code: payload.method_code,
        priority: payload.priority,
        ttl: payload.ttl,
        payload: payload.payload,
      };

      const res = await createDeviceTask(orgId, input);
      message.success(`Команда #${payload.method_code} успешно поставлена в очередь`);

      const taskId = res.id || payload.ext_task_id;
      setCreatedTaskId(taskId);
      setPhase("result");

      // Сразу загружаем начальные детали задачи
      try {
        const detail = await getTaskDetail(orgId, taskId);
        setTaskDetail(detail);
      } catch {
        setTaskDetail({
          id: taskId,
          ext_task_id: payload.ext_task_id,
          device_id: deviceId,
          method_code: payload.method_code,
          status: TaskStatus.READY,
          priority: payload.priority,
          ttl: payload.ttl,
          payload: payload.payload,
          created_at: res.created_at || Math.floor(Date.now() / 1000),
        });
      }
    } catch (err: any) {
      message.error(err.message || "Ошибка отправки команды");
    } finally {
      setSubmitting(false);
    }
  };

  // Ручная проверка результата задачи
  const handleCheckResult = useCallback(async () => {
    if (!createdTaskId) return;
    setCheckingResult(true);
    try {
      const detail = await getTaskDetail(orgId, createdTaskId);
      setTaskDetail(detail);
      const statusInfo = getTaskStatusInfo(detail.status);
      if (statusInfo.isFinished) {
        message.info(`Статус задачи: ${statusInfo.label}`);
      } else {
        message.info(`Задача еще в обработке: ${statusInfo.label}`);
      }
    } catch (err: any) {
      message.error(err.message || "Не удалось получить результат задачи");
    } finally {
      setCheckingResult(false);
    }
  }, [orgId, createdTaskId]);

  const handleResetToForm = () => {
    const newExtTaskId = generateExtTaskId();
    form.setFieldsValue({ ext_task_id: newExtTaskId });
    setFormValues((prev: any) => ({ ...prev, ext_task_id: newExtTaskId }));
    setPhase("form");
    setCreatedTaskId(null);
    setTaskDetail(null);
  };

  // Извлечение результатов
  const extractedResults = useMemo(() => {
    return extractTaskResults(taskDetail);
  }, [taskDetail]);

  const taskStatusInfo = useMemo(() => {
    return getTaskStatusInfo(taskDetail?.status ?? TaskStatus.READY);
  }, [taskDetail?.status]);

  const getStatusIcon = (state: string) => {
    switch (state) {
      case "success":
        return <CheckCircleOutlined />;
      case "timeout":
        return <ClockCircleOutlined />;
      case "failed":
        return <CloseCircleOutlined />;
      default:
        return <SyncOutlined spin />;
    }
  };

  return (
    <Modal
      title={
        phase === "form"
          ? `Создание RPC-задачи (Устройство #${deviceId}, SN: ${sn})`
          : `Статус и результат RPC-задачи #${createdTaskId || ""}`
      }
      open={open}
      onCancel={onCancel}
      footer={
        phase === "form" ? (
          <Space>
            <Button onClick={onCancel}>Отмена</Button>
            <Button
              type="primary"
              icon={<SendOutlined />}
              onClick={() => form.submit()}
              loading={submitting}
            >
              Отправить команду
            </Button>
          </Space>
        ) : (
          <Space>
            <Button icon={<ArrowLeftOutlined />} onClick={handleResetToForm}>
              Создать еще команду
            </Button>
            <Button
              type="primary"
              onClick={() => {
                onSuccess();
              }}
            >
              Готово
            </Button>
          </Space>
        )
      }
      width={720}
      style={{ maxWidth: "calc(100vw - 16px)", top: 16 }}
      destroyOnClose
    >
      {phase === "form" ? (
        <Form
          form={form}
          layout="vertical"
          onFinish={handleSubmit}
          onValuesChange={handleValuesChange}
          style={{ marginTop: 12 }}
        >
          {/* Выбор метода */}
          <Form.Item
            name="method_code"
            label="RPC Метод / Команда"
            rules={[{ required: true, message: "Выберите метод" }]}
          >
            <Select
              showSearch
              optionFilterProp="label"
              options={availableMethods.map((m) => ({
                value: m.code,
                label: m.label,
              }))}
            />
          </Form.Item>

          {/* Описание выбранного метода */}
          {selectedMethod && (
            <Alert
              type="info"
              showIcon
              icon={<InfoCircleOutlined />}
              message={selectedMethod.description}
              style={{ marginBottom: 16 }}
            />
          )}

          {/* Поле ввода произвольного номера метода при Custom JSON */}
          {selectedMethod.code === CUSTOM_METHOD_CODE && (
            <Form.Item
              name="custom_method_code"
              label="Числовой код метода (method_code)"
              tooltip="Укажите числовой код RPC-метода (0..65535)"
              rules={[{ required: true, message: "Укажите код метода" }]}
            >
              <InputNumber min={0} max={65535} style={{ width: "100%" }} placeholder="Например, 100" />
            </Form.Item>
          )}

          {/* Динамические поля метода */}
          {/* 1. Метод 16 с Form.List */}
          {selectedMethod.code === 16 && (
            <Form.List name="items">
              {(fields, { add, remove }) => (
                <div style={{ marginBottom: 16 }}>
                  <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 8 }}>
                    <Text strong>Список привязок карт/PIN к слотам:</Text>
                    <Button type="dashed" size="small" icon={<PlusOutlined />} onClick={() => add({ cd: "", cl: 1 })}>
                      Добавить строку
                    </Button>
                  </div>
                  {fields.map(({ key, name, ...restField }) => (
                    <Space key={key} style={{ display: "flex", marginBottom: 8 }} align="baseline">
                      <Form.Item
                        {...restField}
                        name={[name, "cd"]}
                        rules={[{ required: true, message: "Введите ID карты / PIN" }]}
                        style={{ marginBottom: 0, minWidth: 260 }}
                      >
                        <Input placeholder="ID карты / PIN-код (cd)" />
                      </Form.Item>
                      <Form.Item
                        {...restField}
                        name={[name, "cl"]}
                        rules={[{ required: true, message: "Слот 1..255" }]}
                        style={{ marginBottom: 0, width: 140 }}
                      >
                        <InputNumber min={1} max={255} placeholder="Слот (cl)" style={{ width: "100%" }} />
                      </Form.Item>
                      {fields.length > 1 && (
                        <Button type="text" danger icon={<DeleteOutlined />} onClick={() => remove(name)} />
                      )}
                    </Space>
                  ))}
                </div>
              )}
            </Form.List>
          )}

          {/* 2. Метод 49 (NVS) с Form.List */}
          {selectedMethod.code === 49 && (
            <Form.List name="items">
              {(fields, { add, remove }) => (
                <div style={{ marginBottom: 16 }}>
                  <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 8 }}>
                    <Text strong>Параметры записи в NVS:</Text>
                    <Button type="dashed" size="small" icon={<PlusOutlined />} onClick={() => add({ ns: "cfg_eth", k: "", t: "str", v: "" })}>
                      Добавить параметр
                    </Button>
                  </div>
                  {fields.map(({ key, name, ...restField }) => (
                    <div
                      key={key}
                      style={{
                        padding: 10,
                        background: "#fafafa",
                        border: "1px solid #f0f0f0",
                        borderRadius: 6,
                        marginBottom: 8,
                      }}
                    >
                      <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr 140px 1fr auto", gap: 8, alignItems: "center" }}>
                        <Form.Item
                          {...restField}
                          name={[name, "ns"]}
                          rules={[{ required: true, message: "Раздел" }]}
                          style={{ marginBottom: 0 }}
                        >
                          <Input placeholder="Раздел (ns)" />
                        </Form.Item>
                        <Form.Item
                          {...restField}
                          name={[name, "k"]}
                          rules={[{ required: true, message: "Ключ" }]}
                          style={{ marginBottom: 0 }}
                        >
                          <Input placeholder="Ключ (k)" />
                        </Form.Item>
                        <Form.Item
                          {...restField}
                          name={[name, "t"]}
                          rules={[{ required: true, message: "Тип" }]}
                          style={{ marginBottom: 0 }}
                        >
                          <Select
                            options={[
                              { label: "str", value: "str" },
                              { label: "i8", value: "i8" },
                              { label: "u8", value: "u8" },
                              { label: "i16", value: "i16" },
                              { label: "u16", value: "u16" },
                              { label: "i32", value: "i32" },
                              { label: "u32", value: "u32" },
                            ]}
                          />
                        </Form.Item>
                        <Form.Item
                          {...restField}
                          name={[name, "v"]}
                          rules={[{ required: true, message: "Значение" }]}
                          style={{ marginBottom: 0 }}
                        >
                          <Input placeholder="Значение (v)" />
                        </Form.Item>
                        {fields.length > 1 && (
                          <Button type="text" danger icon={<DeleteOutlined />} onClick={() => remove(name)} />
                        )}
                      </div>
                    </div>
                  ))}
                </div>
              )}
            </Form.List>
          )}

          {/* 3. Обычные поля для других методов */}
          {selectedMethod.fields &&
            selectedMethod.code !== 16 &&
            selectedMethod.code !== 49 &&
            selectedMethod.fields.map((field) => {
              const rules: any[] = [];
              if (field.required) {
                rules.push({ required: true, message: `Поле ${field.label} обязательно` });
              }
              if (field.validation?.regex) {
                rules.push({
                  pattern: new RegExp(field.validation.regex),
                  message: field.validation.regexMessage || "Некорректный формат значения",
                });
              }

              return (
                <Form.Item
                  key={field.name}
                  name={field.name}
                  label={field.label}
                  tooltip={field.tooltip}
                  rules={rules}
                >
                  {field.type === "select" ? (
                    <Select options={field.options} placeholder={field.placeholder} />
                  ) : field.type === "number" ? (
                    <InputNumber
                      min={field.validation?.min}
                      max={field.validation?.max}
                      style={{ width: "100%" }}
                      placeholder={field.placeholder}
                    />
                  ) : (
                    <Input placeholder={field.placeholder || `Введите ${field.label.toLowerCase()}...`} />
                  )}
                </Form.Item>
              );
            })}

          {/* 4. Поле Custom JSON */}
          {selectedMethod.code === CUSTOM_METHOD_CODE && (
            <Form.Item
              name="params_json"
              label="Полезная нагрузка (Custom JSON для dt)"
              tooltip="Введите JSON массив или объект полезной нагрузки dt"
              rules={[{ required: true, message: "Введите JSON полезной нагрузки" }]}
            >
              <Input.TextArea
                rows={4}
                placeholder='[{"key": "value"}]'
                style={{ fontFamily: "Consolas, Monaco, monospace", fontSize: 13 }}
              />
            </Form.Item>
          )}

          {/* 5. Команды без параметров */}
          {selectedMethod.dtFormat === "empty" && (
            <div style={{ padding: "8px 0", color: "#8c8c8c", fontSize: 13 }}>
              Данная команда не требует дополнительных параметров (отправляется <code>dt: []</code>).
            </div>
          )}

          <Divider style={{ margin: "14px 0" }} />

          {/* Заголовок задачи (Header) */}
          <div style={{ display: "grid", gridTemplateColumns: "repeat(auto-fit, minmax(180px, 1fr))", gap: 12 }}>
            <Form.Item
              name="ext_task_id"
              label="Внешний ID задачи (ext_task_id)"
              tooltip="Уникальный идентификатор задачи (20 символов a-z0-9)"
              rules={[
                { required: true, message: "Укажите ext_task_id" },
                { min: 1, max: 64, message: "Длина от 1 до 64 символов" },
              ]}
            >
              <Input
                addonAfter={
                  <Tooltip title="Сгенерировать новый ID">
                    <Button
                      type="text"
                      size="small"
                      icon={<ReloadOutlined />}
                      onClick={handleRegenerateExtTaskId}
                      style={{ border: "none", padding: 0 }}
                    />
                  </Tooltip>
                }
                style={{ fontFamily: "monospace" }}
              />
            </Form.Item>

            <Form.Item
              name="priority"
              label="Приоритет (0..9)"
              tooltip="0 - обычный, 9 - наивысший приоритет исполнения"
            >
              <InputNumber min={0} max={9} style={{ width: "100%" }} />
            </Form.Item>

            <Form.Item
              name="ttl"
              label="TTL (минуты)"
              tooltip="Срок действия задачи в очереди до перевода в EXPIRED"
            >
              <InputNumber min={1} max={44640} style={{ width: "100%" }} />
            </Form.Item>
          </div>

          {/* Live JSON Packet Preview */}
          <div style={{ marginTop: 8 }}>
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 6 }}>
              <Text type="secondary" style={{ fontSize: 12, fontWeight: 500 }}>
                Предпросмотр отправляемого JSON-пакета (Live Preview):
              </Text>
            </div>
            <Paragraph
              copyable={{ text: previewJsonString }}
              style={{
                backgroundColor: "#1e1e1e",
                color: "#9cdcfe",
                padding: "10px 14px",
                borderRadius: 6,
                fontFamily: "Consolas, Monaco, monospace",
                fontSize: 12,
                maxHeight: 180,
                overflowY: "auto",
                whiteSpace: "pre-wrap",
                marginBottom: 0,
              }}
            >
              {previewJsonString}
            </Paragraph>
          </div>
        </Form>
      ) : (
        /* Фаза 2: Результат выполнения и ручная проверка */
        <div style={{ display: "flex", flexDirection: "column", gap: 16, marginTop: 12 }}>
          <Alert
            type="success"
            showIcon
            message="Задача успешно зарегистрирована в очереди ядра"
            description={
              <span>
                UUID задачи: <strong>{createdTaskId}</strong> | ext_task_id:{" "}
                <code>{taskDetail?.ext_task_id || "—"}</code>
              </span>
            }
          />

          <Card size="small" title="Текущее состояние RPC-вызова">
            <div style={{ display: "grid", gridTemplateColumns: "repeat(auto-fit, minmax(200px, 1fr))", gap: 12 }}>
              <div>
                <Text type="secondary">Метод: </Text>
                <Text strong>{selectedMethod.label}</Text>
              </div>

              <div>
                <Text type="secondary">Статус: </Text>
                <Tag
                  icon={getStatusIcon(taskStatusInfo.state)}
                  color={taskStatusInfo.color}
                  style={{ fontSize: 13, padding: "2px 8px" }}
                >
                  {taskStatusInfo.label}
                </Tag>
              </div>

              <div>
                <Text type="secondary">Создана: </Text>
                <Text>{formatTimestamp(taskDetail?.created_at)}</Text>
              </div>

              <div>
                <Text type="secondary">Принята устройством (ACK): </Text>
                <Text>{taskDetail?.pending_at ? formatTimestamp(taskDetail.pending_at) : "—"}</Text>
              </div>
            </div>

            <Divider style={{ margin: "12px 0" }} />

            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 8 }}>
              <Text strong>Ответ и результаты от устройства:</Text>
              <Button
                type="primary"
                icon={<ReloadOutlined spin={checkingResult} />}
                onClick={handleCheckResult}
                loading={checkingResult}
              >
                Проверить результат
              </Button>
            </div>

            {checkingResult ? (
              <div style={{ textAlign: "center", padding: 24 }}>
                <Spin tip="Опрос статуса задачи..." />
              </div>
            ) : (
              <Paragraph
                copyable={{
                  text: JSON.stringify(
                    extractedResults.hasResults
                      ? extractedResults.resultsList.length > 0
                        ? extractedResults.resultsList
                        : extractedResults.primaryResult
                      : { status: taskStatusInfo.label, message: extractedResults.summary },
                    null,
                    2
                  ),
                }}
                style={{
                  backgroundColor: extractedResults.hasResults ? "#0f1f14" : "#1e1e1e",
                  color: extractedResults.hasResults ? "#73d13d" : "#faad14",
                  border: `1px solid ${extractedResults.hasResults ? "#237804" : "#434343"}`,
                  padding: "10px 14px",
                  borderRadius: 6,
                  fontFamily: "Consolas, Monaco, monospace",
                  fontSize: 12,
                  maxHeight: 200,
                  overflowY: "auto",
                  whiteSpace: "pre-wrap",
                  marginBottom: 0,
                }}
              >
                {extractedResults.hasResults
                  ? JSON.stringify(
                      extractedResults.resultsList.length > 0
                        ? extractedResults.resultsList
                        : extractedResults.primaryResult,
                      null,
                      2
                    )
                  : `/* ${extractedResults.summary} */\n/* Нажмите «Проверить результат», чтобы получить свежий статус */`}
              </Paragraph>
            )}
          </Card>
        </div>
      )}
    </Modal>
  );
}
