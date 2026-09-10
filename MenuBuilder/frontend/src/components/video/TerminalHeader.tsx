import React from "react";
import { Button, Space, Tag, theme, Tooltip } from "antd";
import {
  EnvironmentOutlined,
  ReloadOutlined,
  SettingOutlined,
  SwapOutlined,
} from "@ant-design/icons";
import { TerminalBadge } from "./TerminalBadge";
import type { DeviceListItem } from "../../api/devices";

export interface TerminalHeaderProps {
  selectedDevice: DeviceListItem;
  address?: string | null;
  onOpenDeviceDrawer: () => void;
  onRefreshDevice?: () => void;
  loadingRefresh?: boolean;
  onOpenPermissionsModal?: () => void;
  canManagePermissions?: boolean;
  isMobile?: boolean;
}

export const TerminalHeader: React.FC<TerminalHeaderProps> = ({
  selectedDevice,
  address,
  onOpenDeviceDrawer,
  onRefreshDevice,
  loadingRefresh = false,
  onOpenPermissionsModal,
  canManagePermissions = false,
  isMobile = false,
}) => {
  const { token } = theme.useToken();
  const isOnline = selectedDevice.status === "online";
  const displayAddress = address?.trim() || "Адрес не указан";

  return (
    <div
      style={{
        display: "flex",
        flexDirection: isMobile ? "column" : "row",
        justifyContent: "space-between",
        alignItems: isMobile ? "flex-start" : "center",
        gap: 12,
        padding: "12px 16px",
        borderRadius: 8,
        backgroundColor: token.colorBgContainer,
        border: `1px solid ${token.colorBorderSecondary}`,
      }}
    >
      {/* Информация о выбранном терминале */}
      <div style={{ display: "flex", flexDirection: "column", gap: 4, minWidth: 0 }}>
        <div style={{ display: "flex", alignItems: "center", gap: 8, flexWrap: "wrap" }}>
          <TerminalBadge
            deviceId={selectedDevice.device_id}
            status={selectedDevice.status}
            size="large"
          />

          <Tag
            color="default"
            style={{
              margin: 0,
              fontSize: 12,
              fontFamily: "monospace",
              color: token.colorTextSecondary,
              backgroundColor: token.colorFillAlter,
            }}
          >
            SN: {selectedDevice.sn}
          </Tag>
        </div>

        {/* Адрес терминала */}
        <div
          style={{
            display: "flex",
            alignItems: "center",
            gap: 6,
            fontSize: 13,
            color: address?.trim() ? token.colorTextSecondary : token.colorTextTertiary,
            wordBreak: "break-word",
          }}
        >
          <EnvironmentOutlined style={{ color: token.colorTextTertiary, flexShrink: 0 }} />
          <span>{displayAddress}</span>
        </div>
      </div>

      {/* Кнопки действий контекста терминала */}
      <div
        style={{
          display: "flex",
          alignItems: "center",
          gap: 8,
          alignSelf: isMobile ? "stretch" : "center",
          justifyContent: isMobile ? "flex-start" : "flex-end",
          flexWrap: "wrap",
        }}
      >
        <Button
          icon={<SwapOutlined />}
          onClick={onOpenDeviceDrawer}
          style={{ flex: isMobile ? 1 : "initial" }}
          aria-label="Сменить терминал"
        >
          Сменить терминал
        </Button>

        {onRefreshDevice && (
          <Tooltip title="Обновить статус устройства">
            <Button
              icon={<ReloadOutlined />}
              onClick={onRefreshDevice}
              loading={loadingRefresh}
              aria-label="Обновить статус устройства"
            />
          </Tooltip>
        )}

        {canManagePermissions && onOpenPermissionsModal && (
          <Tooltip title="Настройка прав доступа к видеопотокам">
            <Button
              icon={<SettingOutlined />}
              onClick={onOpenPermissionsModal}
              aria-label="Настройка прав доступа"
            >
              {!isMobile && "Доступ"}
            </Button>
          </Tooltip>
        )}
      </div>
    </div>
  );
};

export default TerminalHeader;
