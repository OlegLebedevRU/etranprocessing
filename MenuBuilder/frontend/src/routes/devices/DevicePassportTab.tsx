import { useEffect, useState, useCallback } from "react";
import { useNavigate } from "react-router";
import {
  Card,
  Descriptions,
  Badge,
  Tag,
  Button,
  Typography,
  Space,
  Spin,
  Alert,
  message,
  Table,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import {
  ReloadOutlined,
  CopyOutlined,
  LinkOutlined,
  SyncOutlined,
  StopOutlined,
  SafetyCertificateOutlined,
  HistoryOutlined,
} from "@ant-design/icons";
import {
  getDeviceRaw,
  triggerDeviceProvisioning,
  formatPeerHost,
  formatConflictingHosts,
  type DeviceRawResult,
  type DeviceTagItem,
  type DeviceAuditEventItem,
} from "../../api/devices";
import {
  formatTenantDateTime,
  resolveTenantTimezone,
} from "../../utils/timezone";

const { Text } = Typography;

interface DevicePassportTabProps {
  deviceId: number;
  sn: string;
  orgId: number;
}

function formatBytes(bytes?: number): string {
  if (bytes === undefined || bytes === null || isNaN(bytes)) return "—";
  if (bytes === 0) return "0 Б";
  const k = 1024;
  const sizes = ["Б", "КБ", "МБ", "ГБ", "ТБ"];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  if (i <= 0) return `${bytes} Б`;
  return `${parseFloat((bytes / Math.pow(k, i)).toFixed(2))} ${sizes[i]}`;
}

function formatDateTime(dateStrOrNum?: string | number, tz?: string): string {
  if (!dateStrOrNum) return "—";
  return formatTenantDateTime(dateStrOrNum, tz);
}

function formatRelativeTime(dateStrOrNum?: string | number): string {
  if (!dateStrOrNum) return "";
  const date = new Date(dateStrOrNum);
  if (isNaN(date.getTime())) return "";
  const diffSec = Math.floor((Date.now() - date.getTime()) / 1000);
  if (diffSec < 0) return "";
  if (diffSec < 60) return `${diffSec} сек. назад`;
  const diffMin = Math.floor(diffSec / 60);
  if (diffMin < 60) return `${diffMin} мин. назад`;
  const diffHours = Math.floor(diffMin / 60);
  if (diffHours < 24) return `${diffHours} ч. назад`;
  const diffDays = Math.floor(diffHours / 24);
  return `${diffDays} дн. назад`;
}

function getTagValue(
  tags: DeviceTagItem[] | undefined,
  tagName: string
): string | undefined {
  const item = tags?.find(
    (t) => t.tag.toLowerCase() === tagName.toLowerCase()
  );
  return item?.value;
}

function renderSystemPlatform(sys?: string) {
  if (!sys) return <Text type="secondary">—</Text>;
  const lower = sys.toLowerCase();
  let color = "geekblue";
  if (lower.includes("win")) {
    color = "blue";
  } else if (
    lower.includes("linux") ||
    lower.includes("debian") ||
    lower.includes("ubuntu")
  ) {
    color = "green";
  } else if (
    lower.includes("esp") ||
    lower.includes("mcu") ||
    lower.includes("stm") ||
    lower.includes("freertos")
  ) {
    color = "orange";
  } else if (lower.includes("android")) {
    color = "cyan";
  }
  return (
    <Tag color={color} style={{ fontWeight: 500 }}>
      {sys}
    </Tag>
  );
}

export default function DevicePassportTab({
  deviceId,
  sn,
  orgId,
}: DevicePassportTabProps) {
  const navigate = useNavigate();
  const [loading, setLoading] = useState(false);
  const [unblocking, setUnblocking] = useState(false);
  const [device, setDevice] = useState<DeviceRawResult | null>(null);
  const [error, setError] = useState<string | null>(null);
  const tenantTz = resolveTenantTimezone();

  const fetchDeviceData = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const data = await getDeviceRaw(orgId, deviceId);
      setDevice(data);
    } catch (err: any) {
      const errMsg =
        err?.message || "Ошибка загрузки данных паспорта устройства";
      setError(errMsg);
      message.error(errMsg);
    } finally {
      setLoading(false);
    }
  }, [orgId, deviceId]);

  useEffect(() => {
    fetchDeviceData();
  }, [fetchDeviceData]);

  const handleUnblock = async () => {
    setUnblocking(true);
    try {
      await triggerDeviceProvisioning(deviceId);
      message.success(
        "Команда повторного провиженинга успешно отправлена. Блокировка снята!"
      );
      await fetchDeviceData();
    } catch (err: any) {
      message.error(
        err?.message || "Не удалось выполнить повторный провиженинг устройства"
      );
    } finally {
      setUnblocking(false);
    }
  };

  const handleCopyJson = async () => {
    if (!device) return;
    try {
      await navigator.clipboard.writeText(JSON.stringify(device, null, 2));
      message.success("JSON скопирован в буфер обмена");
    } catch {
      message.error("Не удалось скопировать JSON в буфер");
    }
  };

  const tags = device?.device_tags;
  const sysTag = getTagValue(tags, "sys");
  const appTag = getTagValue(tags, "app");
  const nameTag = getTagValue(tags, "name");
  const descTag = getTagValue(tags, "description");

  const conn = device?.connection;
  const details = conn?.details;
  const isBlocked = Boolean(conn?.is_blocked);
  const isOnline = Boolean(!isBlocked && conn?.last_checked_result);

  const connectedAt = conn?.connected_at || details?.connected_at;
  const relTime = connectedAt ? formatRelativeTime(connectedAt) : "";

  // Protocol & Encryption string
  const protoParts: string[] = [];
  if (details?.protocol) {
    protoParts.push(details.protocol);
  }
  if (details?.ssl_protocol) {
    if (details.ssl_cipher) {
      protoParts.push(`${details.ssl_protocol} (${details.ssl_cipher})`);
    } else {
      protoParts.push(details.ssl_protocol);
    }
  } else if (details?.ssl) {
    protoParts.push("SSL/TLS");
  }
  const protocolText = protoParts.length > 0 ? protoParts.join(" / ") : "—";

  // Peer host & port (parse Erlang IPv4-mapped tuples)
  const rawHost = details?.peer_host;
  const parsedHost = rawHost ? formatPeerHost(rawHost) : undefined;
  const peerAddress = parsedHost
    ? `${parsedHost}${details?.peer_port ? `:${details.peer_port}` : ""}`
    : undefined;

  // Collision details
  const violation = conn?.violation_details;
  const violationType = conn?.violation_type || violation?.violation_type;
  const conflictingHosts = formatConflictingHosts(violation?.conflicting_hosts);
  const certValidities = violation?.cert_validities || [];
  const recentAuditEvents: DeviceAuditEventItem[] =
    conn?.recent_audit_events || [];

  const auditColumns: ColumnsType<DeviceAuditEventItem> = [
    {
      title: "Дата и время",
      dataIndex: "created_at",
      key: "created_at",
      width: 170,
      render: (val) => (
        <span style={{ fontSize: 12 }}>{formatDateTime(val, tenantTz)}</span>
      ),
    },
    {
      title: "Событие",
      dataIndex: "event_type",
      key: "event_type",
      width: 170,
      render: (type: string) => {
        let color = "default";
        let label = type;
        if (type === "PROVISIONED") {
          color = "blue";
          label = "PROVISIONED (Регистрация)";
        } else if (type === "SN_COLLISION") {
          color = "volcano";
          label = "SN_COLLISION (Коллизия)";
        } else if (type === "DEVICE_CLONE") {
          color = "red";
          label = "DEVICE_CLONE (Клон)";
        } else if (type === "BLOCKED") {
          color = "error";
          label = "BLOCKED (Блокировка)";
        } else if (type === "UNBLOCKED") {
          color = "success";
          label = "UNBLOCKED (Разблокировка)";
        }
        return (
          <Tag color={color} style={{ margin: 0, fontWeight: 500 }}>
            {label}
          </Tag>
        );
      },
    },
    {
      title: "Инициатор (Actor)",
      dataIndex: "actor",
      key: "actor",
      width: 160,
      render: (actor) => (actor ? <Text code>{actor}</Text> : <Text type="secondary">—</Text>),
    },
    {
      title: "Детали события",
      dataIndex: "details",
      key: "details",
      render: (det) => {
        if (!det) return <Text type="secondary">—</Text>;
        return (
          <code
            style={{
              background: "#f5f5f5",
              padding: "2px 6px",
              borderRadius: 4,
              fontSize: 11,
              wordBreak: "break-all",
              display: "inline-block",
              maxWidth: 420,
            }}
          >
            {typeof det === "object" ? JSON.stringify(det) : String(det)}
          </code>
        );
      },
    },
  ];

  return (
    <Spin spinning={loading && !device}>
      <Space
        direction="vertical"
        size="middle"
        style={{ width: "100%", marginTop: 8 }}
      >
        {error && (
          <Alert
            type="error"
            message={error}
            showIcon
            action={
              <Button
                size="small"
                type="primary"
                danger
                onClick={fetchDeviceData}
              >
                Повторить
              </Button>
            }
          />
        )}

        {/* Security Incident Alert (When is_blocked = true) */}
        {isBlocked && (
          <Alert
            type="error"
            showIcon
            message={
              <Space>
                <StopOutlined />
                <span style={{ fontSize: 15, fontWeight: 600 }}>
                  Зафиксирован инцидент безопасности:{" "}
                  {violationType === "SN_COLLISION"
                    ? "Коллизия серийного номера (SN_COLLISION)"
                    : violationType === "DEVICE_CLONE"
                      ? "Обнаружен клон устройства (DEVICE_CLONE)"
                      : "Устройство заблокировано"}
                </span>
              </Space>
            }
            description={
              <div style={{ marginTop: 8 }}>
                <div style={{ marginBottom: 6 }}>
                  <strong>Причина: </strong>
                  <span>
                    {violation?.reason ||
                      "Обнаружена попытка одновременного подключения нескольких устройств или несовпадающих сертификатов под одним SN."}
                  </span>
                </div>

                {violation?.detected_at && (
                  <div style={{ marginBottom: 4 }}>
                    <strong>Время первого обнаружения: </strong>
                    <span>{formatDateTime(violation.detected_at, tenantTz)}</span>
                  </div>
                )}

                {violation?.flapping_count !== undefined && (
                  <div style={{ marginBottom: 4 }}>
                    <strong>Количество циклов переподключения (Flapping): </strong>
                    <Tag color="volcano">{violation.flapping_count}</Tag>
                  </div>
                )}

                {conflictingHosts.length > 0 && (
                  <div style={{ marginBottom: 4 }}>
                    <strong>Конфликтующие IP-адреса: </strong>
                    <Space size={4} wrap>
                      {conflictingHosts.map((h, i) => (
                        <Tag key={i} color="red">
                          {h}
                        </Tag>
                      ))}
                    </Space>
                  </div>
                )}

                {certValidities.length > 0 && (
                  <div style={{ marginBottom: 6 }}>
                    <strong>Конфликтующие сертификаты: </strong>
                    <Space size={4} wrap>
                      {certValidities.map((cv, i) => (
                        <Tag key={i} color="orange">
                          {cv}
                        </Tag>
                      ))}
                    </Space>
                  </div>
                )}

                <div
                  style={{
                    marginTop: 10,
                    padding: "6px 10px",
                    background: "#fff1f0",
                    border: "1px dashed #ffa39e",
                    borderRadius: 4,
                  }}
                >
                  <Text strong style={{ color: "#cf1322" }}>
                    Рекомендованное действие:{" "}
                  </Text>
                  <Text style={{ fontSize: 12 }}>
                    Отключите старый/дублирующий терминал и выполните повторный
                    провиженинг устройства для автоматического снятия блокировки
                    и сброса инцидента.
                  </Text>
                </div>
              </div>
            }
            action={
              <Button
                type="primary"
                danger
                icon={<SyncOutlined />}
                loading={unblocking}
                onClick={handleUnblock}
              >
                Снять блокировку (Перепровиженить)
              </Button>
            }
          />
        )}

        {/* Блок 1: Основная идентификация и системная платформа */}
        <Descriptions
          title="Основная идентификация и системная платформа"
          bordered
          size="small"
          column={{ xs: 1, sm: 2, md: 2 }}
        >
          <Descriptions.Item label="ID устройства">
            <Space>
              <Text strong>#{deviceId}</Text>
              <Button
                type="link"
                size="small"
                icon={<LinkOutlined />}
                onClick={() => navigate(`/admin/terminals?search=${deviceId}`)}
                style={{ padding: 0 }}
              >
                В Реестр терминалов →
              </Button>
            </Space>
          </Descriptions.Item>

          <Descriptions.Item label="Серийный номер (SN)">
            <Text code copyable={{ text: device?.sn || sn }}>
              {device?.sn || sn}
            </Text>
          </Descriptions.Item>

          <Descriptions.Item label="Организация">
            <Text>#{orgId}</Text>
          </Descriptions.Item>

          <Descriptions.Item label="Системная платформа (sys)">
            {renderSystemPlatform(sysTag)}
          </Descriptions.Item>

          <Descriptions.Item label="Прикладное ПО (app)">
            {appTag ? (
              <Tag color="blue">{appTag}</Tag>
            ) : (
              <Text type="secondary">—</Text>
            )}
          </Descriptions.Item>

          <Descriptions.Item label="Имя / Описание">
            {nameTag || descTag ? (
              <Text>
                {nameTag && <strong>[{nameTag}] </strong>}
                {descTag || ""}
              </Text>
            ) : (
              <Text type="secondary">—</Text>
            )}
          </Descriptions.Item>
        </Descriptions>

        {/* Блок 2: Сетевое подключение, статус MQTT и LWT-сервисы */}
        <Descriptions
          title="Сетевое подключение, статус MQTT и LWT-сервисы"
          bordered
          size="small"
          column={{ xs: 1, sm: 2, md: 2 }}
        >
          <Descriptions.Item label="Статус соединения">
            <Space wrap size={8}>
              {isBlocked ? (
                <Badge
                  status="error"
                  text={
                    <Text strong style={{ color: "#ff4d4f" }}>
                      Заблокировано (Коллизия)
                    </Text>
                  }
                />
              ) : isOnline ? (
                <Badge
                  status="success"
                  text={
                    <Text strong style={{ color: "#52c41a" }}>
                      Онлайн
                    </Text>
                  }
                />
              ) : (
                <Badge
                  status="default"
                  text={
                    <Text strong style={{ color: "#8c8c8c" }}>
                      Оффлайн
                    </Text>
                  }
                />
              )}
              <Tag color={conn?.app_connect ? "success" : "default"}>
                {conn?.app_connect ? "app_online" : "app_offline"}
              </Tag>
              <Tag color={conn?.svc_connect ? "success" : "default"}>
                {conn?.svc_connect ? "svc_online" : "svc_offline"}
              </Tag>
            </Space>
          </Descriptions.Item>

          <Descriptions.Item label="LWT Готовность сервисов">
            <Space wrap size={6}>
              <Tag color={conn?.is_app_available ? "green" : "default"}>
                app_available:{" "}
                {conn?.is_app_available
                  ? "Да"
                  : conn?.is_app_available === false
                    ? "Нет"
                    : "—"}
              </Tag>
              <Tag color={conn?.is_svc_available ? "green" : "default"}>
                svc_available:{" "}
                {conn?.is_svc_available
                  ? "Да"
                  : conn?.is_svc_available === false
                    ? "Нет"
                    : "—"}
              </Tag>
            </Space>
          </Descriptions.Item>

          <Descriptions.Item label="IP-адрес и порт">
            {peerAddress ? (
              <Text code>{peerAddress}</Text>
            ) : (
              <Text type="secondary">—</Text>
            )}
          </Descriptions.Item>

          <Descriptions.Item label="Время подключения">
            {connectedAt ? (
              <Space wrap>
                <Text>{formatDateTime(connectedAt, tenantTz)}</Text>
                {relTime && <Text type="secondary">({relTime})</Text>}
              </Space>
            ) : (
              <Text type="secondary">—</Text>
            )}
          </Descriptions.Item>

          <Descriptions.Item label="Время сверки в БД (checked_at)">
            {conn?.checked_at ? (
              <Text>{formatDateTime(conn.checked_at, tenantTz)}</Text>
            ) : (
              <Text type="secondary">—</Text>
            )}
          </Descriptions.Item>

          <Descriptions.Item label="Протокол и шифрование">
            <Text>{protocolText}</Text>
          </Descriptions.Item>

          <Descriptions.Item label="Сетевой трафик" span={2}>
            <Space size={16} wrap>
              <span>
                ↓ Принято: <strong>{formatBytes(details?.bytes_received)}</strong>
              </span>
              <span>
                ↑ Отправлено: <strong>{formatBytes(details?.bytes_sent)}</strong>
              </span>
            </Space>
          </Descriptions.Item>
        </Descriptions>

        {/* Блок 3: Безопасность и коллизии */}
        <Descriptions
          title={
            <Space>
              <SafetyCertificateOutlined style={{ color: isBlocked ? "#ff4d4f" : "#52c41a" }} />
              <span>Безопасность и статус коллизий</span>
            </Space>
          }
          bordered
          size="small"
          column={{ xs: 1, sm: 2, md: 2 }}
        >
          <Descriptions.Item label="Статус безопасности">
            {isBlocked ? (
              <Tag color="error">
                Заблокировано ({violationType || "Коллизия"})
              </Tag>
            ) : (
              <Tag color="success">В норме (Коллизий не зафиксировано)</Tag>
            )}
          </Descriptions.Item>

          <Descriptions.Item label="Тип инцидента">
            {violationType ? (
              <Tag color="volcano">{violationType}</Tag>
            ) : (
              <Text type="secondary">—</Text>
            )}
          </Descriptions.Item>

          {violation?.reason && (
            <Descriptions.Item label="Причина инцидента" span={2}>
              <Text>{violation.reason}</Text>
            </Descriptions.Item>
          )}

          {conflictingHosts.length > 0 && (
            <Descriptions.Item label="Конфликтующие хосты" span={2}>
              <Space size={4} wrap>
                {conflictingHosts.map((h, i) => (
                  <Tag key={i} color="red">
                    {h}
                  </Tag>
                ))}
              </Space>
            </Descriptions.Item>
          )}

          {certValidities.length > 0 && (
            <Descriptions.Item label="Конфликтующие сертификаты" span={2}>
              <Space size={4} wrap>
                {certValidities.map((cv, i) => (
                  <Tag key={i} color="orange">
                    {cv}
                  </Tag>
                ))}
              </Space>
            </Descriptions.Item>
          )}
        </Descriptions>

        {/* Блок 4: Данные mTLS сертификата */}
        <Descriptions
          title={
            <Space wrap>
              <span>Данные mTLS сертификата</span>
              {!isOnline && (
                <Text
                  type="secondary"
                  style={{ fontSize: 12, fontWeight: "normal" }}
                >
                  (данные зафиксированы в последнем сеансе связи)
                </Text>
              )}
            </Space>
          }
          bordered
          size="small"
          column={1}
        >
          <Descriptions.Item label="Субъект (DN / Subject)">
            {details?.peer_cert_subject ? (
              <div style={{ wordBreak: "break-all" }}>
                <Text code copyable={{ text: details.peer_cert_subject }}>
                  {details.peer_cert_subject}
                </Text>
              </div>
            ) : (
              <Text type="secondary">—</Text>
            )}
          </Descriptions.Item>

          <Descriptions.Item label="Срок действия">
            {details?.peer_cert_validity ? (
              <Tag color="cyan">{details.peer_cert_validity}</Tag>
            ) : (
              <Text type="secondary">—</Text>
            )}
          </Descriptions.Item>
        </Descriptions>

        {/* Блок 5: Журнал аудита жизненного цикла и безопасности */}
        <Card
          size="small"
          title={
            <Space>
              <HistoryOutlined style={{ color: "#1677ff" }} />
              <span>
                Журнал аудита жизненного цикла и безопасности (Device Audit Log)
              </span>
            </Space>
          }
        >
          {recentAuditEvents.length > 0 ? (
            <Table
              rowKey="id"
              columns={auditColumns}
              dataSource={recentAuditEvents}
              size="small"
              pagination={false}
            />
          ) : (
            <Text type="secondary">
              Записи аудита безопасности и провиженинга отсутствуют.
            </Text>
          )}
        </Card>

        {/* Блок 6: Сырой объект JSON (Raw JSON Viewer) */}
        <Card
          size="small"
          title="Сырой объект JSON (Raw Device Data)"
          extra={
            <Space>
              <Button
                size="small"
                icon={<ReloadOutlined />}
                onClick={fetchDeviceData}
                loading={loading}
              >
                Обновить
              </Button>
              <Button
                size="small"
                icon={<CopyOutlined />}
                onClick={handleCopyJson}
                disabled={!device}
              >
                Копировать JSON
              </Button>
            </Space>
          }
        >
          <pre
            style={{
              background: "#141414",
              color: "#e6f7ff",
              padding: "12px 16px",
              borderRadius: 6,
              maxHeight: 380,
              overflow: "auto",
              fontFamily: "Consolas, Monaco, 'Courier New', monospace",
              fontSize: 12,
              lineHeight: 1.5,
              margin: 0,
            }}
          >
            {device ? JSON.stringify(device, null, 2) : "Нет данных"}
          </pre>
        </Card>
      </Space>
    </Spin>
  );
}
