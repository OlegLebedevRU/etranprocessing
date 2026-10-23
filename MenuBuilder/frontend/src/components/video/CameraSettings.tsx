import { useCallback, useEffect, useRef, useState } from "react";
import { Alert, Button, Card, Collapse, Descriptions, Modal, Space, Typography } from "antd";
import { commandCamera, getCamera, getCameraCommand, type CameraCommand, type CameraObservation, type CameraProbe, type CameraPatch } from "../../api/cameras";
import { CameraFields } from "./CameraFields";
import { CameraConnection } from "./CameraConnection";
import { CameraCredentials } from "./CameraCredentials";
import { cameraObservedFormat, cameraVideoSettings, configFresh, observationAge, powerFresh } from "../../utils/cameraState";

type Pending = { until: number; task?: string; action?: CameraCommand["action"]; operation?: string; cancellation?: string };
const pause = (signal: AbortSignal) => new Promise<void>((resolve, reject) => {
  const abort = () => { clearTimeout(timer); reject(new Error("aborted")); };
  const timer = setTimeout(() => { signal.removeEventListener("abort", abort); resolve(); }, 1000);
  signal.addEventListener("abort", abort, { once: true });
  if (signal.aborted) abort();
});

export function CameraSettings({ device, org, operator, viewing, onBusy }: {
  device: number; org: number; operator: boolean; viewing: boolean; onBusy: (busy: boolean) => void;
}) {
  const key = `camera-command:${org}:${device}`;
  const [pending, setPending] = useState<Pending | null>(() => {
    try { const item = JSON.parse(sessionStorage.getItem(key) || "null");
      return item?.until > Date.now() ? item : null; } catch { return null; }
  });
  const [data, setData] = useState<CameraObservation | null>(null);
  const [notice, setNotice] = useState("");
  const [working, setWorking] = useState(false);
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
  const busy = working || credentialBusy || credentialLockUntil > now || !!pending && pending.until > now;
  useEffect(() => { onBusy(busy); return () => onBusy(false); }, [busy, onBusy]);
  useEffect(() => {
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
  }, [device]);
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
      setNotice(error?.response?.data?.detail?.message || error?.message || "Результат не подтверждён; автоматического повтора нет");
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
  return <Card title="Состояние и настройки камеры" size="small">
    {notice && <Alert title={notice} type="info" showIcon style={{ marginBottom: 12 }} />}
    {busy && <Alert title="Команда выполняется или ожидает подтверждения. Повтор и запуск просмотра временно заблокированы." type="warning" />}
    {(viewing || data?.lease_active) && <Alert title="Для изменения настроек остановите просмотр. Чужая сессия не завершается." type="warning" />}
    <Descriptions size="small" column={{ xs: 1, sm: 2, md: 3 }} items={[
      { key: "power", label: "Питание", children: powerValid ? camera?.power?.enabled ? "Включено" : "Выключено" : "Неизвестно / устарело" },
      { key: "camera", label: "Камера", children: configValid && camera?.status === "ready" ? "Доступна" : "Недоступна / нет актуальных данных" },
      { key: "age", label: "Настройки", children: observationAge(snapshot?.observed_at, !!camera?.config && !camera?.stale, now, snapshot?.freshness_max_age_sec ?? 330) },
      { key: "quality", label: "Режим видео", children: configValid ? cameraVideoSettings(camera) : "Неизвестно" },
      { key: "actual", label: "Проба потока", children: cameraObservedFormat(snapshot, camera, now) },
      { key: "mode", label: "День / ночь", children: configValid && camera?.config?.nightMode?.lightMonitor ? "Auto" :
        configValid && probeAge(camera?.telemetry) === "Актуально" && [0, 1].includes(camera?.telemetry?.metrics?.night_enabled ?? -1) ?
          camera?.telemetry?.metrics?.night_enabled === 1 ? "Night" : "Day" : "Неизвестно" },
    ]} />
    <Space wrap style={{ marginBlock: 12 }}>
      <Button disabled={!operator || busy} onClick={() => void apply("refresh")}>Обновить состояние</Button>
      <Typography.Text type="secondary">Питание — состояние управляющего выхода; готовность камеры проверяется отдельно.</Typography.Text>
    </Space>
    <Space direction="vertical" style={{ width: "100%" }}>
      <Collapse items={[{ key: "connection", label: "Подключение камеры", children:
        <CameraConnection camera={camera} disabled={settingsBlocked} searching={busy && pending?.action === "discover"}
          onSearch={() => void apply("discover")}
          cancelAvailable={operator && !!pending?.operation && !pending.cancellation && pending.until > now}
          cancelling={cancelWorking} onCancel={() => void cancelDiscovery()}
          onBind={(candidate_mac, discovery_generation) => void apply("bind", undefined, undefined, undefined, { candidate_mac, discovery_generation })} /> }]} />
      <Collapse items={[{ key: "credentials", label: "Логин и пароль", children:
        <CameraCredentials key={device} device={device} org={org} camera={camera} operator={operator} disabled={settingsBlocked}
          onBusy={onCredentialBusy} /> }]} />
      <Collapse items={[{ key: "native-settings", label: "Видео и изображение", children:
        <CameraFields key={device} camera={camera} disabled={settingsBlocked || !configValid}
          onApply={(patch, revision) => apply("patch", undefined, patch, revision)} /> }]} />
      <Space wrap><Typography.Text>Качество</Typography.Text>{(["medium", "hd"] as const).map(id =>
        <Button key={id} disabled={settingsBlocked || !configValid || !camera?.capabilities?.quality?.presets?.some(p => p.id === id && p.available)}
          onClick={() => void apply("quality", id)}>{id === "hd" ? "HD" : "Medium"}</Button>)}</Space>
      <Space wrap><Typography.Text>День / ночь</Typography.Text>{(["auto", "day", "night"] as const).map(mode =>
        <Button key={mode} disabled={settingsBlocked || !configValid || !camera?.capabilities?.day_night?.write || !camera.capabilities.day_night.modes?.includes(mode)}
          onClick={() => void apply("day_night", mode)}>{mode === "auto" ? "Auto" : mode === "day" ? "Day" : "Night"}</Button>)}</Space>
      <Space wrap><Typography.Text>Питание</Typography.Text>{(["on", "off", "restart"] as const).map(action =>
        <Button key={action} danger={action !== "on"} disabled={settingsBlocked || !powerValid || !camera?.capabilities?.power?.available ||
          !camera.capabilities.power.actions?.includes(action) || (action === "restart" && camera.power?.enabled !== true)}
          onClick={() => power(action)}>{action === "on" ? "Включить" : action === "off" ? "Выключить" : "Перезапустить"}</Button>)}</Space>
    </Space>
    <Collapse style={{ marginTop: 12 }} items={[{ key: "illumination", label: "Подсветка и плата", children:
      <Space orientation="vertical">
        <Typography.Text>White LED · IR LED · общий enable · PWM</Typography.Text>
        <Alert type="info" showIcon title="Подключение и полярность подсветки не подтверждены. Физические пробы и управление GPIO недоступны в этой версии." />
        <Typography.Text type="secondary">Предполагаемые роли платы не являются проверенными выходами. Управление днём / ночью доступно отдельно по возможностям камеры.</Typography.Text>
      </Space> }]} />
    <Collapse style={{ marginTop: 12 }} items={[{ key: "details", label: "Подробности", children:
      <Descriptions size="small" column={1} items={[
        { key: "identity", label: "Версии", children: probeAge(camera?.identity) === "Актуально" ? `${camera?.identity?.openipc_version} / ${camera?.identity?.majestic_version}` : probeAge(camera?.identity) },
        { key: "health", label: "Диагностика", children: probeAge(camera?.telemetry?.health) },
        { key: "temperature", label: "Температура", children: probeAge(camera?.telemetry?.health) === "Актуально" ? `${camera?.telemetry?.health?.metrics?.node_hwmon_temp_celsius ?? "Неизвестно"} °C` : "Неизвестно" },
        { key: "uptime", label: "Uptime камеры", children: probeAge(camera?.telemetry?.health) === "Актуально" ? `${camera?.telemetry?.health?.uptime_sec ?? "Неизвестно"} с` : "Неизвестно" },
        { key: "memory", label: "Доступная память", children: probeAge(camera?.telemetry?.health) === "Актуально" ? `${camera?.telemetry?.health?.metrics?.node_memory_MemAvailable_bytes ?? "Неизвестно"} байт` : "Неизвестно" },
        { key: "clock", label: "Часы камеры", children: probeAge(camera?.telemetry?.health) === "Актуально" && camera?.telemetry?.health?.clock?.status === "skewed" ? "Время камеры расходится со временем терминала" : "См. диагностику" },
      ]} /> }]} />
  </Card>;
}
