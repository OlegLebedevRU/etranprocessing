import React, { useMemo, useState } from "react";
import { Button, Empty, Input, Segmented, Spin, theme, Tooltip } from "antd";
import {
  ReloadOutlined,
  SearchOutlined,
  CloseCircleOutlined,
} from "@ant-design/icons";
import { TerminalListItem } from "./TerminalListItem";
import type { DeviceListItem } from "../../api/devices";

export interface TerminalListProps {
  devices: DeviceListItem[];
  terminalAddresses: Record<number, string>;
  selectedDevice: DeviceListItem | null;
  onSelectDevice: (device: DeviceListItem) => void;
  loading?: boolean;
  onRefresh?: () => void;
  maxHeight?: string | number;
}

type StatusFilter = "all" | "online" | "offline";

export const TerminalList: React.FC<TerminalListProps> = ({
  devices,
  terminalAddresses,
  selectedDevice,
  onSelectDevice,
  loading = false,
  onRefresh,
  maxHeight = "calc(100vh - 220px)",
}) => {
  const { token } = theme.useToken();
  const [searchQuery, setSearchQuery] = useState("");
  const [statusFilter, setStatusFilter] = useState<StatusFilter>("all");

  // Подсчет онлайн / офлайн
  const { onlineCount, offlineCount } = useMemo(() => {
    let online = 0;
    let offline = 0;
    for (const dev of devices) {
      if (dev.status === "online") online++;
      else offline++;
    }
    return { onlineCount: online, offlineCount: offline };
  }, [devices]);

  // Фильтрация устройств по статусу и поисковой строке (по номеру или адресу)
  const filteredDevices = useMemo(() => {
    const q = searchQuery.trim().toLowerCase();
    return devices.filter((dev) => {
      // Фильтр по статусу
      if (statusFilter === "online" && dev.status !== "online") return false;
      if (statusFilter === "offline" && dev.status === "online") return false;

      // Поиск
      if (!q) return true;

      const idMatch = String(dev.device_id).includes(q);
      const snMatch = dev.sn?.toLowerCase().includes(q);
      const address = terminalAddresses[dev.device_id]?.toLowerCase() || "";
      const addressMatch = address.includes(q);

      return idMatch || snMatch || addressMatch;
    });
  }, [devices, searchQuery, statusFilter, terminalAddresses]);

  return (
    <div
      style={{
        display: "flex",
        flexDirection: "column",
        height: "100%",
        gap: 12,
      }}
    >
      {/* Шапка списка с поиском и кнопкой обновления */}
      <div style={{ display: "flex", gap: 8, alignItems: "center" }}>
        <Input
          placeholder="Поиск по № или адресу..."
          prefix={<SearchOutlined style={{ color: token.colorTextQuaternary }} />}
          value={searchQuery}
          onChange={(e) => setSearchQuery(e.target.value)}
          allowClear={{
            clearIcon: <CloseCircleOutlined style={{ color: token.colorTextQuaternary }} />,
          }}
          aria-label="Поиск терминала по номеру или адресу"
          size="middle"
          style={{ flex: 1 }}
        />
        {onRefresh && (
          <Tooltip title="Обновить список терминалов">
            <Button
              icon={<ReloadOutlined />}
              onClick={onRefresh}
              loading={loading}
              aria-label="Обновить список терминалов"
              size="middle"
            />
          </Tooltip>
        )}
      </div>

      {/* Быстрые фильтры по статусу с подсчетом количества */}
      <Segmented<StatusFilter>
        block
        size="small"
        value={statusFilter}
        onChange={(val) => setStatusFilter(val)}
        options={[
          {
            label: `Все (${devices.length})`,
            value: "all",
          },
          {
            label: (
              <span style={{ color: "#389e0d" }}>
                В сети ({onlineCount})
              </span>
            ),
            value: "online",
          },
          {
            label: (
              <span style={{ color: "#cf1322" }}>
                Офлайн ({offlineCount})
              </span>
            ),
            value: "offline",
          },
        ]}
      />

      {/* Скроллируемый список элементов */}
      <div
        role="feed"
        aria-label="Список терминалов видеонаблюдения"
        style={{
          flex: 1,
          maxHeight,
          overflowY: "auto",
          paddingRight: 4,
          display: "flex",
          flexDirection: "column",
          gap: 8,
        }}
      >
        {loading && devices.length === 0 ? (
          <div style={{ padding: "40px 0", textAlign: "center" }}>
            <Spin tip="Загрузка списка терминалов..." />
          </div>
        ) : filteredDevices.length === 0 ? (
          <div style={{ padding: "32px 12px", textAlign: "center" }}>
            <Empty
              image={Empty.PRESENTED_IMAGE_SIMPLE}
              description={
                searchQuery
                  ? "Ни один терминал не соответствует запросу"
                  : statusFilter !== "all"
                  ? `Нет терминалов со статусом «${statusFilter === "online" ? "В сети" : "Офлайн"}»`
                  : "Терминалы не найдены"
              }
            >
              {searchQuery && (
                <Button size="small" onClick={() => setSearchQuery("")}>
                  Сбросить поиск
                </Button>
              )}
            </Empty>
          </div>
        ) : (
          filteredDevices.map((dev) => (
            <TerminalListItem
              key={dev.device_id}
              device={dev}
              address={terminalAddresses[dev.device_id]}
              isSelected={selectedDevice?.device_id === dev.device_id}
              onSelect={onSelectDevice}
              disabled={dev.is_blocked}
            />
          ))
        )}
      </div>
    </div>
  );
};

export default TerminalList;
