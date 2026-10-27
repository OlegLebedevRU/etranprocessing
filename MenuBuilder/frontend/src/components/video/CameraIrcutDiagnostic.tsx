import { useEffect, useRef, useState } from "react";
import { Alert, Button, Descriptions, Modal, Progress, Space, Typography } from "antd";
import { commandCameraIrcut, getCameraCommand, type CameraSnapshot, type IrcutCommand, type IrcutReport } from "../../api/cameras";
import { cameraErrorMessage } from "../../utils/cameraErrors";

const reasons: Record<string, string> = {
  insufficient_light: "Недостаточно подходящего света — движение фильтра не подтверждено",
  weak_or_noisy_transition: "Отклик недостаточно уверенный — результат требует проверки",
  reversible_optical_transition: "Получен обратимый оптический отклик",
  non_reversible: "Повторное положение не подтвердилось",
  cancelled: "Проверка отменена", cleanup_unconfirmed: "Восстановление не подтверждено",
  restore_unconfirmed: "Настройки требуют восстановления", preparing: "Подготовка камеры",
  recovering: "Восстановление настроек", frame_unavailable_or_stale: "Кадр не получен или устарел",
};
const wait = (signal: AbortSignal, ms: number) => new Promise<void>((resolve, reject) => {
  if (signal.aborted) { reject(new Error("aborted")); return; }
  const abort = () => { clearTimeout(timer); reject(new Error("aborted")); };
  const timer = setTimeout(() => { signal.removeEventListener("abort", abort); resolve(); }, ms);
  signal.addEventListener("abort", abort, { once: true });
});

/** RPC169 only: no arbitrary pairs, pin assignment or search action. */
export function CameraIrcutDiagnostic({ device, org, snapshot, disabled, onBusy }: {
  device: number; org: number; snapshot: CameraSnapshot | null; disabled: boolean; onBusy: (busy: boolean) => void;
}) {
  const storageKey = `camera-ircut:${org}:${device}`;
  const [report, setReport] = useState<IrcutReport | undefined>(snapshot?.ircut);
  const [pending, setPending] = useState<{ session_id: string; camera_mac: string } | null>(() => {
    try { const item = JSON.parse(sessionStorage.getItem(storageKey) || "null");
      return typeof item?.session_id === "string" && typeof item.camera_mac === "string" ? item : null;
    } catch { return null; }
  });
  const [working, setWorking] = useState(false);
  const [cancelling, setCancelling] = useState(false);
  const [error, setError] = useState("");
  const inFlight = useRef(false);
  const lifecycle = useRef<AbortController | null>(null);
  const confirmation = useRef<{ destroy: () => void } | null>(null);
  useEffect(() => {
    const controller = new AbortController(); lifecycle.current = controller;
    return () => { controller.abort(); confirmation.current?.destroy(); };
  }, []);
  useEffect(() => { onBusy(working || !!pending); }, [working, pending, onBusy]);
  useEffect(() => {
    if (!working && !pending && snapshot?.ircut) setReport(snapshot.ircut);
  }, [snapshot?.ircut, working, pending]);
  const mac = pending?.camera_mac || report?.camera_mac || snapshot?.media_observations?.find(o => o.physical_camera_mac)?.physical_camera_mac;
  const supported = snapshot?.ircut?.ircut_revision === "siplite-ircut/1.0.0" && snapshot.ircut.methods?.includes(169);
  const persist = (item: typeof pending) => {
    if (item) sessionStorage.setItem(storageKey, JSON.stringify(item));
    else sessionStorage.removeItem(storageKey);
    setPending(item);
  };
  async function rpc(action: IrcutCommand["action"], extra: Partial<IrcutCommand> = {}): Promise<IrcutReport> {
    const signal = lifecycle.current!.signal;
    if (signal.aborted || !mac) throw new Error("Камера не определена");
    const task = await commandCameraIrcut(device, { operation_id: crypto.randomUUID(), action, camera_mac: mac, ...extra });
    for (let until = Date.now() + 45000; Date.now() < until;) {
      const status = await getCameraCommand(device, task.id, signal);
      const result = status.results?.[0];
      if (result) {
        const body = typeof result.result === "string" ? JSON.parse(result.result) : result.result;
        if (result.status_code >= 400 || !body.ircut) throw Object.assign(new Error(body.error || "Диагностика не подтверждена камерой"), { report: body.ircut });
        if (body.ircut.ircut_revision !== "siplite-ircut/1.0.0" || body.ircut.camera_mac !== mac ||
          action !== "inspect" && body.ircut.session_id !== extra.session_id) throw new Error("Ответ относится к другой камере или операции");
        return body.ircut;
      }
      if ([4, 5, 6, 7].includes(status.status)) throw new Error("Команда завершилась без результата");
      await wait(signal, 1500);
    }
    throw new Error("Ответ пока не получен. Новый тест не запускайте; проверьте статус этой операции.");
  }
  const accept = (next: IrcutReport) => {
    setReport(next);
    if (next.phase === 12 && next.configuration_restored && next.parked && !next.recovery_required) persist(null);
  };
  async function watch(session: string) {
    for (let until = Date.now() + 330000; Date.now() < until;) {
      const next = await rpc("status", { session_id: session });
      accept(next);
      if (next.phase === 12) return;
      await wait(lifecycle.current!.signal, 4000);
    }
    throw new Error("Проверка затянулась. Проверьте статус или отмените её.");
  }
  async function perform(action: "inspect" | "status" | "recover") {
    if (inFlight.current || !mac) return;
    inFlight.current = true; setWorking(true); setError("");
    try {
      if (action === "inspect") {
        const inspected = await rpc("inspect"); setReport(inspected);
        if (inspected.recovery_required) { persist({ session_id: inspected.session_id, camera_mac: inspected.camera_mac }); return; }
        if (!inspected.configured_pair || inspected.configured_pair.length !== 2) throw new Error("Два пина IR-cut не назначены. Поиск пинов здесь не выполняется.");
        confirmation.current = Modal.confirm({ title: "Проверить IR-cut?", okText: "Проверить", cancelText: "Отмена",
          content: `Будут проверены только назначенные пины ${inspected.configured_pair.join(" / ")}. Изображение временно изменится. Настройки будут восстановлены. Подсветка не используется.`,
          onOk: () => {
            setWorking(true); inFlight.current = true;
            const session = crypto.randomUUID();
            const execute = async () => { try {
              persist({ session_id: session, camera_mac: inspected.camera_mac });
              const next = await rpc("start", { session_id: session, boot_id: inspected.boot_id,
                expected_revision: inspected.revision, allow_disruptive: true });
              accept(next); await watch(session);
            } catch (e: any) {
              if (!lifecycle.current?.signal.aborted) {
                if (e.report) accept(e.report);
                if ([400, 401, 403, 404, 409, 422, 429].includes(e?.response?.status)) persist(null);
                setError(cameraErrorMessage(e));
              }
            }
            finally { inFlight.current = false; if (!lifecycle.current?.signal.aborted) setWorking(false); } };
            void execute();
          },
        });
      } else {
        const session = pending?.session_id || report?.session_id;
        if (!session) throw new Error("Нет операции для проверки");
        const next = await rpc(action, { session_id: session }); accept(next);
        if (action === "recover" || next.phase !== 12) await watch(session);
      }
    } catch (e: any) {
      if (!lifecycle.current?.signal.aborted) setError(cameraErrorMessage(e));
    } finally { inFlight.current = false; if (!lifecycle.current?.signal.aborted) setWorking(false); }
  }
  async function cancel() {
    if (!pending || cancelling) return;
    setCancelling(true);
    try { accept(await rpc("cancel", { session_id: pending.session_id })); }
    catch (e) { setError(e instanceof Error ? e.message : "Отмена не подтверждена"); }
    finally { setCancelling(false); }
  }
  return <Space orientation="vertical" style={{ width: "100%" }}>
    <Typography.Text>Проверка назначенных пинов. Поиск и переназначение не выполняются.</Typography.Text>
    {!supported && <Alert type="info" title="Терминал ещё не подтвердил поддержку диагностики. Обновите состояние." />}
    {error && <Alert type="warning" title={error} showIcon />}
    <Descriptions size="small" column={1} items={[
      { key: "pair", label: "Пины", children: report?.configured_pair?.join(" / ") || "Не подтверждены" },
      { key: "state", label: "Результат", children: report ? reasons[report.reason] || (report.state === "running" ? "Проверка выполняется" : "Пока не проверено") : "Пока не проверено" },
      { key: "restore", label: "Восстановление", children: report?.configuration_restored && report.parked ? "Подтверждено" : report ? "Не подтверждено" : "—" },
    ]} />
    {working && <Progress size="small" percent={Math.min(95, Math.round(((report?.stage || 0) * 3 + (report?.frame || 0)) / 9 * 100))} />}
    <Space wrap>
      <Button type="primary" disabled={disabled || !supported || !mac || !!pending || working} onClick={() => void perform("inspect")}>Проверить</Button>
      {pending && <Button disabled={working} onClick={() => void perform("status")}>Статус</Button>}
      {pending && <Button loading={cancelling} onClick={() => void cancel()}>Отмена</Button>}
      {report?.recovery_required && <Button disabled={working} onClick={() => void perform("recover")}>Восстановить</Button>}
    </Space>
    <Typography.Text type="secondary">Ответ команды не подтверждает физическое положение фильтра. Неуверенный отклик означает необходимость проверки, а не отсутствие IR-cut.</Typography.Text>
  </Space>;
}
