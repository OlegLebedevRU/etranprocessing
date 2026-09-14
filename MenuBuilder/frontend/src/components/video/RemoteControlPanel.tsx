import React, { useState } from "react";
import {
  Button,
  Popconfirm,
  Space,
  Switch,
  Tag,
  theme,
  Tooltip,
  Typography,
} from "antd";
import {
  CloseCircleOutlined,
  DesktopOutlined,
  EnterOutlined,
  StopOutlined,
  ThunderboltOutlined,
  ToolOutlined,
  WindowsOutlined,
} from "@ant-design/icons";
import type { ClickResult, ControlAgentStatus, ControlLease } from "../../api/video";
import type { RemoteControlStatus } from "../../hooks/useRemoteControl";

const { Text } = Typography;

export interface RemoteControlPanelProps {
  rcStatus: RemoteControlStatus;
  presence: ControlAgentStatus | null;
  lease: ControlLease | null;
  busyOwner: string | null;
  isSessionActive: boolean;
  isCameraMode: boolean;
  isTerminalOnline: boolean;
  selectedProfile?: string;
  onEnableControl: () => void;
  onDisableControl: () => void;
  onSendShortcut?: (action: "f12" | "alt_f4" | "win_d") => Promise<any>;
  onSendKey?: (kind: "down" | "up" | "press", vk: number, text?: string) => boolean | Promise<any>;
  lastCommandResult?: ClickResult | null;
  isMobile?: boolean;
}

export const RemoteControlPanel: React.FC<RemoteControlPanelProps> = ({
  rcStatus,
  presence,
  lease,
  busyOwner,
  isSessionActive,
  isCameraMode,
  isTerminalOnline,
  selectedProfile,
  onEnableControl,
  onDisableControl,
  onSendShortcut,
  onSendKey,
  lastCommandResult,
  isMobile = false,
}) => {
  const { token } = theme.useToken();
  const [maintenanceAllowed, setMaintenanceAllowed] = useState(false);

  const isControlActive = rcStatus === "active";
  const isAcquiring = rcStatus === "acquiring";
  const isAgentOnline = Boolean(presence?.online && !presence?.stale);
  const isDesktopAvailable = Boolean(presence?.desktop_available);

  // Проверка условий доступности удалённого управления
  let disabledReason: string | null = null;
  if (!isTerminalOnline) {
    disabledReason = "Терминал не на связи (offline).";
  } else if (!isSessionActive) {
    disabledReason = "Запустите видеотрансляцию для включения удалённого управления.";
  } else if (isCameraMode) {
    disabledReason = "Управление недоступно в режиме камеры. Переключитесь на рабочий стол.";
  } else if (selectedProfile && selectedProfile !== "low" && selectedProfile !== "480p") {
    disabledReason = "Удалённое управление разрешено только в режиме качества 480p (Эконом).";
  } else if (!isAgentOnline) {
    disabledReason = "Агент удалённого управления l4desk на терминале не отвечает.";
  } else if (!isDesktopAvailable) {
    disabledReason = "Рабочий стол терминала заблокирован или недоступен для ввода.";
  } else if (rcStatus === "busy") {
    disabledReason = `Управление занято другим оператором (${busyOwner || "другой сеанс"}).`;
  }

  const canEnable = !disabledReason && !isControlActive && !isAcquiring;

  // Безопасная отправка нажатия базовых функциональных клавиш
  const handleKeyPress = (vk: number) => {
    if (!isControlActive || !onSendKey) return;
    onSendKey("down", vk);
    setTimeout(() => {
      onSendKey("up", vk);
    }, 50);
  };

  return (
    <div
      onClick={(e) => e.stopPropagation()}
      onMouseDown={(e) => e.stopPropagation()}
      style={{
        padding: "14px 16px",
        borderRadius: 8,
        backgroundColor: token.colorBgContainer,
        border: `1px solid ${isControlActive ? token.colorPrimaryBorder : token.colorBorderSecondary}`,
        boxShadow: isControlActive ? `0 0 0 1px ${token.colorPrimaryBorder}` : "none",
        display: "flex",
        flexDirection: "column",
        gap: 12,
        transition: "border 0.2s ease, box-shadow 0.2s ease",
      }}
    >
      {/* Заголовок панели, статус агента и главный переключатель */}
      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          alignItems: isMobile ? "flex-start" : "center",
          flexDirection: isMobile ? "column" : "row",
          gap: 10,
        }}
      >
        <div style={{ display: "flex", flexDirection: "column", gap: 4 }}>
          <div style={{ display: "flex", alignItems: "center", gap: 8, flexWrap: "wrap" }}>
            <DesktopOutlined style={{ fontSize: 16, color: token.colorPrimary }} />
            <Text strong style={{ fontSize: 13 }}>
              Удалённое управление терминалом (l4desk):
            </Text>

            {/* Мягкие статусные бейджи агента и экрана */}
            {isAgentOnline ? (
              <Tag color="success" style={{ margin: 0, fontSize: 11 }}>
                Агент на связи
              </Tag>
            ) : (
              <Tag color="default" style={{ margin: 0, fontSize: 11 }}>
                Агент офлайн
              </Tag>
            )}

            {isDesktopAvailable ? (
              <Tag color="processing" style={{ margin: 0, fontSize: 11 }}>
                Экран доступен
              </Tag>
            ) : (
              <Tag color="warning" style={{ margin: 0, fontSize: 11 }}>
                Экран заблокирован
              </Tag>
            )}
          </div>

          <Text type="secondary" style={{ fontSize: 12 }}>
            {isControlActive
              ? "Управление активно (480p): клики мышью по видео передаются на рабочий стол терминала."
              : disabledReason || "Активируйте режим управления для отправки мыши и клавиатуры."}
          </Text>
        </div>

        {/* Кнопка активации / деактивации */}
        <div>
          {isControlActive ? (
            <Button
              danger
              icon={<StopOutlined />}
              onClick={onDisableControl}
              style={{ minHeight: isMobile ? 44 : "auto" }}
            >
              Отключить управление
            </Button>
          ) : (
            <Tooltip title={disabledReason || "Запросить эксклюзивный доступ к вводу на терминале (режим 480p)"}>
              <span>
                <Button
                  type="default"
                  icon={<ThunderboltOutlined style={{ color: canEnable ? token.colorPrimary : undefined }} />}
                  loading={isAcquiring}
                  disabled={!canEnable}
                  onClick={onEnableControl}
                  style={{ minHeight: isMobile ? 44 : "auto" }}
                >
                  Включить управление
                </Button>
              </span>
            </Tooltip>
          )}
        </div>
      </div>

      {/* Панель быстрых действий оператора (клавиши и сервис) */}
      <div
        style={{
          padding: "10px 12px",
          backgroundColor: token.colorFillAlter,
          borderRadius: 6,
          border: `1px solid ${token.colorBorderSecondary}`,
          display: "flex",
          flexDirection: "column",
          gap: 8,
          opacity: isControlActive ? 1 : 0.6,
          pointerEvents: isControlActive ? "auto" : "none",
        }}
      >
        <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", flexWrap: "wrap", gap: 6 }}>
          <Text strong style={{ fontSize: 11, textTransform: "uppercase", color: token.colorTextTertiary }}>
            Быстрые действия и клавиши
          </Text>
          <div style={{ display: "flex", alignItems: "center", gap: 6 }}>
            <Switch
              size="small"
              checked={maintenanceAllowed}
              onChange={setMaintenanceAllowed}
              disabled={!isControlActive}
            />
            <Text style={{ fontSize: 11, color: maintenanceAllowed ? token.colorWarningText : token.colorTextSecondary }}>
              Профиль обслуживания
            </Text>
          </div>
        </div>

        <div style={{ display: "flex", flexWrap: "wrap", gap: 8, alignItems: "center" }}>
          {/* F12 Shortcut */}
          <Tooltip title="Отправить F12 (для проверенного профиля приложения киоска; не гарантирует открытие консоли на всех терминалах)">
            <Button
              size="small"
              icon={<ToolOutlined />}
              onClick={() => onSendShortcut?.("f12")}
              disabled={!isControlActive}
              style={{ minHeight: isMobile ? 38 : "auto" }}
            >
              F12
            </Button>
          </Tooltip>

          {/* Alt+F4 Shortcut with Browser Confirmation */}
          {maintenanceAllowed ? (
            <Popconfirm
              title="Закрыть разрешенное активное окно? Это может прервать его работу."
              description="Подтверждение оператора в браузере. Действие проверяется политикой киоска."
              okText="Закрыть окно"
              cancelText="Отмена"
              okButtonProps={{ danger: true }}
              onConfirm={() => onSendShortcut?.("alt_f4")}
              disabled={!isControlActive}
            >
              <Button
                size="small"
                danger
                icon={<CloseCircleOutlined />}
                disabled={!isControlActive}
                style={{ minHeight: isMobile ? 38 : "auto" }}
              >
                Alt + F4
              </Button>
            </Popconfirm>
          ) : (
            <Tooltip title="Alt+F4 отключён по умолчанию на киоске для защиты от выхода из приложения. Требуется подтверждённый профиль обслуживания.">
              <span>
                <Button
                  size="small"
                  disabled
                  icon={<CloseCircleOutlined />}
                  style={{ minHeight: isMobile ? 38 : "auto" }}
                >
                  Alt + F4
                </Button>
              </span>
            </Tooltip>
          )}

          {/* Win+D Shortcut with Browser Confirmation */}
          {maintenanceAllowed ? (
            <Popconfirm
              title="Свернуть окна и показать рабочий стол?"
              description="Подтверждение оператора в браузере. Действие проверяется политикой киоска."
              okText="Свернуть окна"
              cancelText="Отмена"
              onConfirm={() => onSendShortcut?.("win_d")}
              disabled={!isControlActive}
            >
              <Button
                size="small"
                icon={<WindowsOutlined />}
                disabled={!isControlActive}
                style={{ minHeight: isMobile ? 38 : "auto" }}
              >
                Win + D
              </Button>
            </Popconfirm>
          ) : (
            <Tooltip title="Win+D отключён по умолчанию на киоске для защиты от выхода из приложения. Требуется подтверждённый профиль обслуживания.">
              <span>
                <Button
                  size="small"
                  disabled
                  icon={<WindowsOutlined />}
                  style={{ minHeight: isMobile ? 38 : "auto" }}
                >
                  Win + D
                </Button>
              </span>
            </Tooltip>
          )}

          {/* Базовые клавиши навигации */}
          <Button
            size="small"
            icon={<EnterOutlined />}
            onClick={() => handleKeyPress(13)}
            disabled={!isControlActive}
            style={{ minHeight: isMobile ? 38 : "auto" }}
          >
            Enter
          </Button>

          <Button
            size="small"
            onClick={() => handleKeyPress(27)}
            disabled={!isControlActive}
            style={{ minHeight: isMobile ? 38 : "auto" }}
          >
            Esc
          </Button>

          <Button
            size="small"
            onClick={() => handleKeyPress(9)}
            disabled={!isControlActive}
            style={{ minHeight: isMobile ? 38 : "auto" }}
          >
            Tab
          </Button>

          <Button
            size="small"
            onClick={() => handleKeyPress(32)}
            disabled={!isControlActive}
            style={{ minHeight: isMobile ? 38 : "auto" }}
          >
            Пробел
          </Button>
        </div>

        {/* Компактный статус последней команды */}
        {lastCommandResult && (
          <div
            style={{
              display: "flex",
              alignItems: "center",
              gap: 8,
              fontSize: 12,
              marginTop: 4,
              paddingTop: 6,
              borderTop: `1px dashed ${token.colorBorderSecondary}`,
              flexWrap: "wrap",
            }}
          >
            <Text type="secondary" style={{ fontSize: 11 }}>
              Последняя команда:
            </Text>
            <Tag
              color={
                lastCommandResult.result === "injected"
                  ? "success"
                  : lastCommandResult.result === "unconfirmed"
                  ? "warning"
                  : "error"
              }
              style={{ margin: 0, fontSize: 11 }}
            >
              {lastCommandResult.result === "injected"
                ? "Ввод передан"
                : lastCommandResult.result === "unconfirmed"
                ? "Не подтверждено"
                : "Отклонено"}
            </Tag>
            <Text type="secondary" style={{ fontSize: 11 }}>
              {lastCommandResult.message || (lastCommandResult.code ? `Код: ${lastCommandResult.code}` : "")}
              {typeof lastCommandResult.latency_ms === "number" ? ` (${lastCommandResult.latency_ms} мс)` : ""}
            </Text>
          </div>
        )}
      </div>
    </div>
  );
};

export default RemoteControlPanel;
