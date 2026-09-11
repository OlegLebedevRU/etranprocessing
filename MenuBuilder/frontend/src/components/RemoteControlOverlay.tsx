import React, { useCallback, useEffect, useRef, useState } from "react";
import { Alert, Tooltip } from "antd";
import { ControlAgentStatus } from "../api/video";

export interface RemoteControlOverlayProps {
  videoRef: React.RefObject<HTMLVideoElement | null>;
  active: boolean;
  presence: ControlAgentStatus | null;
  sendMove: (x: number, y: number) => void;
  sendClick: (x: number, y: number) => Promise<any>;
  sendKey?: (kind: "down" | "up" | "press", vk: number, text?: string) => boolean | Promise<any>;
  isCameraMode?: boolean;
  streamMode?: string;
  debugBorder?: boolean;
}

interface Rect {
  left: number;
  top: number;
  width: number;
  height: number;
}

export default function RemoteControlOverlay({
  videoRef,
  active,
  presence,
  sendMove,
  sendClick,
  sendKey,
  isCameraMode = false,
  streamMode: propStreamMode,
  debugBorder = true,
}: RemoteControlOverlayProps) {
  const containerRef = useRef<HTMLDivElement | null>(null);
  const [desktopRect, setDesktopRect] = useState<Rect | null>(null);
  const [geometryKnown, setGeometryKnown] = useState<boolean>(true);

  const streamMode = propStreamMode ?? presence?.stream?.mode;

  // Recalculate contentRect and desktopRect based on video dimensions and container dimensions
  const updateRects = useCallback(() => {
    const container = containerRef.current;
    const video = videoRef.current;
    if (!container || !video) {
      setDesktopRect(null);
      return;
    }

    const cWidth = container.clientWidth;
    const cHeight = container.clientHeight;
    const vWidth = video.videoWidth || 1920;
    const vHeight = video.videoHeight || 1080;

    if (cWidth <= 0 || cHeight <= 0 || vWidth <= 0 || vHeight <= 0) {
      setDesktopRect(null);
      return;
    }

    // 1. contentRect from object-fit: contain
    const videoAspect = vWidth / vHeight;
    const containerAspect = cWidth / cHeight;

    let contentLeft = 0;
    let contentTop = 0;
    let contentWidth = cWidth;
    let contentHeight = cHeight;

    if (videoAspect > containerAspect) {
      // Letterbox (black bars top & bottom)
      contentWidth = cWidth;
      contentHeight = cWidth / videoAspect;
      contentLeft = 0;
      contentTop = (cHeight - contentHeight) / 2;
    } else {
      // Pillarbox (black bars left & right)
      contentHeight = cHeight;
      contentWidth = cHeight * videoAspect;
      contentTop = 0;
      contentLeft = (cWidth - contentWidth) / 2;
    }

    // 2. ffmpeg frame internal padding calculation
    const screen = presence?.screen;
    if (screen && screen.virtual_width > 0 && screen.virtual_height > 0) {
      setGeometryKnown(true);
      const desktopAspect = screen.virtual_width / screen.virtual_height;

      let dLeft = contentLeft;
      let dTop = contentTop;
      let dWidth = contentWidth;
      let dHeight = contentHeight;

      if (desktopAspect > videoAspect) {
        dWidth = contentWidth;
        dHeight = contentWidth / desktopAspect;
        dLeft = contentLeft;
        dTop = contentTop + (contentHeight - dHeight) / 2;
      } else {
        dHeight = contentHeight;
        dWidth = contentHeight * desktopAspect;
        dTop = contentTop;
        dLeft = contentLeft + (contentWidth - dWidth) / 2;
      }

      setDesktopRect({
        left: Math.round(dLeft),
        top: Math.round(dTop),
        width: Math.round(dWidth),
        height: Math.round(dHeight),
      });
    } else {
      // Screen geometry not yet reported by agent
      setGeometryKnown(false);
      setDesktopRect({
        left: Math.round(contentLeft),
        top: Math.round(contentTop),
        width: Math.round(contentWidth),
        height: Math.round(contentHeight),
      });
    }
  }, [presence?.screen, videoRef]);

  // Recalculate on resize, presence change, or video load
  useEffect(() => {
    updateRects();

    const container = containerRef.current;
    if (!container) return;

    const resizeObserver = new ResizeObserver(() => {
      updateRects();
    });
    resizeObserver.observe(container);

    const video = videoRef.current;
    if (video) {
      video.addEventListener("loadedmetadata", updateRects);
      video.addEventListener("resize", updateRects);
    }

    return () => {
      resizeObserver.disconnect();
      if (video) {
        video.removeEventListener("loadedmetadata", updateRects);
        video.removeEventListener("resize", updateRects);
      }
    };
  }, [updateRects, videoRef]);

  // Non-passive wheel event listener to prevent page scrolling during remote control
  useEffect(() => {
    const el = containerRef.current;
    if (!el || !active || isCameraMode || streamMode !== "desktop") return;
    const handleWheel = (e: WheelEvent) => {
      e.preventDefault();
    };
    el.addEventListener("wheel", handleWheel, { passive: false });
    return () => {
      el.removeEventListener("wheel", handleWheel);
    };
  }, [active, isCameraMode, streamMode]);

  const getNormalizedCoordinates = (e: React.PointerEvent<HTMLDivElement>) => {
    if (!desktopRect || !containerRef.current) return null;
    const rect = containerRef.current.getBoundingClientRect();
    const px = e.clientX - rect.left;
    const py = e.clientY - rect.top;

    if (
      px < desktopRect.left ||
      px > desktopRect.left + desktopRect.width ||
      py < desktopRect.top ||
      py > desktopRect.top + desktopRect.height
    ) {
      return null;
    }

    const normX = Math.round(
      ((px - desktopRect.left) / desktopRect.width) * 65535
    );
    const normY = Math.round(
      ((py - desktopRect.top) / desktopRect.height) * 65535
    );

    const clampedX = Math.max(0, Math.min(65535, normX));
    const clampedY = Math.max(0, Math.min(65535, normY));
    return { x: clampedX, y: clampedY };
  };

  const handlePointerMove = (e: React.PointerEvent<HTMLDivElement>) => {
    if (!active || isCameraMode || streamMode !== "desktop" || !geometryKnown) return;
    const coords = getNormalizedCoordinates(e);
    if (!coords) return;
    sendMove(coords.x, coords.y);
  };

  const handleClick = (e: React.MouseEvent<HTMLDivElement>) => {
    if (!active || isCameraMode || streamMode !== "desktop" || !geometryKnown) return;
    // Left-click only, no modifiers
    if (e.button !== 0 || e.shiftKey || e.ctrlKey || e.altKey || e.metaKey) {
      return;
    }

    const coords = getNormalizedCoordinates(
      e as unknown as React.PointerEvent<HTMLDivElement>
    );
    if (!coords) return;

    void sendClick(coords.x, coords.y);
  };

  const handleKeyDown = (e: React.KeyboardEvent<HTMLDivElement>) => {
    if (!active || isCameraMode || streamMode !== "desktop" || !sendKey) return;
    if (e.key === "F5" || e.key === "F12" || (e.ctrlKey && e.key === "r")) {
      return;
    }
    e.preventDefault();
    e.stopPropagation();
    sendKey("down", e.keyCode, e.key.length === 1 ? e.key : undefined);
  };

  const handleKeyUp = (e: React.KeyboardEvent<HTMLDivElement>) => {
    if (!active || isCameraMode || streamMode !== "desktop" || !sendKey) return;
    if (e.key === "F5" || e.key === "F12" || (e.ctrlKey && e.key === "r")) {
      return;
    }
    e.preventDefault();
    e.stopPropagation();
    sendKey("up", e.keyCode, e.key.length === 1 ? e.key : undefined);
  };

  if (!active) {
    return null;
  }

  if (isCameraMode || streamMode !== "desktop") {
    return (
      <div
        style={{
          position: "absolute",
          top: 10,
          left: "50%",
          transform: "translateX(-50%)",
          zIndex: 11,
          pointerEvents: "auto",
        }}
      >
        <Alert
          message={
            isCameraMode
              ? "Управление вводом недоступно в режиме трансляции камеры (требуется рабочий стол)"
              : "Управление вводом доступно только в подтверждённом режиме рабочего стола"
          }
          type="info"
          showIcon
          style={{
            backgroundColor: "rgba(230, 244, 255, 0.9)",
            padding: "4px 14px",
            fontSize: 12,
          }}
        />
      </div>
    );
  }

  return (
    <div
      ref={containerRef}
      tabIndex={0}
      onPointerMove={handlePointerMove}
      onClick={handleClick}
      onKeyDown={handleKeyDown}
      onKeyUp={handleKeyUp}
      onContextMenu={(e) => e.preventDefault()}
      onMouseDown={(e) => {
        if (e.button !== 0) e.preventDefault();
      }}
      style={{
        position: "absolute",
        top: 0,
        left: 0,
        width: "100%",
        height: "100%",
        zIndex: 10,
        cursor: "crosshair",
        userSelect: "none",
        touchAction: "none",
        outline: "none",
      }}
    >
      {/* Visual boundary of active desktop screen */}
      {desktopRect && (
        <div
          style={{
            position: "absolute",
            left: `${desktopRect.left}px`,
            top: `${desktopRect.top}px`,
            width: `${desktopRect.width}px`,
            height: `${desktopRect.height}px`,
            border: debugBorder ? "1px dashed rgba(24, 144, 255, 0.65)" : "none",
            pointerEvents: "none",
            boxSizing: "border-box",
          }}
        />
      )}

      {/* Warning banner when screen geometry is unconfirmed */}
      {!geometryKnown && (
        <div
          style={{
            position: "absolute",
            top: 10,
            left: "50%",
            transform: "translateX(-50%)",
            zIndex: 11,
            pointerEvents: "auto",
          }}
        >
          <Tooltip title="Разрешение рабочего стола терминала ещё не получено от агента l4desk. Координаты вычисляются по размеру видеокадра.">
            <Alert
              message="Геометрия экрана неизвестна — точность клика снижена"
              type="warning"
              showIcon
              style={{
                backgroundColor: "rgba(255, 251, 230, 0.9)",
                padding: "4px 12px",
                fontSize: 12,
              }}
            />
          </Tooltip>
        </div>
      )}

      {/* Keyboard capture hint */}
      <div
        style={{
          position: "absolute",
          bottom: 10,
          left: "50%",
          transform: "translateX(-50%)",
          zIndex: 11,
          pointerEvents: "none",
        }}
      >
        <div
          style={{
            backgroundColor: "rgba(0, 0, 0, 0.65)",
            color: "#ffffff",
            padding: "2px 10px",
            borderRadius: 4,
            fontSize: 11,
          }}
        >
          ⌨ Ввод с клавиатуры активен (кликните по окну видео для фокуса ввода)
        </div>
      </div>
    </div>
  );
}
