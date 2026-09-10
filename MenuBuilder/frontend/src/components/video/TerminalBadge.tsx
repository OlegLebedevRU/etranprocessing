import React from "react";
import { theme } from "antd";

export interface TerminalBadgeProps {
  deviceId: number;
  status: "online" | "offline" | "unknown" | string;
  size?: "small" | "default" | "large";
  showDot?: boolean;
  className?: string;
  style?: React.CSSProperties;
}

/**
 * Компактный бейдж с номером терминала.
 * Цвет отражает состояние связи (мягкий зеленый — online, мягкий красный — offline)
 * БЕЗ отдельного текстового статуса online/offline, но с полной доступностью через title и aria-label.
 */
export const TerminalBadge: React.FC<TerminalBadgeProps> = ({
  deviceId,
  status,
  size = "default",
  showDot = true,
  className,
  style,
}) => {
  const isOnline = status === "online";
  const isOffline = status === "offline";

  // Мягкие пастельные цвета согласно требованиям
  const themeColors = isOnline
    ? {
        bg: "#f6ffed",
        border: "#b7eb8f",
        text: "#237804",
        dot: "#52c41a",
        dotPulse: "rgba(82, 196, 26, 0.2)",
        statusName: "на связи (online)",
      }
    : isOffline
    ? {
        bg: "#fff1f0",
        border: "#ffa39e",
        text: "#cf1322",
        dot: "#ff4d4f",
        dotPulse: "rgba(255, 77, 79, 0.2)",
        statusName: "не на связи (offline)",
      }
    : {
        bg: "#fafafa",
        border: "#d9d9d9",
        text: "#595959",
        dot: "#bfbfbf",
        dotPulse: "transparent",
        statusName: "статус неизвестен",
      };

  const padVertical = size === "small" ? 2 : size === "large" ? 6 : 4;
  const padHorizontal = size === "small" ? 6 : size === "large" ? 12 : 8;
  const fontSize = size === "small" ? 11 : size === "large" ? 14 : 12;
  const dotSize = size === "small" ? 6 : size === "large" ? 8 : 7;

  return (
    <span
      className={className}
      title={`Терминал №${deviceId}: ${themeColors.statusName}`}
      aria-label={`Терминал №${deviceId}, ${themeColors.statusName}`}
      style={{
        display: "inline-flex",
        alignItems: "center",
        gap: size === "small" ? 4 : 6,
        padding: `${padVertical}px ${padHorizontal}px`,
        borderRadius: 6,
        backgroundColor: themeColors.bg,
        border: `1px solid ${themeColors.border}`,
        color: themeColors.text,
        fontWeight: 600,
        fontSize,
        lineHeight: 1.2,
        userSelect: "none",
        whiteSpace: "nowrap",
        ...style,
      }}
    >
      {showDot && (
        <span
          aria-hidden="true"
          style={{
            width: dotSize,
            height: dotSize,
            borderRadius: "50%",
            backgroundColor: themeColors.dot,
            display: "inline-block",
            flexShrink: 0,
            boxShadow: isOnline ? `0 0 0 2px ${themeColors.dotPulse}` : "none",
          }}
        />
      )}
      <span>№ {deviceId}</span>
    </span>
  );
};

export default TerminalBadge;
