import { useCallback, useEffect, useRef, useState } from "react";
import { Alert, Button, Card, Collapse, Descriptions, Modal, Space, Typography } from "antd";
import { commandCamera, getCamera, getCameraCommand, type CameraCommand, type CameraObservation, type CameraProbe } from "../../api/cameras";
import { configFresh, observationAge, powerFresh } from "../../utils/cameraState";

type Pending = { until: number; task?: string };
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
  const [now, setNow] = useState(Date.now());
  const lifecycle = useRef<AbortController | null>(null);
  const mutation = useRef(false);
  const confirmation = useRef<{ destroy: () => void } | null>(null);
  const saved = useCallback((item: Pending | null) => {
    // If durable blocking cannot be saved, do not send a disruptive command.
    if (item) sessionStorage.setItem(key, JSON.stringify(item));
    else { try { sessionStorage.removeItem(key); } catch { /* No command is sent during cleanup. */ } }
    setPending(item);
  }, [key]);
  const busy = working || !!pending && pending.until > now;
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

  async function apply(action: CameraCommand["action"], value?: string) {
    if (mutation.current || busy || !operator || !lifecycle.current) return;
    if (action !== "refresh" && settingsBlocked) return;
    const signal = lifecycle.current.signal;
    if (signal.aborted) return;
    mutation.current = true; setWorking(true); setNotice("");
    let accepted = false;
    let submitting = true;
    try {
      const command: CameraCommand = { operation_id: crypto.randomUUID(), action, value };
      if (action !== "refresh") command.allow_disruptive = true;
      if (action === "quality" || action === "day_night") {
        if (!configValid || !camera?.revision) throw new Error("Обновите состояние камеры");
        command.expected_revision = camera.revision;
      }
      if (action === "power") {
        if (!powerValid) throw new Error("Обновите состояние питания");
        command.expected_power_on = camera!.power!.enabled!;
      }
      const lock = { until: Date.now() + 90000 };
      saved(lock);
      accepted = true; // POST may commit even when its response is lost.
      const task = await commandCamera(device, command);
      submitting = false;
      saved({ ...lock, task: task.id });
      if (signal.aborted) return;
      const deadline = Date.now() + 45000;
      let result;
      while (Date.now() < deadline) {
        const current = await getCameraCommand(device, task.id, signal);
        if (current.results?.length) { result = current.results[0]; break; }
        if ([4, 5, 6, 7].includes(current.status)) throw new Error("Команда завершилась без подтверждённого результата");
        await pause(signal);
      }
      if (!result) throw new Error("Результат команды пока не подтверждён");
      const body = typeof result.result === "string" ? JSON.parse(result.result) : result.result;
      let correlated = false;
      const eventDeadline = Date.now() + 10000;
      while (Date.now() < eventDeadline) {
        const state = await getCamera(device, signal);
        if (signal.aborted) return;
        setData(state);
        if (state.snapshot?.operation_id === task.id && state.snapshot.snapshot_id === body.snapshot_id) {
          correlated = true; break;
        }
        await pause(signal);
      }
      if (!correlated) throw new Error("Ответ получен, но соответствующее событие камеры ещё не подтверждено");
      saved(null); accepted = false;
      const failure = result.status_code !== 200;
      setNotice(failure ? `Не выполнено: ${body.error || body.status || result.status_code}${
        body.applied_fields?.length ? "; часть изменений применена — проверьте состояние" : ""}` :
        action === "power" ? "Состояние выхода питания подтверждено. После включения дождитесь загрузки камеры и обновите состояние." :
          action === "refresh" ? "Состояние обновлено" : "Настройки применены и подтверждены камерой");
    } catch (error: any) {
      if (signal.aborted) return;
      // Definite admission denial happens before task creation; a lost response stays blocked.
      const status = error?.response?.status;
      if (!accepted || submitting && status && status >= 400 && status < 500) saved(null);
      setNotice(error?.response?.data?.detail?.message || error?.message || "Результат не подтверждён; автоматического повтора нет");
    } finally {
      mutation.current = false;
      if (!signal.aborted) setWorking(false);
    }
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
      { key: "quality", label: "Видео", children: configValid ? `${camera?.config?.video0?.size} · ${camera?.config?.video0?.fps} FPS · ${camera?.config?.video0?.bitrate} кбит/с` : "Неизвестно" },
      { key: "mode", label: "День / ночь", children: configValid && camera?.config?.nightMode?.lightMonitor ? "Auto" :
        configValid && probeAge(camera?.telemetry) === "Актуально" && [0, 1].includes(camera?.telemetry?.metrics?.night_enabled ?? -1) ?
          camera?.telemetry?.metrics?.night_enabled === 1 ? "Night" : "Day" : "Неизвестно" },
    ]} />
    <Space wrap style={{ marginBlock: 12 }}>
      <Button disabled={!operator || busy} onClick={() => void apply("refresh")}>Обновить состояние</Button>
      <Typography.Text type="secondary">Питание — состояние управляющего выхода; готовность камеры проверяется отдельно.</Typography.Text>
    </Space>
    <Space direction="vertical" style={{ width: "100%" }}>
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
