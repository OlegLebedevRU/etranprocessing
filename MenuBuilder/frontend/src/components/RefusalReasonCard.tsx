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
  | "free_quota_exhausted"
  | "grace_blocked"
  | "unknown";

export interface RefusalReasonInfo {
  type: RefusalReasonType;
  title: string;
  description: string;
  badge: string;
  color: "warning" | "error" | "info";
}

/**
 * Classify error code or message into one of the 5 canonical refusal reasons:
 * 1. session conflict (session_busy, 409)
 * 2. offline (offline, not reachable)
 * 3. provisioning/certificate pending (pending, certificate_pending)
 * 4. free quota exhausted without paid access (free_quota_exceeded, unpaid_secondary_terminal)
 * 5. grace/blocked (grace, entitlement_blocked, blocked)
 */
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

  // 4. Free quota exhausted without paid access
  if (
    c === "free_quota_exceeded" ||
    c === "unpaid_secondary_terminal" ||
    m.includes("free_quota_exceeded") ||
    m.includes("бесплатная квота") ||
    m.includes("квота исчерпана") ||
    m.includes("120 минут")
  ) {
    return {
      type: "free_quota_exhausted",
      title: "Бесплатная квота исчерпана",
      description:
        "Бесплатный лимит 120 минут в сутки для данного терминала исчерпан. Для продолжения работы сверх 120 минут пополните баланс лицевого счёта организации.",
      badge: "Квота 120 мин исчерпана",
      color: "warning",
    };
  }

  // 5. Grace / Blocked
  if (
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
        "Удалённые сессии заблокированы из-за задолженности по подписке или истечения grace-периода. Пополните баланс лицевого счёта для немедленной разблокировки всех функций.",
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
  onTopUp?: () => void;
  onRetry?: () => void;
  onStopActiveSession?: () => void;
  onViewPin?: () => void;
  onClose?: () => void;
  style?: React.CSSProperties;
}

export default function RefusalReasonCard({
  code,
  rawMessage,
  onTopUp,
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
      case "free_quota_exhausted":
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
      case "free_quota_exhausted":
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
            {(info.type === "free_quota_exhausted" || info.type === "grace_blocked") && onTopUp && (
              <Button type="primary" size="small" icon={<DollarOutlined />} onClick={onTopUp}>
                Пополнить баланс
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
