import React, { useState } from "react";
import { theme } from "antd";
import { EnvironmentOutlined } from "@ant-design/icons";
import { TerminalBadge } from "./TerminalBadge";
import type { DeviceListItem } from "../../api/devices";

export interface TerminalListItemProps {
  device: DeviceListItem;
  address?: string | null;
  isSelected: boolean;
  onSelect: (device: DeviceListItem) => void;
  disabled?: boolean;
}

export const TerminalListItem: React.FC<TerminalListItemProps> = ({
  device,
  address,
  isSelected,
  onSelect,
  disabled = false,
}) => {
  const { token } = theme.useToken();
  const [isHovered, setIsHovered] = useState(false);
  const [isFocused, setIsFocused] = useState(false);

  const isOnline = device.status === "online";
  const displayAddress = address?.trim() || "Адрес не указан";

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (disabled) return;
    if (e.key === "Enter" || e.key === " ") {
      e.preventDefault();
      onSelect(device);
    }
  };

  // Вычисляем стили состояния
  let bg = "transparent";
  let border = `1px solid ${token.colorBorderSecondary}`;
  let borderLeft = "3px solid transparent";

  if (isSelected) {
    bg = token.colorPrimaryBg;
    border = `1px solid ${token.colorPrimaryBorder}`;
    borderLeft = `4px solid ${token.colorPrimary}`;
  } else if (isHovered && !disabled) {
    bg = token.colorFillTertiary;
    border = `1px solid ${token.colorBorder}`;
  }

  return (
    <div
      role="button"
      tabIndex={disabled ? -1 : 0}
      aria-pressed={isSelected}
      aria-disabled={disabled}
      aria-label={`Терминал №${device.device_id}, ${isOnline ? "на связи" : "не в сети"}. Адрес: ${displayAddress}`}
      title={`Терминал №${device.device_id} (${isOnline ? "В сети" : "Не в сети"})\nАдрес: ${displayAddress}`}
      onClick={() => {
        if (!disabled) onSelect(device);
      }}
      onKeyDown={handleKeyDown}
      onMouseEnter={() => setIsHovered(true)}
      onMouseLeave={() => setIsHovered(false)}
      onFocus={() => setIsFocused(true)}
      onBlur={() => setIsFocused(false)}
      style={{
        display: "flex",
        flexDirection: "column",
        gap: 6,
        padding: "10px 12px",
        borderRadius: 8,
        backgroundColor: bg,
        border,
        borderLeft,
        cursor: disabled ? "not-allowed" : "pointer",
        opacity: disabled ? 0.5 : 1,
        transition: "all 0.15s ease",
        outline: isFocused ? `2px solid ${token.colorPrimary}` : "none",
        outlineOffset: 1,
        position: "relative",
      }}
    >
      {/* Первая строка: только номер терминала в бейдже со статусом (без текста online/offline) */}
      <div
        style={{
          display: "flex",
          alignItems: "center",
          justifyContent: "space-between",
          gap: 8,
        }}
      >
        <TerminalBadge
          deviceId={device.device_id}
          status={device.status}
          size="default"
        />

        {/* Дополнительный визуальный маркер активности выбранного терминала */}
        {isSelected && (
          <span
            style={{
              fontSize: 11,
              fontWeight: 600,
              color: token.colorPrimary,
              backgroundColor: "rgba(22, 119, 255, 0.1)",
              padding: "1px 6px",
              borderRadius: 4,
            }}
          >
            Выбран
          </span>
        )}
      </div>

      {/* Вторая строка: адрес терминала, визуально менее акцентирован, с переносом */}
      <div
        style={{
          fontSize: 12,
          color: address?.trim() ? token.colorTextSecondary : token.colorTextQuaternary,
          lineHeight: 1.35,
          wordBreak: "break-word",
          display: "flex",
          alignItems: "flex-start",
          gap: 5,
        }}
      >
        <EnvironmentOutlined
          style={{
            fontSize: 12,
            marginTop: 2,
            flexShrink: 0,
            color: address?.trim() ? token.colorTextTertiary : token.colorTextQuaternary,
          }}
          aria-hidden="true"
        />
        <span>{displayAddress}</span>
      </div>
    </div>
  );
};

export default TerminalListItem;
