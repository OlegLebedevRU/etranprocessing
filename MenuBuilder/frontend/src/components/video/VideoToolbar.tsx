import { Button, Popover, Segmented, Select, Tooltip, Typography } from "antd";
import {
  FullscreenOutlined,
  ReloadOutlined,
  SwapOutlined,
  PlayCircleOutlined,
  StopOutlined,
  SettingOutlined,
} from "@ant-design/icons";
import type { DeviceListItem } from "../../api/devices";
import type { DeviceInventory } from "../../api/video";
import { getCameraName, getDisplayName } from "./SourceSelector";
import {
  RemoteControlPanel,
  type RemoteControlPanelProps,
} from "./RemoteControlPanel";
import type { StreamStage } from "./StreamControls";
import "./video-toolbar.css";

interface VideoToolbarProps {
  device: DeviceListItem;
  name?: string;
  address?: string;
  inventory: DeviceInventory | null;
  source: string;
  profile: string;
  onSource: (value: string) => void;
  onProfile: (value: string) => void;
  loadingInventory: boolean;
  onRefresh: () => void;
  onSelectTerminal: () => void;
  stage: StreamStage;
  active: boolean;
  freshFrames: boolean;
  statusText: string;
  sourceLabel?: string;
  resolution: string;
  operator: boolean;
  onStart: () => void;
  onStop: () => void;
  control: RemoteControlPanelProps;
  displayMode: "fit" | "native";
  onDisplayMode: (value: "fit" | "native") => void;
  onFullscreen: () => void;
}

export function VideoToolbar(props: VideoToolbarProps) {
  const { device, inventory, active, stage, operator, freshFrames } = props;
  const hasSources = !!(
    inventory?.displays.length || inventory?.cameras.length
  );
  const running = active && stage === "running";
  const live = running && freshFrames;
  const status = live
    ? "В эфире"
    : running
      ? "Ожидание кадров"
      : stage === "starting"
        ? "Подключение…"
        : stage === "stopping"
          ? "Остановка…"
          : stage === "failed"
            ? "Ошибка"
            : "Не запущена";
  const settings = (
    <div
      style={{ display: "flex", flexDirection: "column", gap: 12, width: 250 }}
    >
      <label>
        Качество{" "}
        <Select
          aria-label="Выбор качества трансляции"
          value={props.profile}
          onChange={props.onProfile}
          disabled={active}
          options={[
            { label: "Medium", value: "low" },
            { label: "HD", value: "default" },
          ]}
          style={{ width: "100%" }}
        />
      </label>
      <label>
        Масштаб{" "}
        <Segmented
          aria-label="Масштаб видео в браузере"
          value={
            props.control.rcStatus === "active" ? "fit" : props.displayMode
          }
          onChange={(value) => props.onDisplayMode(value as "fit" | "native")}
          options={[
            { label: "Вписать", value: "fit" },
            {
              label: "Исходный размер",
              value: "native",
              disabled: props.control.rcStatus === "active",
            },
          ]}
        />
      </label>
      <Button
        icon={<ReloadOutlined />}
        loading={props.loadingInventory}
        onClick={props.onRefresh}
      >
        Обновить источники и статус
      </Button>
      <Typography.Text type="secondary" copyable={{ text: device.sn }}>
        SN: {device.sn}
      </Typography.Text>
      {props.address && <Typography.Text>{props.address}</Typography.Text>}
    </div>
  );
  return (
    <div
      className="video-toolbar"
      role="toolbar"
      aria-label="Управление видеотрансляцией"
    >
      <div className="video-toolbar-context">
        <Button
          aria-label="Сменить терминал"
          type="primary"
          icon={<SwapOutlined />}
          onClick={props.onSelectTerminal}
        >
          Сменить терминал
        </Button>
        <Tooltip
          title={[props.name, props.address, device.sn]
            .filter(Boolean)
            .join(" · ")}
        >
          <Typography.Text ellipsis className="video-terminal-label">
            № {device.device_id}
            {props.name ? ` · ${props.name}` : ""}
          </Typography.Text>
        </Tooltip>
      </div>
      {operator && (
        <div className="video-toolbar-source">
          <Select
            aria-label="Источник видео"
            placeholder="Экран или камера"
            value={props.source || undefined}
            onChange={props.onSource}
            loading={props.loadingInventory}
            disabled={active || !hasSources}
            style={{ width: "100%" }}
            options={[
              ...(inventory?.displays || []).map((display, index) => ({
                label: getDisplayName(display, index),
                value: `desktop:${display.id || display.desktop_id || String(index)}`,
              })),
              ...(inventory?.cameras || []).map((camera, index) => ({
                label: getCameraName(camera, index),
                value: `usb-camera:${camera.id || camera.camera_id || String(index)}`,
                disabled: camera.available === false,
              })),
            ]}
          />
        </div>
      )}
      <div className="video-toolbar-actions">
        {operator && (
          <Tooltip
            title={
              !active && device.status !== "online"
                ? "Терминал оффлайн"
                : !active && !hasSources
                  ? "Нет доступных источников"
                  : undefined
            }
          >
            <span>
              <Button
                aria-label={
                  active ? "Остановить трансляцию" : "Запустить трансляцию"
                }
                type="primary"
                danger={active}
                icon={active ? <StopOutlined /> : <PlayCircleOutlined />}
                loading={stage === "starting" || stage === "stopping"}
                disabled={
                  !active && (device.status !== "online" || !hasSources)
                }
                onClick={active ? props.onStop : props.onStart}
              >
                {active ? "Стоп" : "Запустить"}
              </Button>
            </span>
          </Tooltip>
        )}
        <Tooltip
          title={[props.statusText, props.sourceLabel, props.resolution]
            .filter(Boolean)
            .join(" · ")}
        >
          <span
            tabIndex={0}
            role="status"
            className={`video-stream-status ${live ? "live" : running ? "waiting" : ""}`}
          >
            <span className="video-status-dot" />
            {status}
          </span>
        </Tooltip>
        {operator && <RemoteControlPanel {...props.control} compact />}
        <Popover trigger="click" placement="bottomRight" content={settings}>
          <Button aria-label="Настройки видео" icon={<SettingOutlined />} />
        </Popover>
        <Tooltip title="Во весь экран">
          <Button
            aria-label="Полноэкранный режим видео"
            icon={<FullscreenOutlined />}
            disabled={!active}
            onClick={props.onFullscreen}
          />
        </Tooltip>
      </div>
    </div>
  );
}
