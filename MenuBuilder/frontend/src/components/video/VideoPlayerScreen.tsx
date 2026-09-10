import React, { useRef, useState } from "react";
import { Alert, Button, Empty, Space, Spin, Tag, theme, Tooltip, Typography } from "antd";
import {
  AlertOutlined,
  ApiOutlined,
  DisconnectOutlined,
  FullscreenExitOutlined,
  FullscreenOutlined,
  LockOutlined,
  PlayCircleOutlined,
  ReloadOutlined,
  VideoCameraOutlined,
} from "@ant-design/icons";
import RemoteControlOverlay from "../RemoteControlOverlay";
import type { DeviceListItem } from "../../api/devices";
import type { ControlAgentStatus } from "../../api/video";
import type { RemoteControlStatus } from "../../hooks/useRemoteControl";
import type { StreamStage } from "./StreamControls";

const { Text, Title } = Typography;

export interface VideoPlayerScreenProps {
  selectedDevice: DeviceListItem | null;
  videoRef: React.RefObject<HTMLVideoElement | null>;
  isSessionActive: boolean;
  streamStage: StreamStage;
  errorMessage?: string | null;
  isOperator: boolean;
  isViewer: boolean;
  activeSourceLabel?: string;
  isCameraMode: boolean;
  onStartStream: () => void;
  onRetryStream?: () => void;
  onRefreshTerminal?: () => void;
  // Remote control
  rc: {
    status: RemoteControlStatus;
    presence: ControlAgentStatus | null;
    sendMove: (x: number, y: number) => void;
    sendClick: (x: number, y: number) => Promise<any>;
    sendKey?: (kind: "down" | "up" | "press", vk: number, text?: string) => boolean | Promise<any>;
    busyOwner: string | null;
  };
}

export const VideoPlayerScreen: React.FC<VideoPlayerScreenProps> = ({
  selectedDevice,
  videoRef,
  isSessionActive,
  streamStage,
  errorMessage,
  isOperator,
  isViewer,
  activeSourceLabel,
  isCameraMode,
  onStartStream,
  onRetryStream,
  onRefreshTerminal,
  rc,
}) => {
  const { token } = theme.useToken();
  const playerContainerRef = useRef<HTMLDivElement | null>(null);
  const [isFullscreen, setIsFullscreen] = useState(false);

  const isTerminalOnline = selectedDevice?.status === "online";
  const isControlActive = isSessionActive && rc.status === "active";
  const isStarting = streamStage === "starting";
  const isStopping = streamStage === "stopping";
  const isLive = isSessionActive && streamStage === "running";
  const isFailed = streamStage === "failed" || Boolean(errorMessage);

  // Полноэкранный режим плеера
  const toggleFullscreen = () => {
    const container = playerContainerRef.current;
    if (!container) return;

    if (!document.fullscreenElement) {
      void container.requestFullscreen().then(() => setIsFullscreen(true)).catch(() => {});
    } else {
      void document.exitFullscreen().then(() => setIsFullscreen(false)).catch(() => {});
    }
  };

  // 1. Состояние: терминал не выбран
  if (!selectedDevice) {
    return (
      <div
        style={{
          width: "100%",
          aspectRatio: "16 / 9",
          minHeight: "260px",
          maxHeight: "calc(100vh - 280px)",
          backgroundColor: "#0d1117",
          borderRadius: 8,
          border: "1px solid #30363d",
          display: "flex",
          alignItems: "center",
          justifyContent: "center",
          padding: 24,
        }}
      >
        <Empty
          image={<VideoCameraOutlined style={{ fontSize: 56, color: "rgba(255,255,255,0.25)" }} />}
          description={
            <span style={{ color: "rgba(255,255,255,0.65)", fontSize: 14 }}>
              Выберите терминал в списке для просмотра трансляции
            </span>
          }
        />
      </div>
    );
  }

  // 2. Состояние: терминал офлайн
  if (!isTerminalOnline) {
    return (
      <div
        style={{
          width: "100%",
          aspectRatio: "16 / 9",
          minHeight: "260px",
          maxHeight: "calc(100vh - 280px)",
          backgroundColor: "#161b22",
          borderRadius: 8,
          border: "1px solid #30363d",
          display: "flex",
          flexDirection: "column",
          alignItems: "center",
          justifyContent: "center",
          padding: 24,
          textAlign: "center",
          gap: 12,
        }}
      >
        <DisconnectOutlined style={{ fontSize: 48, color: "#ff7875" }} />
        <div style={{ color: "#ffffff", fontSize: 16, fontWeight: 600 }}>
          Терминал не на связи (offline)
        </div>
        <div style={{ color: "rgba(255,255,255,0.65)", fontSize: 13, maxWidth: 440 }}>
          Устройство отключено или отсутствует сетевое подключение. Запуск видеонаблюдения станет доступен сразу после выхода терминала на связь.
        </div>
        {onRefreshTerminal && (
          <Button
            ghost
            icon={<ReloadOutlined />}
            onClick={onRefreshTerminal}
            style={{ marginTop: 8 }}
          >
            Проверить статус связи
          </Button>
        )}
      </div>
    );
  }

  // 3. Состояние: сессия занята другим оператором
  if (rc.status === "busy") {
    return (
      <div
        style={{
          width: "100%",
          aspectRatio: "16 / 9",
          minHeight: "260px",
          maxHeight: "calc(100vh - 280px)",
          backgroundColor: "#161b22",
          borderRadius: 8,
          border: "1px solid #30363d",
          display: "flex",
          flexDirection: "column",
          alignItems: "center",
          justifyContent: "center",
          padding: 24,
          textAlign: "center",
          gap: 12,
        }}
      >
        <LockOutlined style={{ fontSize: 48, color: "#faad14" }} />
        <div style={{ color: "#ffffff", fontSize: 16, fontWeight: 600 }}>
          Терминал занят другим оператором
        </div>
        <div style={{ color: "rgba(255,255,255,0.65)", fontSize: 13, maxWidth: 460 }}>
          В данный момент активна эксклюзивная сессия управления ({rc.busyOwner || "другой пользователь"}).
          Дождитесь завершения сеанса для перехвата управления.
        </div>
        {onRefreshTerminal && (
          <Button ghost icon={<ReloadOutlined />} onClick={onRefreshTerminal}>
            Обновить статус
          </Button>
        )}
      </div>
    );
  }

  // 4. Главный контейнер плеера
  return (
    <div
      ref={playerContainerRef}
      style={{
        position: "relative",
        width: "100%",
        aspectRatio: "16 / 9",
        minHeight: "260px",
        maxHeight: "calc(100vh - 280px)",
        backgroundColor: "#000000",
        borderRadius: 8,
        overflow: "hidden",
        display: "flex",
        alignItems: "center",
        justifyContent: "center",
        border: isControlActive
          ? `2px solid ${token.colorPrimary}`
          : "1px solid #30363d",
        boxShadow: isControlActive ? `0 0 12px rgba(22, 119, 255, 0.35)` : "none",
        transition: "border 0.2s ease, box-shadow 0.2s ease",
      }}
    >
      {/* HTML5 Video элемент */}
      <video
        ref={videoRef}
        autoPlay
        playsInline
        muted
        controls={false}
        style={{
          width: "100%",
          height: "100%",
          objectFit: "contain",
          display: isLive ? "block" : "none",
          backgroundColor: "#000000",
        }}
      />

      {/* Оверлей управления мышью и клавиатурой */}
      <RemoteControlOverlay
        videoRef={videoRef}
        active={isControlActive}
        presence={rc.presence}
        sendMove={rc.sendMove}
        sendClick={rc.sendClick}
        sendKey={rc.sendKey}
        isCameraMode={isCameraMode}
      />

      {/* Индикатор прямого эфира поверх видео */}
      {isLive && (
        <div
          style={{
            position: "absolute",
            top: 12,
            left: 12,
            zIndex: 12,
            display: "flex",
            alignItems: "center",
            gap: 6,
            backgroundColor: "rgba(0, 0, 0, 0.65)",
            padding: "4px 8px",
            borderRadius: 6,
            backdropFilter: "blur(4px)",
            pointerEvents: "none",
          }}
        >
          <span
            style={{
              width: 8,
              height: 8,
              borderRadius: "50%",
              backgroundColor: "#52c41a",
              boxShadow: "0 0 6px #52c41a",
              display: "inline-block",
            }}
          />
          <span style={{ color: "#ffffff", fontSize: 11, fontWeight: 700, letterSpacing: 0.8 }}>
            В ЭФИРЕ
          </span>
          {activeSourceLabel && (
            <span style={{ color: "rgba(255,255,255,0.7)", fontSize: 11 }}>
              • {activeSourceLabel}
            </span>
          )}
        </div>
      )}

      {/* Кнопка полноэкранного режима */}
      {isLive && (
        <Tooltip title={isFullscreen ? "Выйти из полноэкранного режима" : "Во весь экран"}>
          <Button
            type="text"
            icon={isFullscreen ? <FullscreenExitOutlined /> : <FullscreenOutlined />}
            onClick={toggleFullscreen}
            style={{
              position: "absolute",
              bottom: 12,
              right: 12,
              zIndex: 15,
              color: "#ffffff",
              backgroundColor: "rgba(0,0,0,0.55)",
              border: "1px solid rgba(255,255,255,0.2)",
              backdropFilter: "blur(4px)",
            }}
            aria-label="Полноэкранный режим видео"
          />
        </Tooltip>
      )}

      {/* Индикатор активного удалённого управления */}
      {isControlActive && (
        <div
          style={{
            position: "absolute",
            top: 12,
            right: 12,
            zIndex: 12,
            pointerEvents: "none",
          }}
        >
          <Tag color="blue" style={{ margin: 0, fontWeight: 600, fontSize: 12 }}>
            Управление мышью активно
          </Tag>
        </div>
      )}

      {/* Состояние: идёт подключение к трансляции */}
      {isStarting && (
        <div
          style={{
            position: "absolute",
            inset: 0,
            display: "flex",
            flexDirection: "column",
            alignItems: "center",
            justifyContent: "center",
            backgroundColor: "rgba(0, 0, 0, 0.75)",
            zIndex: 20,
            gap: 16,
            padding: 20,
            textAlign: "center",
          }}
        >
          <Spin size="large" />
          <div style={{ color: "#ffffff", fontSize: 15, fontWeight: 500 }}>
            Подключение к видеопотоку терминала...
          </div>
          <div style={{ color: "rgba(255,255,255,0.65)", fontSize: 12 }}>
            Инициализация медиаканала и установка WebRTC соединения
          </div>
        </div>
      )}

      {/* Состояние: ошибка трансляции */}
      {isFailed && !isStarting && (
        <div
          style={{
            position: "absolute",
            inset: 0,
            display: "flex",
            flexDirection: "column",
            alignItems: "center",
            justifyContent: "center",
            backgroundColor: "#161b22",
            zIndex: 20,
            gap: 12,
            padding: 24,
            textAlign: "center",
          }}
        >
          <AlertOutlined style={{ fontSize: 44, color: "#ff4d4f" }} />
          <div style={{ color: "#ffffff", fontSize: 16, fontWeight: 600 }}>
            Не удалось запустить трансляцию
          </div>
          <div style={{ color: "rgba(255,255,255,0.75)", fontSize: 13, maxWidth: 460 }}>
            {errorMessage || "Ошибка связи с видеосервером или источником видеосигнала."}
          </div>
          <div style={{ display: "flex", gap: 10, marginTop: 8 }}>
            {onRetryStream && (
              <Button type="primary" onClick={onRetryStream} icon={<ReloadOutlined />}>
                Повторить попытку
              </Button>
            )}
          </div>
        </div>
      )}

      {/* Состояние: трансляция не запущена */}
      {!isLive && !isStarting && !isFailed && (
        <div
          style={{
            position: "absolute",
            inset: 0,
            display: "flex",
            flexDirection: "column",
            alignItems: "center",
            justifyContent: "center",
            backgroundColor: "#0d1117",
            zIndex: 10,
            gap: 14,
            padding: 24,
            textAlign: "center",
          }}
        >
          <VideoCameraOutlined style={{ fontSize: 52, color: "rgba(255, 255, 255, 0.35)" }} />
          <div style={{ color: "#ffffff", fontSize: 16, fontWeight: 500 }}>
            Трансляция не запущена
          </div>
          <div style={{ color: "rgba(255, 255, 255, 0.6)", fontSize: 13, maxWidth: 440 }}>
            {isViewer
              ? "Ожидание запуска видеотрансляции оператором терминала."
              : "Выберите источник видео ниже и нажмите «Запустить трансляцию»."}
          </div>

          {isOperator && (
            <Button
              type="primary"
              size="middle"
              icon={<PlayCircleOutlined />}
              onClick={onStartStream}
              style={{
                marginTop: 6,
                fontWeight: 600,
                boxShadow: "0 2px 8px rgba(22, 119, 255, 0.35)",
              }}
            >
              Запустить трансляцию
            </Button>
          )}
        </div>
      )}
    </div>
  );
};

export default VideoPlayerScreen;
