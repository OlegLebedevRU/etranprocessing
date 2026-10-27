import { useMemo, useState } from "react";
import { Button, Empty, Input, Segmented, Spin, Tooltip } from "antd";
import { CheckOutlined, ReloadOutlined, SearchOutlined, VideoCameraOutlined } from "@ant-design/icons";
import type { TerminalListProps } from "./TerminalList";
import "./camera-workspace.css";

/** A camera's terminal presence is explicit; it does not claim fresh video. */
export function CameraPicker({ devices, terminalAddresses, terminalNames = {}, selectedDevice, onSelectDevice, loading, onRefresh }: TerminalListProps) {
  const [search, setSearch] = useState("");
  const [filter, setFilter] = useState("all");
  const online = devices.filter(d => d.status === "online").length;
  const filtered = useMemo(() => devices.filter(d => {
    const state = d.status === "online" ? "online" : "offline";
    return (filter === "all" || filter === state) &&
      [d.device_id, d.sn, terminalNames[d.device_id], terminalAddresses[d.device_id]].join(" ").toLowerCase().includes(search.trim().toLowerCase());
  }), [devices, filter, search, terminalNames, terminalAddresses]);
  return <div className="camera-picker">
    <div className="camera-picker-search">
      <Input autoFocus allowClear prefix={<SearchOutlined />} placeholder="Имя, номер или адрес" aria-label="Поиск камеры" value={search} onChange={e => setSearch(e.target.value)} />
      <Tooltip title="Обновить список"><Button aria-label="Обновить список камер" icon={<ReloadOutlined />} loading={loading} onClick={onRefresh} /></Tooltip>
    </div>
    <Segmented block size="small" value={filter} onChange={setFilter} options={[
      { value: "all", label: `Все · ${devices.length}` }, { value: "online", label: `В сети · ${online}` },
      { value: "offline", label: `Офлайн · ${devices.length - online}` },
    ]} />
    <Spin spinning={!!loading}><div className="camera-picker-list" role="list" aria-label="Камеры">
      {!filtered.length && <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="Камеры не найдены" />}
      {filtered.map(device => <div role="listitem" key={device.device_id}><button type="button"
        className={`camera-picker-item ${selectedDevice?.device_id === device.device_id ? "selected" : ""}`}
        aria-current={selectedDevice?.device_id === device.device_id ? "true" : undefined}
        disabled={device.is_blocked} onClick={() => onSelectDevice(device)}>
        <span className="camera-picker-icon"><VideoCameraOutlined /></span>
        <span className="camera-picker-copy"><strong>{terminalNames[device.device_id] || `Камера ${device.device_id}`}</strong>
          <small>{terminalNames[device.device_id] ? `№ ${device.device_id} · ` : ""}{terminalAddresses[device.device_id] || "Адрес не задан"}</small></span>
        <span className={`camera-presence ${device.status === "online" ? "online" : "offline"}`} title="Связь с терминалом">
          <i />{device.status === "online" ? "В сети" : "Офлайн"}</span>
        {selectedDevice?.device_id === device.device_id && <CheckOutlined className="camera-picker-check" />}
      </button></div>)}
    </div></Spin>
  </div>;
}
