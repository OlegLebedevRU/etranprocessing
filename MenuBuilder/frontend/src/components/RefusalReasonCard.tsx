import React from "react";
import { Alert, Button, Space, Tag, Typography } from "antd";
import {
  WarningOutlined,
  StopOutlined,
  DisconnectOutlined,
  ClockCircleOutlined,
  DollarOutlined,
  ReloadOutlined,
  KeyOutlined,
} from "@ant-design/icons";

const { Text } = Typography;

export type RefusalReasonType =
  | "session_conflict"
  | "offline"
  | "provisioning_pending"
  | "subscription_required"
  | "grace_blocked"
  | "unknown";

export interface RefusalReasonInfo {
  type: RefusalReasonType;
  title: string;
  description: string;
  badge: string;
  color: "warning" | "error" | "info";
}

/** Explain the server admission decision and the next action. */
export function classifyRefusalReason(code?: string, rawMessage?: string): RefusalReasonInfo {
  const c = (code || "").toLowerCase();
  const m = (rawMessage || "").toLowerCase();

  // 1. Session conflict
  if (
    c === "session_busy" ||
    c === "conflict" ||
    c.includes("busy") ||
    c.includes("lease_taken") ||
    m.includes("занят активной сессией") ||
    m.includes("session_busy") ||
    m.includes("автоматическое переключение")
  ) {
    return {
      type: "session_conflict",
      title: "Конфликт активной сессии",
      description:
        "Терминал уже занят другой активной сессией (видеонаблюдение или консоль). Автоматическое переключение запрещено для защиты оператора. Завершите текущую сессию перед запуском новой.",
      badge: "Конфликт сессии",
      color: "warning",
    };
  }

  // 2. Offline
  if (
    c === "offline" ||
    c.includes("offline") ||
    m.includes("offline") ||
    m.includes("не в сети") ||
    m.includes("device is offline")
  ) {
    return {
      type: "offline",
      title: "Терминал не в сети (Offline)",
      description:
        "Компьютер терминала отключён или служба L4Desk Agent не запущена. Проверьте питание терминала, подключение к сети интернет и статус службы агента.",
      badge: "Не в сети",
      color: "error",
    };
  }

  // 3. Provisioning / Certificate pending
  if (
    c === "provisioning_pending" ||
    c === "certificate_pending" ||
    c === "pending" ||
    m.includes("certificate") ||
    m.includes("provisioning") ||
    m.includes("сертификат") ||
    m.includes("выпуск") ||
    m.includes("пин")
  ) {
    return {
      type: "provisioning_pending",
      title: "Подготовка терминала не завершена",
      description:
        "Выпуск сертификата устройства или синхронизация с платформой ещё не завершены. Убедитесь, что одноразовый PIN-код введён в мастере установки Агента на терминале.",
      badge: "Ожидает настройки",
      color: "info",
    };
  }

  if (c === "subscription_payments_disabled") {
    return { type: "unknown", title: "Платные подключения пока недоступны", description: "Сейчас можно пользоваться бесплатным терминалом. Дополнительные терминалы подключатся после запуска оплаты и покупки подписки.", badge: "Оплата пока недоступна", color: "info" };
  }
  if (c === "subscription_admin_disabled" || c === "subscription_deleted") {
    return { type: "unknown", title: "Терминал отключён", description: "Обратитесь к администратору. Оплата не отменяет административное отключение.", badge: "Отключён", color: "error" };
  }
  if (c === "subscription_unpaid" || c === "subscription_required" || c === "terminal_limit_reached") {
    return { type: "subscription_required", title: "Требуется подписка", description: c === "terminal_limit_reached" ? "Без активной платной подписки можно создать три терминала. Откройте «Подписки» и подключите дополнительный терминал." : "Для этого дополнительного терминала нужна подписка. Откройте «Подписки», выберите терминал и срок подключения.", badge: "Требуется подписка", color: "warning" };
  }

  // 5. Grace / Blocked
  if (
    c === "subscription_expired" ||
    c === "entitlement_blocked" ||
    c === "grace_blocked" ||
    c === "grace" ||
    c === "blocked" ||
    c === "payment_required" ||
    m.includes("entitlement_blocked") ||
    m.includes("заблокирован") ||
    m.includes("grace") ||
    m.includes("подписка")
  ) {
    return {
      type: "grace_blocked",
      title: "Действие подписки приостановлено",
      description:
        "Подписка этого терминала закончилась. Откройте «Подписки» и продлите срок, чтобы восстановить доступ. Оплата одного терминала не влияет на остальные.",
      badge: "Блокировка подписки",
      color: "error",
    };
  }

  return {
    type: "unknown",
    title: "Сессия отклонена",
    description:
      rawMessage ||
      "Не удалось запустить удалённую сессию. Проверьте состояние терминала и попробуйте снова.",
    badge: "Отказ",
    color: "error",
  };
}

export interface RefusalReasonCardProps {
  code?: string;
  rawMessage?: string;
  onSubscriptions?: () => void;
  onRetry?: () => void;
  onStopActiveSession?: () => void;
  onViewPin?: () => void;
  onClose?: () => void;
  style?: React.CSSProperties;
}

export default function RefusalReasonCard({
  code,
  rawMessage,
  onSubscriptions,
  onRetry,
  onStopActiveSession,
  onViewPin,
  onClose,
  style,
}: RefusalReasonCardProps) {
  const info = classifyRefusalReason(code, rawMessage);

  const getIcon = () => {
    switch (info.type) {
      case "session_conflict":
        return <WarningOutlined style={{ color: "#faad14" }} />;
      case "offline":
        return <DisconnectOutlined style={{ color: "#ff4d4f" }} />;
      case "provisioning_pending":
        return <ClockCircleOutlined style={{ color: "#1890ff" }} />;
      case "subscription_required":
        return <DollarOutlined style={{ color: "#faad14" }} />;
      case "grace_blocked":
        return <StopOutlined style={{ color: "#ff4d4f" }} />;
      default:
        return <WarningOutlined style={{ color: "#ff4d4f" }} />;
    }
  };

  const getTagColor = () => {
    switch (info.type) {
      case "session_conflict":
      case "subscription_required":
        return "warning";
      case "offline":
      case "grace_blocked":
        return "error";
      case "provisioning_pending":
        return "processing";
      default:
        return "default";
    }
  };

  return (
    <Alert
      style={{ marginBottom: 16, borderRadius: 8, ...style }}
      type={info.color}
      showIcon
      icon={getIcon()}
      closable={Boolean(onClose)}
      onClose={onClose}
      message={
        <div style={{ display: "flex", alignItems: "center", gap: 8, flexWrap: "wrap" }}>
          <Text strong style={{ fontSize: 14 }}>
            {info.title}
          </Text>
          <Tag color={getTagColor()}>{info.badge}</Tag>
        </div>
      }
      description={
        <div style={{ marginTop: 6 }}>
          <p style={{ margin: "0 0 10px 0", color: "#595959", fontSize: 13, lineHeight: 1.5 }}>
            {info.description}
          </p>
          <Space wrap size="small">
            {/* Actions depending on reason */}
            {(info.type === "subscription_required" || info.type === "grace_blocked") && onSubscriptions && (
              <Button type="primary" size="small" icon={<DollarOutlined />} onClick={onSubscriptions}>
                Открыть подписки
              </Button>
            )}

            {info.type === "session_conflict" && onStopActiveSession && (
              <Button
                danger
                size="small"
                icon={<StopOutlined />}
                onClick={onStopActiveSession}
              >
                Завершить активную сессию
              </Button>
            )}

            {info.type === "provisioning_pending" && onViewPin && (
              <Button size="small" icon={<KeyOutlined />} onClick={onViewPin}>
                Показать PIN-код и инструкцию
              </Button>
            )}

            {onRetry && (
              <Button size="small" icon={<ReloadOutlined />} onClick={onRetry}>
                Повторить попытку
              </Button>
            )}
          </Space>
        </div>
      }
    />
  );
}
