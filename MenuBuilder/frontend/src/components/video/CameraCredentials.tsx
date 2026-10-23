import { useEffect, useRef, useState } from "react";
import { Alert, Button, Input, Space, Typography } from "antd";
import { changeCameraCredentials, getCamera, getCameraCommand, getCameraCredentialMetadata, type CameraCredentialChange, type CameraCredentialMetadata, type CameraState } from "../../api/cameras";

type Pending = { id: string; until: number; action: CameraCredentialChange["action"] };

export function CameraCredentials({ device, org, camera, operator, disabled, onBusy }: {
  device: number; org: number; camera?: CameraState; operator: boolean; disabled: boolean; onBusy: (value: boolean) => void;
}) {
  const key = `camera-credentials:${org}:${device}`;
  const [pending, setPending] = useState<Pending | null>(() => { try { const item = JSON.parse(sessionStorage.getItem(key) || "null");
    return item && /^[0-9a-f-]{36}$/.test(item.id) && ["replace", "redeliver", "clear"].includes(item.action) && Number.isFinite(item.until) && item.until <= Date.now() + 90000 ? item : null;
  } catch { return null; } });
  const [metadata, setMetadata] = useState<CameraCredentialMetadata | null>(null);
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [notice, setNotice] = useState("");
  const [working, setWorking] = useState(false);
  const [now, setNow] = useState(Date.now());
  const life = useRef<AbortController | null>(null);
  const sending = useRef(false);
  const busy = working || !!pending && pending.until > now;
  useEffect(() => { onBusy(busy); return () => onBusy(false); }, [busy, onBusy]);
  useEffect(() => {
    const controller = new AbortController(); life.current = controller;
    if (operator) void getCameraCredentialMetadata(device, controller.signal).then(setMetadata).catch(() => {
      if (!controller.signal.aborted) setNotice("Сохранение учётных данных пока недоступно");
    });
    const timer = setInterval(() => setNow(Date.now()), 1000);
    return () => { controller.abort(); clearInterval(timer); };
  }, [device, operator]);
  const ready = metadata?.vault_ready && metadata?.recipient_ready && camera?.revision &&
    camera.camera_contract_revision === "siplite-camera/1.0.0" && camera.capabilities?.methods?.includes(166);
  function save(value: Pending | null) {
    // Persist only safe operation metadata. Login/password never enter browser storage.
    if (value) sessionStorage.setItem(key, JSON.stringify(value)); else { try { sessionStorage.removeItem(key); } catch { /* Cleanup never sends a command. */ } }
    setPending(value);
  }
  async function check(id: string, action: CameraCredentialChange["action"], signal: AbortSignal) {
    const task = await getCameraCommand(device, id, signal);
    const result = task.results?.[0];
    if (!result) return false;
    const body = typeof result.result === "string" ? JSON.parse(result.result) : result.result;
    const current = await getCamera(device, signal);
    if (current.snapshot?.operation_id !== id || current.snapshot?.snapshot_id !== body.snapshot_id) return false;
    // The server performs the same independent RES/gauge reconciliation before promotion.
    await getCameraCommand(device, id, signal);
    const stored = await getCameraCredentialMetadata(device, signal); setMetadata(stored);
    if (result.status_code !== 200) { save(null); setNotice("Не удалось применить учётные данные. Ранее сохранённый серверный профиль не изменён."); return true; }
    if (stored.latest_operation_id !== id || stored.latest_state !== (action === "clear" ? "cleared" : "active")) return false;
    save(null); setNotice("Применение и сохранение учётных данных подтверждены"); return true;
  }
  async function submit(action: CameraCredentialChange["action"]) {
    const signal = life.current?.signal;
    if (!signal || signal.aborted || sending.current || disabled || busy || !ready || !camera?.revision) return;
    sending.current = true; setWorking(true); setNotice("");
    const operation_id = crypto.randomUUID();
    const change: CameraCredentialChange = { action, operation_id, expected_revision: camera.revision, expires_at_ms: Date.now() + 60000, allow_disruptive: true };
    if (action === "replace") { change.username = username; change.password = password; }
    if (action === "redeliver") change.source_operation_id = metadata?.last_operation_id || metadata?.active_operation_id || undefined;
    let attempted = false;
    try {
      save({ id: operation_id, until: Date.now() + 90000, action });
      attempted = true;
      const accepted = await changeCameraCredentials(device, change);
      if (accepted.id !== operation_id) throw new Error("Результат команды пока не подтверждён");
      setUsername(""); setPassword("");
      const deadline = Date.now() + 45000;
      while (Date.now() < deadline && !signal.aborted) {
        if (await check(operation_id, action, signal)) return;
        await new Promise(resolve => setTimeout(resolve, 1000));
      }
      if (!signal.aborted) setNotice("Результат пока не подтверждён. Проверьте состояние; автоматического повтора нет.");
    } catch (error: any) {
      if (signal.aborted) return;
      const status = error?.response?.status;
      if (!attempted || status && status >= 400 && status < 500) save(null);
      setNotice(status && status < 500 ? "Команда отклонена. Обновите состояние терминала и камеры." : "Ответ не подтверждён. Автоматического повтора нет.");
    } finally {
      setUsername(""); setPassword(""); sending.current = false;
      if (!signal.aborted) {
        setWorking(false);
        void getCameraCredentialMetadata(device, signal).then(setMetadata).catch(() => {});
      }
    }
  }
  if (!operator) return <Typography.Text type="secondary">Учётные данные изменяет оператор.</Typography.Text>;
  return <Space orientation="vertical" style={{ width: "100%" }}>
    {notice && <Alert type="info" title={notice} showIcon />}
    {!metadata?.recipient_ready && <Typography.Text type="secondary">Терминал ещё не подтвердил защищённое подключение.</Typography.Text>}
    {!metadata?.vault_ready && <Typography.Text type="secondary">Сохранение учётных данных пока недоступно.</Typography.Text>}
    {camera?.camera_contract_revision !== "siplite-camera/1.0.0" && <Typography.Text type="secondary">Терминал пока не подтвердил возможность смены учётных данных.</Typography.Text>}
    <Typography.Text>Сохранено на сервере: {metadata?.has_active ? "да" : "нет"}. Пароль не возвращается при чтении.</Typography.Text>
    <Input aria-label="Логин камеры" autoComplete="off" value={username} disabled={disabled || busy || !ready} onChange={event => setUsername(event.target.value)} placeholder="Логин камеры" />
    <Input.Password aria-label="Пароль камеры" autoComplete="new-password" value={password} disabled={disabled || busy || !ready} onChange={event => setPassword(event.target.value)} placeholder="Пароль камеры" />
    <Space wrap>
      <Button type="primary" loading={working} disabled={disabled || busy || !ready || !username || !password} onClick={() => void submit("replace")}>Сохранить и применить</Button>
      <Button disabled={disabled || busy || !ready || !(metadata?.last_operation_id || metadata?.active_operation_id)} onClick={() => void submit("redeliver")}>Отправить сохранённые</Button>
      <Button disabled={disabled || busy || !ready || camera?.auth_source !== "custom"} onClick={() => void submit("clear")}>Вернуть штатные учётные данные</Button>
      <Button disabled={working} onClick={() => void getCameraCredentialMetadata(device, life.current?.signal).then(setMetadata).catch(() => setNotice("Состояние сохранения недоступно"))}>Обновить доступность</Button>
      {pending && <Button disabled={working} onClick={() => { const signal = life.current?.signal; if (signal) void check(pending.id, pending.action, signal).catch(() => setNotice("Результат пока не подтверждён")); }}>Проверить результат</Button>}
    </Space>
  </Space>;
}
