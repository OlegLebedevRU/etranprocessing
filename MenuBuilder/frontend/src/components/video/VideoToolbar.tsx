import { useState } from "react";
import type { ReactNode } from "react";
import { Button, Popover, Segmented, Select, Switch, Tooltip, Typography } from "antd";
import { useCameraTools } from "./CameraToolsContext";
import {
  FullscreenOutlined,
  ReloadOutlined,
  SwapOutlined,
  PlayCircleOutlined,
  StopOutlined,
  SettingOutlined,
  VideoCameraOutlined,
  InfoCircleOutlined,
} from "@ant-design/icons";
import type { DeviceListItem } from "../../api/devices";
import type { CodecPolicy, DeviceInventory } from "../../api/video";
import { getCameraName, getDisplayName } from "./SourceSelector";
import {
  RemoteControlPanel,
  type RemoteControlPanelProps,
} from "./RemoteControlPanel";
import type { StreamStage } from "./StreamControls";
import "./video-toolbar.css";
import { videoErrorMessage } from "../../utils/videoErrors";

interface VideoToolbarProps {
  device: DeviceListItem;
  name?: string;
  address?: string;
  inventory: DeviceInventory | null;
  source: string;
  profile: string;
  codecPolicy?: CodecPolicy;
  onCodecPolicy?: (value: CodecPolicy) => void;
  activeCodec?: string;
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
  cameraOnly?: boolean;
  settingsBusy?: boolean;
  cameraStartReady?: boolean;
  cameraMetrics?: ReactNode;
  cameraShowStats?: boolean;
  onCameraShowStats?: (show: boolean) => void;
}

export function VideoToolbar(props: VideoToolbarProps) {
  const camera = useCameraTools();
  const [statusOpen, setStatusOpen] = useState(false);
  const [statusPinned, setStatusPinned] = useState(false);
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
        ? props.statusText || "Подключение…"
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
          disabled={active || stage === "starting" || stage === "stopping"}
          options={[
            { label: "Medium", value: "low" },
            { label: "HD", value: "default" },
          ]}
          style={{ width: "100%" }}
        />
      </label>
      <Typography.Text type="secondary">
        {props.cameraOnly ? "Medium и HD — политики качества; камера может использовать один подтверждённый исходный формат для обеих." : "Medium — умеренный трафик; HD — выше детализация. Формат задаёт источник."}
      </Typography.Text>
      {operator && <label>
        Кодек общего потока
        <Select<CodecPolicy>
          aria-label="Кодек общего потока"
          value={props.codecPolicy || "auto"}
          onChange={props.onCodecPolicy}
          disabled={active || stage === "starting" || stage === "stopping"}
          style={{ width: "100%" }}
          options={[
            { value: "auto", label: props.cameraOnly ? "Auto · H.265 приоритет" : "Auto" },
            { value: "h264", label: "H.264" },
            { value: "h265", label: "H.265", disabled: !props.cameraOnly && !inventory?.video_codecs?.hevc_available },
          ]}
        />
        {props.cameraOnly && <Typography.Text type="secondary">Совместимость камеры и браузера проверяется перед запуском.</Typography.Text>}
        {!props.cameraOnly && !inventory?.video_codecs?.hevc_available && <Typography.Text type="secondary">
          {videoErrorMessage(inventory?.video_codecs?.reason_code)
            || `H.265 недоступен: ${inventory?.video_codecs?.reason_code || "capability_unavailable"}`}
        </Typography.Text>}
      </label>}
      {props.activeCodec && <Typography.Text>Общий поток: {props.activeCodec === "h265" ? "H.265" : "H.264"}</Typography.Text>}
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
      className={`video-toolbar${props.cameraOnly ? " camera-toolbar" : ""}`}
      role="toolbar"
      aria-label="Управление видеотрансляцией"
    >
      <div className="video-toolbar-context">
        <Button
          aria-label={props.cameraOnly ? "Выбрать камеру" : "Сменить терминал"}
          type={props.cameraOnly ? "default" : "primary"}
          icon={props.cameraOnly ? <VideoCameraOutlined /> : <SwapOutlined />}
          onClick={props.onSelectTerminal}
        >
        {props.cameraOnly ? <span className="camera-picker-label">Камеры</span> : "Сменить терминал"}
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
      {operator && !props.cameraOnly && (
        <div className="video-toolbar-source">
          <Select
            aria-label="Источник видео"
            placeholder="Экран или камера"
            value={props.source || undefined}
            onChange={props.onSource}
            loading={props.loadingInventory}
            disabled={active || stage === "starting" || stage === "stopping" || !hasSources}
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
        {(operator || props.cameraOnly) && (
          <Tooltip
            title={
              !active && device.status !== "online"
                ? "Терминал оффлайн"
                : !active && props.cameraOnly && props.cameraStartReady === false
                  ? "Источник камеры пока не подтверждён"
                  : !active && !hasSources
                  ? "Нет доступных источников"
                  : undefined
            }
          >
            <span>
              <Button
                aria-label={
                  stage === "starting" ? "Отменить запуск" : props.cameraOnly ? active ? "Остановить просмотр" : "Начать просмотр" : active ? "Остановить трансляцию" : "Запустить трансляцию"
                }
                type="primary"
                danger={active}
                icon={active ? <StopOutlined /> : <PlayCircleOutlined />}
                loading={stage === "stopping"}
                disabled={
                  stage !== "starting" && !active && (device.status !== "online" || !hasSources || !!props.settingsBusy || props.cameraOnly && props.cameraStartReady === false)
                }
                onClick={active || stage === "starting" ? props.onStop : props.onStart}
              >
                <span className={props.cameraOnly ? "camera-play-label" : undefined}>{stage === "starting" ? props.cameraOnly ? "Отмена" : "Отменить запуск" : props.cameraOnly ? active ? "Стоп" : "Смотреть" : active ? "Стоп" : "Запустить"}</span>
              </Button>
            </span>
          </Tooltip>
        )}
        {props.cameraOnly && camera?.tools}
        {props.cameraOnly ? <Popover destroyOnHidden placement="bottomRight" trigger={["hover", "focus"]} open={statusOpen || statusPinned}
          onOpenChange={setStatusOpen} content={<div className="camera-status-content">
            {camera?.summary}
            {props.cameraMetrics && <div className="camera-status-metrics">{props.cameraMetrics}</div>}
            <label style={{ display: "flex", gap: 8, alignItems: "center", marginTop: 8 }}>
              <Switch size="small" aria-label="Статистика поверх видео" checked={props.cameraShowStats} onChange={props.onCameraShowStats} />Статистика поверх видео
            </label>
          </div>}>
          <button type="button" aria-label="Состояние камеры и потока" title={status} aria-expanded={statusOpen || statusPinned}
            className={`video-stream-status ${live ? "live" : running || stage === "starting" ? "waiting" : stage === "failed" ? "failed" : ""}`}
            onClick={() => { setStatusPinned(!statusPinned); setStatusOpen(!statusPinned); }}
            onKeyDown={e => { if (e.key === "Escape") { setStatusPinned(false); setStatusOpen(false); e.currentTarget.blur(); } }}>
            <span className="video-status-dot" /><span role="status">{status}</span> <InfoCircleOutlined />
          </button>
        </Popover> : <Tooltip
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
        </Tooltip>}
        {operator && !props.cameraOnly && <RemoteControlPanel {...props.control} compact />}
        {!props.cameraOnly && <Popover trigger="click" placement="bottomRight" content={settings}>
          <Button aria-label="Настройки видео" icon={<SettingOutlined />} />
        </Popover>}
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
