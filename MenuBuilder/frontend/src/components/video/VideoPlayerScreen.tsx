import React, { useEffect, useRef, useState } from "react";
import { Button, Empty, Segmented, Spin, theme } from "antd";
import {
  AlertOutlined,
  DisconnectOutlined,
  FullscreenExitOutlined,
  FullscreenOutlined,
  LockOutlined,
  ReloadOutlined,
  VideoCameraOutlined,
} from "@ant-design/icons";
import RemoteControlOverlay from "../RemoteControlOverlay";
import type { DeviceListItem } from "../../api/devices";
import type { ControlAgentStatus } from "../../api/video";
import type { RemoteControlStatus } from "../../hooks/useRemoteControl";
import type { StreamStage } from "./StreamControls";

export interface VideoPlayerScreenProps {
  containerRef: React.RefObject<HTMLDivElement | null>;
  displayMode: "fit" | "native";
  onDisplayMode: (mode: "fit" | "native") => void;
  onFrameStateChange: (fresh: boolean) => void;
  onResolutionChange: (resolution: string) => void;
  selectedDevice: DeviceListItem | null;
  videoRef: React.RefObject<HTMLVideoElement | null>;
  isSessionActive: boolean;
  streamStage: StreamStage;
  errorMessage?: string | null;
  isViewer: boolean;
  isCameraMode: boolean;
  streamMode?: string;
  onRetryStream?: () => void;
  onRefreshTerminal?: () => void;
  // Remote control
  rc: {
    status: RemoteControlStatus;
    presence: ControlAgentStatus | null;
    sendMove: (x: number, y: number) => void;
    sendClick: (x: number, y: number) => Promise<any>;
    sendDrag: (x: number, y: number, toX: number, toY: number) => boolean;
    sendWheel: (x: number, y: number, delta: number) => boolean;
    sendKey?: (kind: "down" | "up" | "press", vk: number, text?: string) => boolean | Promise<any>;
    busyOwner: string | null;
  };
}

export const VideoPlayerScreen: React.FC<VideoPlayerScreenProps> = ({
  containerRef: playerContainerRef,
  displayMode,
  onDisplayMode,
  onFrameStateChange,
  onResolutionChange,
  selectedDevice,
  videoRef,
  isSessionActive,
  streamStage,
  errorMessage,
  isViewer,
  isCameraMode,
  streamMode,
  onRetryStream,
  onRefreshTerminal,
  rc,
}) => {
  const { token } = theme.useToken();
  const [isFullscreen, setIsFullscreen] = useState(false);
  const [videoSize, setVideoSize] = useState({ width: 0, height: 0 });
  const [viewportHeight, setViewportHeight] = useState(() => window.innerHeight);
  const [fullscreenControlsVisible, setFullscreenControlsVisible] = useState(true);
  const hideControlsTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  const isTerminalOnline = selectedDevice?.status === "online";
  const isControlActive = isSessionActive && rc.status === "active";
  const isStarting = streamStage === "starting";
  const isStopping = streamStage === "stopping";
  const isLive = isSessionActive && streamStage === "running";
  const isFailed = streamStage === "failed" || Boolean(errorMessage);
  const isNativeSize = isLive && !isControlActive && displayMode === "native" && videoSize.width > 0;
  const sourceAspect = videoSize.width > 0 && videoSize.height > 0 ? videoSize.width / videoSize.height : 16 / 9;
  const reservedHeight = window.innerWidth < 700 ? 210 : 140;

  useEffect(() => {
    const onFullscreenChange = () => setIsFullscreen(document.fullscreenElement === playerContainerRef.current);
    const onResize = () => setViewportHeight(window.innerHeight);
    document.addEventListener("fullscreenchange", onFullscreenChange);
    window.addEventListener("resize", onResize);
    return () => {
      document.removeEventListener("fullscreenchange", onFullscreenChange);
      window.removeEventListener("resize", onResize);
    };
  }, []);

  useEffect(() => {
    if (!isLive) setVideoSize({ width: 0, height: 0 });
  }, [isLive, selectedDevice?.device_id]);

  useEffect(() => {
    onResolutionChange(videoSize.width ? `${videoSize.width}×${videoSize.height}` : "");
  }, [videoSize, onResolutionChange]);

  useEffect(() => {
    const video = videoRef.current;
    onFrameStateChange(false);
    if (!isLive || !video) return;
    let lastFrameAt = 0;
    let frameCallback = 0;
    let cancelled = false;
    const frame = () => {
      if (cancelled) return;
      lastFrameAt = Date.now();
      frameCallback = video.requestVideoFrameCallback(frame);
    };
    frameCallback = video.requestVideoFrameCallback(frame);
    const timer = window.setInterval(() => onFrameStateChange(lastFrameAt > 0 && Date.now() - lastFrameAt < 5000), 1000);
    return () => {
      cancelled = true;
      video.cancelVideoFrameCallback(frameCallback);
      window.clearInterval(timer);
      onFrameStateChange(false);
    };
  }, [isLive, selectedDevice?.device_id, videoRef, onFrameStateChange]);

  useEffect(() => () => {
    if (hideControlsTimer.current) clearTimeout(hideControlsTimer.current);
  }, []);

  const showFullscreenControls = () => {
    setFullscreenControlsVisible(true);
    if (hideControlsTimer.current) clearTimeout(hideControlsTimer.current);
    hideControlsTimer.current = setTimeout(() => setFullscreenControlsVisible(!!document.activeElement?.closest("[data-fullscreen-controls]")), 2500);
  };

  useEffect(() => {
    if (isFullscreen) showFullscreenControls();
    return () => { if (hideControlsTimer.current) clearTimeout(hideControlsTimer.current); };
  }, [isFullscreen]);

  // Полноэкранный режим плеера
  const toggleFullscreen = () => {
    const container = playerContainerRef.current;
    if (!container) return;

    if (!document.fullscreenElement) {
      void container.requestFullscreen().catch(() => {});
    } else {
      void document.exitFullscreen().catch(() => {});
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
          maxHeight: `calc(100dvh - ${reservedHeight}px)`,
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
          maxHeight: `calc(100dvh - ${reservedHeight}px)`,
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
          maxHeight: `calc(100dvh - ${reservedHeight}px)`,
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
      data-testid="video-player"
      onPointerMove={isFullscreen ? showFullscreenControls : undefined}
      style={{
        position: "relative",
        width: "100%",
        maxWidth: isLive && videoSize.width > 0 && !isFullscreen
          ? Math.max(260, Math.floor((viewportHeight - reservedHeight) * sourceAspect))
          : undefined,
        marginInline: "auto",
        aspectRatio: isFullscreen ? undefined : `${sourceAspect}`,
        height: isFullscreen ? "100vh" : undefined,
        minHeight: isFullscreen || isLive ? undefined : "260px",
        maxHeight: isFullscreen ? undefined : `max(260px, calc(100dvh - ${reservedHeight}px))`,
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
      {/* Browser-side display only: native size never changes the encoded WebRTC stream. */}
      <div
        style={{
          position: "absolute",
          inset: 0,
          overflow: isNativeSize ? "auto" : "hidden",
          display: isNativeSize ? "block" : "flex",
          alignItems: "center",
          justifyContent: "center",
        }}
      >
        <video
          ref={videoRef}
          autoPlay
          playsInline
          muted
          controls={false}
          onLoadedMetadata={(event) => setVideoSize({ width: event.currentTarget.videoWidth, height: event.currentTarget.videoHeight })}
          onResize={(event) => setVideoSize({ width: event.currentTarget.videoWidth, height: event.currentTarget.videoHeight })}
          style={{
            width: isNativeSize ? videoSize.width : "100%",
            height: isNativeSize ? videoSize.height : "100%",
            flexShrink: 0,
            objectFit: "contain",
            display: isLive ? "block" : "none",
            backgroundColor: "#000000",
          }}
        />
      </div>

      {/* Оверлей управления мышью и клавиатурой */}
      <RemoteControlOverlay
        videoRef={videoRef}
        active={isControlActive}
        presence={rc.presence}
        sendMove={rc.sendMove}
        sendClick={rc.sendClick}
        sendDrag={rc.sendDrag}
        sendWheel={rc.sendWheel}
        sendKey={rc.sendKey}
        isCameraMode={isCameraMode}
        streamMode={streamMode}
      />

      {isFullscreen && <div data-fullscreen-controls onFocus={showFullscreenControls} style={{ position: "absolute", bottom: 12, right: 12, zIndex: 20, display: "flex", gap: 8, opacity: fullscreenControlsVisible ? 1 : 0, pointerEvents: fullscreenControlsVisible ? "auto" : "none", transition: "opacity .2s" }}>
        <Segmented aria-label="Масштаб видео в браузере" value={isControlActive ? "fit" : displayMode} onChange={value => onDisplayMode(value as "fit" | "native")} options={[{ label: "Вписать", value: "fit" }, { label: "Исходный размер", value: "native", disabled: isControlActive }]} />
        <Button aria-label="Выйти из полноэкранного режима" icon={isFullscreen ? <FullscreenExitOutlined /> : <FullscreenOutlined />} onClick={toggleFullscreen} />
      </div>}

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
              : "Выберите источник видео над плеером и нажмите «Запустить трансляцию»."}
          </div>


        </div>
      )}
    </div>
  );
};

export default VideoPlayerScreen;
