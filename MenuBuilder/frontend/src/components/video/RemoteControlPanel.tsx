import React from "react";
import { Button, Card, Collapse, Popconfirm, Space, Tag, theme, Tooltip, Typography } from "antd";
import {
  DesktopOutlined,
  EnterOutlined,
  KeyOutlined,
  LockOutlined,
  PoweroffOutlined,
  SafetyCertificateOutlined,
  StopOutlined,
  ThunderboltOutlined,
  WindowsOutlined,
} from "@ant-design/icons";
import type { ControlAgentStatus, ControlLease } from "../../api/video";
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
  onEnableControl: () => void;
  onDisableControl: () => void;
  onSendKey: (kind: "down" | "up" | "press", vk: number, text?: string) => boolean | Promise<any>;
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
  onEnableControl,
  onDisableControl,
  onSendKey,
  isMobile = false,
}) => {
  const { token } = theme.useToken();

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
  } else if (!isAgentOnline) {
    disabledReason = "Агент удалённого управления l4desk на терминале не отвечает.";
  } else if (!isDesktopAvailable) {
    disabledReason = "Рабочий стол терминала заблокирован или недоступен для ввода.";
  } else if (rcStatus === "busy") {
    disabledReason = `Управление занято другим оператором (${busyOwner || "другой сеанс"}).`;
  }

  const canEnable = !disabledReason && !isControlActive && !isAcquiring;

  // Безопасная отправка нажатия функциональных клавиш
  const handleKeyPress = (vk: number, label: string) => {
    if (!isControlActive) return;
    onSendKey("down", vk);
    setTimeout(() => {
      onSendKey("up", vk);
    }, 50);
  };

  // Комбинация Win + D (Свернуть все окна)
  const handleWinD = () => {
    if (!isControlActive) return;
    onSendKey("down", 91); // VK_LWIN
    onSendKey("down", 68); // 'D'
    setTimeout(() => {
      onSendKey("up", 68);
      onSendKey("up", 91);
    }, 60);
  };

  // Комбинация Ctrl + Alt + Del
  const handleCtrlAltDel = () => {
    if (!isControlActive) return;
    onSendKey("down", 17); // VK_CONTROL
    onSendKey("down", 18); // VK_MENU (Alt)
    onSendKey("down", 46); // VK_DELETE
    setTimeout(() => {
      onSendKey("up", 46);
      onSendKey("up", 18);
      onSendKey("up", 17);
    }, 80);
  };

  return (
    <div
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
              ? "Управление активно: клики мышью по видео передаются на рабочий стол терминала."
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
            <Tooltip title={disabledReason || "Запросить эксклюзивный доступ к вводу на терминале"}>
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
        <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
          <Text strong style={{ fontSize: 11, textTransform: "uppercase", color: token.colorTextTertiary }}>
            Быстрые клавиши терминала
          </Text>
          {!isControlActive && (
            <Text type="secondary" style={{ fontSize: 11 }}>
              (Доступно при активном управлении)
            </Text>
          )}
        </div>

        <div style={{ display: "flex", flexWrap: "wrap", gap: 8, alignItems: "center" }}>
          <Button
            size="small"
            icon={<EnterOutlined />}
            onClick={() => handleKeyPress(13, "Enter")}
            disabled={!isControlActive}
            style={{ minHeight: isMobile ? 38 : "auto" }}
          >
            Enter
          </Button>

          <Button
            size="small"
            onClick={() => handleKeyPress(27, "Esc")}
            disabled={!isControlActive}
            style={{ minHeight: isMobile ? 38 : "auto" }}
          >
            Esc
          </Button>

          <Button
            size="small"
            onClick={() => handleKeyPress(9, "Tab")}
            disabled={!isControlActive}
            style={{ minHeight: isMobile ? 38 : "auto" }}
          >
            Tab
          </Button>

          <Button
            size="small"
            onClick={() => handleKeyPress(32, "Space")}
            disabled={!isControlActive}
            style={{ minHeight: isMobile ? 38 : "auto" }}
          >
            Пробел
          </Button>

          <Tooltip title="Свернуть все окна на рабочем столе (Win + D)">
            <Button
              size="small"
              icon={<WindowsOutlined />}
              onClick={handleWinD}
              disabled={!isControlActive}
              style={{ minHeight: isMobile ? 38 : "auto" }}
            >
              Win + D
            </Button>
          </Tooltip>

          <Button
            size="small"
            onClick={() => handleKeyPress(116, "F5")}
            disabled={!isControlActive}
            style={{ minHeight: isMobile ? 38 : "auto" }}
          >
            F5 (Обновить)
          </Button>

          {/* Безопасное действие с подтверждением через Popconfirm */}
          <Popconfirm
            title="Отправка комбинации клавиш"
            description="Отправить системную комбинацию Ctrl + Alt + Del на терминал?"
            okText="Отправить"
            cancelText="Отмена"
            onConfirm={handleCtrlAltDel}
            disabled={!isControlActive}
          >
            <Button
              size="small"
              danger
              icon={<SafetyCertificateOutlined />}
              disabled={!isControlActive}
              style={{ minHeight: isMobile ? 38 : "auto", marginLeft: "auto" }}
            >
              Ctrl + Alt + Del
            </Button>
          </Popconfirm>
        </div>
      </div>
    </div>
  );
};

export default RemoteControlPanel;
