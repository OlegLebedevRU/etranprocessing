import React from "react";
import { Badge, Button, Space, Tag, theme, Tooltip, Typography } from "antd";
import {
  PlayCircleOutlined,
  StopOutlined,
  SyncOutlined,
  VideoCameraOutlined,
} from "@ant-design/icons";

const { Text } = Typography;

export type StreamStage = "idle" | "starting" | "running" | "stopping" | "failed";

export interface StreamControlsProps {
  isSessionActive: boolean;
  streamStage: StreamStage;
  isTerminalOnline: boolean;
  isOperator: boolean;
  isViewer: boolean;
  onStart: () => void;
  onStop: () => void;
  hasSources?: boolean;
  activeSourceLabel?: string;
  isMobile?: boolean;
}

export const StreamControls: React.FC<StreamControlsProps> = ({
  isSessionActive,
  streamStage,
  isTerminalOnline,
  isOperator,
  isViewer,
  onStart,
  onStop,
  hasSources = true,
  activeSourceLabel,
  isMobile = false,
}) => {
  const { token } = theme.useToken();

  const isStarting = streamStage === "starting";
  const isStopping = streamStage === "stopping";
  const isLive = isSessionActive && streamStage === "running";

  // Определение человекочитаемого статуса трансляции
  let statusBadgeColor = "default";
  let statusBadgeText = "Не запущена";
  let statusBadgeType: "default" | "success" | "processing" | "warning" | "error" = "default";

  if (isLive) {
    statusBadgeType = "success";
    statusBadgeColor = "green";
    statusBadgeText = "В эфире";
  } else if (isStarting) {
    statusBadgeType = "processing";
    statusBadgeColor = "blue";
    statusBadgeText = "Подключение...";
  } else if (isStopping) {
    statusBadgeType = "warning";
    statusBadgeColor = "orange";
    statusBadgeText = "Остановка...";
  } else if (streamStage === "failed") {
    statusBadgeType = "error";
    statusBadgeColor = "red";
    statusBadgeText = "Ошибка запуска";
  }

  // Причины блокировки кнопки запуска
  let startDisabledReason: string | null = null;
  if (!isTerminalOnline) {
    startDisabledReason = "Терминал не на связи (offline). Запуск трансляции невозможен.";
  } else if (!hasSources) {
    startDisabledReason = "Источники видео не обнаружены. Обновите список источников.";
  } else if (!isOperator) {
    startDisabledReason = "У вашей учетной записи нет прав оператора для запуска трансляции.";
  } else if (isStarting) {
    startDisabledReason = "Инициализация видеопотока...";
  }

  const canStart = !startDisabledReason && !isLive && !isStarting;

  return (
    <div
      style={{
        display: "flex",
        flexDirection: isMobile ? "column" : "row",
        justifyContent: "space-between",
        alignItems: isMobile ? "stretch" : "center",
        gap: 12,
        padding: "12px 16px",
        borderRadius: 8,
        backgroundColor: token.colorBgContainer,
        border: `1px solid ${token.colorBorderSecondary}`,
      }}
    >
      {/* Статус потока и активный источник */}
      <div style={{ display: "flex", alignItems: "center", gap: 10, flexWrap: "wrap" }}>
        <Space size="small">
          <Badge status={statusBadgeType} />
          <Text strong style={{ fontSize: 13 }}>
            Статус трансляции:
          </Text>
          <Tag
            color={statusBadgeColor}
            style={{
              margin: 0,
              fontSize: 12,
              fontWeight: 600,
              padding: "2px 8px",
            }}
          >
            {statusBadgeText}
          </Tag>
        </Space>

        {isLive && activeSourceLabel && (
          <Tag style={{ margin: 0, fontSize: 12 }}>
            Источник: <Text strong>{activeSourceLabel}</Text>
          </Tag>
        )}
      </div>

      {/* Кнопки управления трансляцией */}
      <div
        style={{
          display: "flex",
          alignItems: "center",
          gap: 10,
          justifyContent: isMobile ? "stretch" : "flex-end",
        }}
      >
        {isOperator && (
          <>
            {!isLive ? (
              <Tooltip title={startDisabledReason || "Начать трансляцию видео с терминала"}>
                <span style={{ display: "inline-block", flex: isMobile ? 1 : "initial" }}>
                  <Button
                    type="primary"
                    size="large"
                    icon={isStarting ? <SyncOutlined spin /> : <PlayCircleOutlined />}
                    disabled={!canStart}
                    loading={isStarting}
                    onClick={onStart}
                    style={{
                      width: isMobile ? "100%" : "auto",
                      minWidth: 170,
                      fontWeight: 600,
                      boxShadow: canStart ? "0 2px 8px rgba(22, 119, 255, 0.25)" : "none",
                    }}
                    aria-label="Запустить трансляцию"
                  >
                    {isStarting ? "Подключение..." : "Запустить трансляцию"}
                  </Button>
                </span>
              </Tooltip>
            ) : (
              <Button
                danger
                type="primary"
                size="large"
                icon={<StopOutlined />}
                loading={isStopping}
                onClick={onStop}
                style={{
                  width: isMobile ? "100%" : "auto",
                  minWidth: 170,
                  fontWeight: 600,
                }}
                aria-label="Остановить трансляцию"
              >
                Остановить трансляцию
              </Button>
            )}
          </>
        )}

        {isViewer && !isLive && (
          <Text type="secondary" style={{ fontSize: 13 }}>
            Режим наблюдателя: ожидание запуска трансляции оператором
          </Text>
        )}
      </div>
    </div>
  );
};

export default StreamControls;
