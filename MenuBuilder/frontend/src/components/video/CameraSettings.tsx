import { useCallback, useEffect, useRef, useState } from "react";
import type { ReactNode } from "react";
import { Alert, Button, Collapse, Descriptions, Drawer, Dropdown, Modal, Select, Space, Switch, Tooltip, Typography } from "antd";
import { DownOutlined, MoreOutlined, SlidersOutlined, SettingOutlined, InfoCircleOutlined, LoadingOutlined } from "@ant-design/icons";
import type { CodecPolicy } from "../../api/video";
import { CameraToolsContext } from "./CameraToolsContext";
import { CameraIrcutDiagnostic } from "./CameraIrcutDiagnostic";
import "./camera-workspace.css";
import { commandCamera, getCamera, getCameraCommand, type CameraCommand, type CameraObservation, type CameraProbe, type CameraPatch } from "../../api/cameras";
import { CameraFields } from "./CameraFields";
import { CameraConnection } from "./CameraConnection";
import { CameraCredentials } from "./CameraCredentials";
import { cameraObservedFormat, cameraVideoSettings, configFresh, observationAge, powerFresh } from "../../utils/cameraState";
import { cameraErrorMessage } from "../../utils/cameraErrors";

type Pending = { until: number; task?: string; action?: CameraCommand["action"]; operation?: string; cancellation?: string };
const pause = (signal: AbortSignal) => new Promise<void>((resolve, reject) => {
  const abort = () => { clearTimeout(timer); reject(new Error("aborted")); };
  const timer = setTimeout(() => { signal.removeEventListener("abort", abort); resolve(); }, 1000);
  signal.addEventListener("abort", abort, { once: true });
  if (signal.aborted) abort();
});

export function CameraSettings({ device, org, operator, viewing, freshFrames = false, onBusy, enabled = true, children,
  codecPolicy = "auto", onCodecPolicy, adaptiveQuality, onAdaptiveQuality, adaptationAvailable = false, onRefresh }: {
  device: number; org: number; operator: boolean; viewing: boolean; freshFrames?: boolean; onBusy: (busy: boolean) => void;
  enabled?: boolean; children?: ReactNode; codecPolicy?: CodecPolicy; onCodecPolicy?: (value: CodecPolicy) => void;
  adaptiveQuality?: boolean; onAdaptiveQuality?: (value: boolean) => void; adaptationAvailable?: boolean;
  onRefresh?: () => void;
}) {
  const [panel, setPanel] = useState<"quality" | "image" | "connection" | "ircut" | null>(null);
  const key = `camera-command:${org}:${device}`;
  const [pending, setPending] = useState<Pending | null>(() => {
    try { const item = JSON.parse(sessionStorage.getItem(key) || "null");
      return item?.until > Date.now() ? item : null; } catch { return null; }
  });
  const [data, setData] = useState<CameraObservation | null>(null);
  const [notice, setNotice] = useState("");
  const [working, setWorking] = useState(false);
  const [diagnosticBusy, setDiagnosticBusy] = useState(() => {
    try { return !!sessionStorage.getItem(`camera-ircut:${org}:${device}`); } catch { return false; }
  });
  const [credentialBusy, setCredentialBusy] = useState(false);
  const [credentialLockUntil, setCredentialLockUntil] = useState(() => { try { const item = JSON.parse(sessionStorage.getItem(`camera-credentials:${org}:${device}`) || "null");
    return Number.isFinite(item?.until) && item.until <= Date.now() + 90000 ? item.until : 0;
  } catch { return 0; } });
  const onCredentialBusy = useCallback((value: boolean) => {
    setCredentialBusy(value);
    if (!value) { try { const item = JSON.parse(sessionStorage.getItem(`camera-credentials:${org}:${device}`) || "null");
      setCredentialLockUntil(Number.isFinite(item?.until) && item.until <= Date.now() + 90000 ? item.until : 0);
    } catch { setCredentialLockUntil(0); } }
  }, [org, device]);
  const [now, setNow] = useState(Date.now());
  const lifecycle = useRef<AbortController | null>(null);
  const mutation = useRef(false);
  const cancelling = useRef(false);
  const cancelled = useRef<string | null>(null);
  const [cancelWorking, setCancelWorking] = useState(false);
  const confirmation = useRef<{ destroy: () => void } | null>(null);
  const saved = useCallback((item: Pending | null) => {
    // If durable blocking cannot be saved, do not send a disruptive command.
    if (item) sessionStorage.setItem(key, JSON.stringify(item));
    else { try { sessionStorage.removeItem(key); } catch { /* No command is sent during cleanup. */ } }
    setPending(item);
  }, [key]);
  const busy = working || diagnosticBusy || credentialBusy || credentialLockUntil > now || !!pending && pending.until > now;
  useEffect(() => { onBusy(busy); return () => onBusy(false); }, [busy, onBusy]);
  useEffect(() => {
    if (!enabled) return;
    const controller = new AbortController(); lifecycle.current = controller;
    let fetching = false;
    const load = async () => {
      if (fetching || mutation.current) return;
      fetching = true;
      try { const state = await getCamera(device, controller.signal);
        if (!controller.signal.aborted && !mutation.current) setData(state);
      } catch (error: any) {
        if (!controller.signal.aborted) setNotice(error?.response?.data?.detail?.message ||
          (typeof error?.response?.data?.detail === "string" ? error.response.data.detail : "Состояние камеры недоступно"));
      } finally { fetching = false; }
    };
    void load();
    const poll = setInterval(() => { setNow(Date.now()); void load(); }, 5000);
    return () => { controller.abort(); clearInterval(poll); confirmation.current?.destroy(); };
  }, [device, enabled]);
  const snapshot = data?.snapshot ?? null;
  const camera = snapshot?.cameras.find(item => item.camera_id === "cam:ip000001");
  const configValid = configFresh(snapshot, camera, now);
  const powerValid = powerFresh(snapshot, camera, now);
  const settingsBlocked = !operator || busy || viewing || data?.lease_active !== false || data?.terminal_online !== true;
  const probeAge = (probe?: CameraProbe | null) => observationAge(
    probe?.observed_at, probe?.status === "ok", now, snapshot?.freshness_max_age_sec ?? 330);

  async function apply(action: CameraCommand["action"], value?: string, patch?: CameraPatch, expectedRevision?: string, connection?: Pick<CameraCommand, "candidate_mac" | "discovery_generation">): Promise<boolean> {
    if (mutation.current || busy || !operator || !lifecycle.current) return false;
    if (action !== "refresh" && settingsBlocked) return false;
    const signal = lifecycle.current.signal;
    if (signal.aborted) return false;
    mutation.current = true; setWorking(true); setNotice("");
    let accepted = false;
    let submitting = true;
    const operation = crypto.randomUUID();
    try {
      const command: CameraCommand = { ...connection, operation_id: operation, action, value };
      if (action !== "refresh") command.allow_disruptive = true;
      if (action === "quality" || action === "day_night" || action === "patch") {
        if (!configValid || !camera?.revision) throw new Error("Обновите состояние камеры");
        command.expected_revision = expectedRevision ?? camera.revision;
        if (action === "patch") command.patch = patch;
      }
      if (action === "power") {
        if (!powerValid) throw new Error("Обновите состояние питания");
        command.expected_power_on = camera!.power!.enabled!;
      }
      if (["bind", "unbind", "clear_credentials"].includes(action)) {
        if (!camera?.revision) throw new Error("Обновите состояние камеры");
        command.expected_revision = camera.revision;
      }
      const lock = { until: Date.now() + 90000, action, operation: command.operation_id };
      saved(lock);
      accepted = true; // POST may commit even when its response is lost.
      const task = await commandCamera(device, command);
      submitting = false;
      saved({ ...lock, task: task.id });
      if (signal.aborted) return false;
      const deadline = Date.now() + 45000;
      let result;
      while (Date.now() < deadline) {
        if (cancelled.current === command.operation_id) return false;
        const current = await getCameraCommand(device, task.id, signal);
        if (cancelled.current === operation) return false;
        if (current.results?.length) { result = current.results[0]; break; }
        if ([4, 5, 6, 7].includes(current.status)) throw new Error("Команда завершилась без подтверждённого результата");
        if (action === "discover") {
          const progress = await getCamera(device, signal);
          if (!signal.aborted && progress.snapshot?.operation_id === task.id) setData(progress);
        }
        await pause(signal);
      }
      if (!result) throw new Error("Результат команды пока не подтверждён");
      const body = typeof result.result === "string" ? JSON.parse(result.result) : result.result;
      let correlated = false;
      const eventDeadline = Date.now() + 10000;
      while (Date.now() < eventDeadline) {
        if (cancelled.current === command.operation_id) return false;
        const state = await getCamera(device, signal);
        if (signal.aborted || cancelled.current === operation) return false;
        setData(state);
        if (state.snapshot?.operation_id === task.id && state.snapshot.snapshot_id === body.snapshot_id) {
          correlated = true; break;
        }
        await pause(signal);
      }
      if (!correlated) throw new Error("Ответ получен, но соответствующее обновление состояния камеры ещё не подтверждено");
      saved(null); accepted = false;
      const failure = result.status_code !== 200;
      setNotice(failure ? `Не выполнено: ${body.error || body.status || result.status_code}${
        body.applied_fields?.length ? "; часть изменений применена — проверьте состояние" : ""}` :
        action === "discover" ? "Поиск завершён. Выберите камеру с подтверждённым port-2." :
        action === "bind" ? "Привязка камеры подтверждена" :
        action === "clear_credentials" ? "Возврат к штатным учётным данным подтверждён" :
        action === "power" ? "Состояние выхода питания подтверждено. После включения дождитесь загрузки камеры и обновите состояние." :
          action === "refresh" ? "Состояние обновлено" : "Настройки применены и подтверждены камерой");
      return !failure;
    } catch (error: any) {
      if (signal.aborted || cancelled.current === operation) return false;
      // Definite admission denial happens before task creation; a lost response stays blocked.
      const status = error?.response?.status;
      if (!accepted || submitting && status && status >= 400 && status < 500) saved(null);
      setNotice(cameraErrorMessage(error));
      return false;
    } finally {
      mutation.current = false;
      if (!signal.aborted) setWorking(false);
    }
  }
  async function cancelDiscovery() {
    const target = pending;
    if (!operator || !target?.operation || target.action !== "discover" || target.cancellation || cancelling.current || !lifecycle.current) return;
    const signal = lifecycle.current.signal;
    if (signal.aborted) return;
    cancelling.current = true; setCancelWorking(true);
    const operation = crypto.randomUUID();
    let submitting = true;
    try {
      saved({ ...target, cancellation: operation });
      const task = await commandCamera(device, { action: "cancel_discovery", operation_id: operation,
        target_operation_id: target.operation, allow_disruptive: true });
      submitting = false;
      if (task.id !== operation) throw new Error("Не подтверждён идентификатор отмены");
      const deadline = Date.now() + 30000;
      while (Date.now() < deadline) {
        const current = await getCameraCommand(device, task.id, signal);
        const result = current.results?.[0];
        if (result) {
          const body = typeof result.result === "string" ? JSON.parse(result.result) : result.result;
          const state = await getCamera(device, signal);
          if (result.status_code !== 200) throw new Error("Отмена поиска не подтверждена");
          if (body.operation_id === operation && state.snapshot?.operation_id === operation && state.snapshot.snapshot_id === body.snapshot_id) {
            cancelled.current = target.operation;
            if (!signal.aborted) { setData(state); saved(null); setNotice("Отмена поиска подтверждена"); }
            return;
          }
        } else if ([4, 5, 6, 7].includes(current.status)) break;
        await pause(signal);
      }
      throw new Error("Результат отмены пока не подтверждён. Автоматического повтора нет.");
    } catch (error: any) {
      if (!signal.aborted) {
        const status = error?.response?.status;
        if (submitting && status >= 400 && status < 500) saved(target);
        setNotice(error?.response?.data?.detail?.message || error?.message || "Результат отмены неизвестен; повтор не отправлен");
      }
    } finally { cancelling.current = false; if (!signal.aborted) setCancelWorking(false); }
  }
  function power(action: string) {
    if (action === "on") { void apply("power", action); return; }
    confirmation.current = Modal.confirm({ title: action === "off" ? "Выключить питание камеры?" : "Перезапустить питание камеры?",
      content: action === "off" ? "Камера останется выключенной до команды включения." : "Питание выключится на 3 секунды. Затем потребуется время на загрузку камеры.",
      okText: action === "off" ? "Выключить" : "Перезапустить", cancelText: "Отмена",
      onOk: () => apply("power", action) });
  }
  if (!enabled) return <>{children}</>;
  const blockedReason = !operator ? "Только для оператора" : busy ? "Ожидается результат команды" :
    data?.terminal_online !== true ? "Нет связи с терминалом" : viewing ? "Сначала остановите просмотр" :
      data?.lease_active !== false ? "Камера занята другой сессией или состояние сессии неизвестно" :
        !configValid ? "Обновите состояние камеры" : null;
  const commandBlockedReason = !operator ? "Только для оператора" : busy ? "Ожидается результат команды" :
    data?.terminal_online !== true ? "Нет связи с терминалом" : viewing ? "Сначала остановите просмотр" :
      data?.lease_active !== false ? "Камера занята другой сессией или состояние сессии неизвестно" : null;
  const night = camera?.telemetry?.metrics?.night_enabled;
  const dayNightAvailable = !!camera?.capabilities?.day_night?.write;
  const powerAvailable = !!camera?.capabilities?.power?.available;
  const needsAccess = camera?.status === "auth_required" || camera?.discovery?.state === "auth_required";
  const missingCamera = camera?.discovery?.state === "not_found" && camera?.status !== "ready";
  const controlReason = (available: boolean, needsConfig = false) => commandBlockedReason ||
    (!available ? "Камера не подтвердила поддержку" : needsConfig && !configValid ? "Обновите состояние камеры" : null);
  const summary = <Descriptions size="small" column={1} items={[
    { key: "connection", label: "Доступ", children: !data?.terminal_online ? "Терминал офлайн" : needsAccess ? "Нужен пароль" : camera?.status === "ready" ? "Камера доступна" : "Пока не подтверждён" },
    { key: "requested", label: "Настройка", children: configValid ? cameraVideoSettings(camera) : "Нет свежих данных" },
    { key: "actual", label: "Поток камеры", children: cameraObservedFormat(snapshot, camera, now) },
    { key: "browser", label: "Просмотр", children: freshFrames ? "Кадры поступают" : "Нет подтверждённых кадров в браузере" },
    { key: "power", label: "Выход питания", children: powerValid ? camera?.power?.enabled ? "Включён" : "Выключен" : "Не подтверждён" },
    { key: "night", label: "Ночной режим", children: probeAge(camera?.telemetry) === "Актуально" && (night === 0 || night === 1) ? night === 1 ? "Включён" : "Выключен" : "Не подтверждён" },
    { key: "age", label: "Состояние получено", children: observationAge(snapshot?.received_at ?? undefined, !!snapshot, now, snapshot?.freshness_max_age_sec ?? 330) },
    { key: "details", label: "Подробности", children: <Collapse ghost size="small" items={[{ key: "details", label: "Устройство и возможности", children: <Space orientation="vertical" size={3}>
      <Typography.Text>OpenIPC {camera?.identity?.openipc_version || "—"} · Majestic {camera?.identity?.majestic_version || "—"}</Typography.Text>
      <Typography.Text>Port-2 · {camera?.discovery?.ethernet_mode || "Режим неизвестен"}</Typography.Text>
      <Typography.Text type="secondary">Подсветка/PWM: аппаратная поддержка не подтверждена.</Typography.Text>
    </Space> }]} /> },
  ]} />;
  const menuItems = [
    { key: "night", type: "group" as const, label: "Ночной режим", children: [
      ...(["auto", "night", "day"] as const).map(mode => {
        const reason = controlReason(dayNightAvailable && !!camera?.capabilities?.day_night?.modes?.includes(mode), true);
        return { key: `night:${mode}`, disabled: !!reason, label: <Tooltip title={reason}>{mode === "auto" ? "Авто" : mode === "night" ? "Включить" : "Выключить"}</Tooltip> };
      }),
      { key: "ircut", label: <Tooltip title={diagnosticBusy ? null : commandBlockedReason}>Проверить IR-cut…</Tooltip>, disabled: !!commandBlockedReason && !diagnosticBusy },
    ] },
    { key: "power", type: "group" as const, label: "Питание камеры", children: (["on", "off", "restart"] as const).map(action => {
      const reason = controlReason(powerAvailable && !!camera?.capabilities?.power?.actions?.includes(action)) ||
        (!powerValid ? "Состояние питания не подтверждено" : action === "restart" && !camera?.power?.enabled ? "Питание выключено" : null);
      return { key: `power:${action}`, danger: action !== "on", disabled: !!reason,
        label: <Tooltip title={reason}>{action === "on" ? "Включить" : action === "off" ? "Выключить" : "Перезапустить"}</Tooltip> };
    }) },
    { type: "divider" as const }, { key: "connection", label: "Подключение…" },
    { key: "refresh", label: "Обновить", disabled: !operator || busy },
  ];
  const tools = <span className="camera-tools">
    <Button icon={<SettingOutlined />} onClick={() => setPanel("quality")} aria-label="Качество камеры"><span className="camera-tool-label">Качество</span><DownOutlined /></Button>
    <Button icon={<SlidersOutlined />} onClick={() => setPanel("image")} aria-label="Изображение камеры"><span className="camera-tool-label">Изображение</span></Button>
    <Dropdown menu={{ items: menuItems, onClick: ({ key }) => {
      if (key === "connection" || key === "ircut") setPanel(key);
      else if (key === "refresh") { void apply("refresh"); onRefresh?.(); }
      else if (key.startsWith("night:")) void apply("day_night", key.slice(6));
      else if (key.startsWith("power:")) power(key.slice(6));
    } }} trigger={["click"]}><Button aria-label="Команды камеры" icon={<MoreOutlined />} /></Dropdown>
  </span>;
  return <CameraToolsContext.Provider value={{ tools, summary }}>
    {children || tools}
    {(notice || busy || needsAccess || missingCamera) && <div className="camera-operation-notice" role="status">
      {busy ? <LoadingOutlined /> : <InfoCircleOutlined />}
      <span>{notice || (diagnosticBusy ? "Проверка IR-cut: ожидается результат" : busy ? "Ожидается подтверждение команды" : needsAccess ? "Камере нужен пароль" : "Камера не найдена")}</span>
      {(needsAccess || missingCamera) && <Button size="small" type="link" onClick={() => setPanel("connection")}>{needsAccess ? "Доступ" : "Подключить"}</Button>}
      {!busy && <Button size="small" type="text" aria-label="Закрыть сообщение камеры" onClick={() => setNotice("")}>×</Button>}
    </div>}
    <Drawer open={panel !== null} onClose={() => setPanel(null)} size={400} styles={{ body: { padding: 16 } }}
      title={panel === "quality" ? "Качество" : panel === "image" ? "Изображение" : panel === "ircut" ? "Проверка IR-cut" : "Подключение"}>
      <div className="camera-control-panel">
        {panel !== "connection" && panel !== "ircut" && blockedReason && <Alert title={blockedReason} type="info" showIcon action={operator && !busy && !viewing ? <Button size="small" onClick={() => void apply("refresh")}>Обновить</Button> : undefined} />}
        <div hidden={panel !== "quality"}>
          <Space orientation="vertical" size={12} style={{ width: "100%" }}>
            <CameraFields key={`video:${device}`} group="video" camera={camera} disabled={settingsBlocked || !configValid} onApply={(patch, revision) => apply("patch", undefined, patch, revision)} />
            <label>Кодек подключения <Select<CodecPolicy> aria-label="Кодек общего потока" value={codecPolicy} onChange={onCodecPolicy} disabled={!operator || viewing || busy}
              style={{ width: "100%" }} options={[{ value: "auto", label: "Auto · H.265 первым" }, { value: "h265", label: "H.265" }, { value: "h264", label: "H.264" }]} /></label>
            <Tooltip title={!adaptationAvailable ? "Доступно инициатору активной трансляции" : "Меняет битрейт, сохраняя выбранный размер"}>
              <Space><Switch size="small" aria-label="Автоадаптация по приёму" checked={adaptiveQuality} disabled={!adaptationAvailable} onChange={onAdaptiveQuality} /><span>Автобитрейт</span></Space>
            </Tooltip>
            <Typography.Text type="secondary">Фактически: {cameraObservedFormat(snapshot, camera, now)}</Typography.Text>
          </Space>
        </div>
        <div hidden={panel !== "image"}><CameraFields key={`image:${device}`} group="image" camera={camera} disabled={settingsBlocked || !configValid} onApply={(patch, revision) => apply("patch", undefined, patch, revision)} /></div>
        {panel === "connection" && <>
          {commandBlockedReason && <Alert title={commandBlockedReason} type="info" showIcon />}
          <CameraConnection camera={camera} disabled={settingsBlocked} searching={busy && pending?.action === "discover"}
            onSearch={() => void apply("discover")} cancelAvailable={operator && !!pending?.operation && !pending.cancellation && pending.until > now}
            cancelling={cancelWorking} onCancel={() => void cancelDiscovery()}
            onBind={(candidate_mac, discovery_generation) => void apply("bind", undefined, undefined, undefined, { candidate_mac, discovery_generation })} />
          <CameraCredentials key={device} device={device} org={org} camera={camera} operator={operator} disabled={settingsBlocked} onBusy={onCredentialBusy} />
        </>}
        <div hidden={panel !== "ircut"}><CameraIrcutDiagnostic device={device} org={org} snapshot={snapshot} disabled={!!commandBlockedReason} onBusy={setDiagnosticBusy} /></div>
      </div>
    </Drawer>
  </CameraToolsContext.Provider>;
}
