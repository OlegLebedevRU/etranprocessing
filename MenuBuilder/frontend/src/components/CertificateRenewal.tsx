import { useEffect, useRef, useState } from "react";
import { Alert, Button, Modal, Space, Typography, message } from "antd";
import client from "../api/client";

type Snapshot = { allowed: boolean; reason?: string; status: string; expires_at?: string; observed_at?: string };
const labels: Record<string, string> = { none: "Продление ещё не заказывалось", pending: "Ожидает терминал",
  issued_waiting_identity: "Сертификат выпущен; ожидается подтверждение использования", confirmed: "Новый сертификат используется терминалом",
  failed: "Команда завершилась ошибкой; подробности в истории команд", cancelled: "Команда отменена",
  expired: "Срок ожидания истёк" };

export default function CertificateRenewal({ deviceId }: { deviceId: number }) {
  const [snapshot, setSnapshot] = useState<Snapshot>();
  const [open, setOpen] = useState(false);
  const [busy, setBusy] = useState(false);
  const [pinId, setPinId] = useState<number>();
  const [taskId, setTaskId] = useState<string>();
  const [failed, setFailed] = useState(false);
  const [retries, setRetries] = useState(0);
  const [cooldown, setCooldown] = useState(0);
  const sending = useRef(false);
  const generation = useRef(0);
  const endpoint = `/devices/${deviceId}/certificate-renewal`;
  const refresh = async () => {
    const current = generation.current;
    try { const { data } = await client.get<Snapshot>(endpoint); if (current === generation.current) setSnapshot(data); }
    catch { if (current === generation.current) setSnapshot({ allowed: false, reason: "Проверка продления недоступна", status: "none" }); }
  };
  useEffect(() => { generation.current += 1; sending.current = false; setBusy(false); setSnapshot(undefined); setOpen(false); void refresh();
    return () => { generation.current += 1; }; }, [deviceId]);
  useEffect(() => {
    if (!cooldown) return;
    const timer = window.setTimeout(() => setCooldown(0), Math.max(0, cooldown - Date.now()));
    return () => window.clearTimeout(timer);
  }, [cooldown]);
  const send = async () => {
    if (sending.current || taskId) return;
    const current = generation.current;
    sending.current = true; setBusy(true);
    const next = failed ? retries + 1 : 0;
    setRetries(next);
    try {
      const { data } = await client.post<{ task_id: string; pin_id: number; expires_at: string }>(endpoint, { pin_id: pinId });
      if (current !== generation.current) return;
      setPinId(data.pin_id); setTaskId(data.task_id); setFailed(false);
      message.success("Продление поставлено в очередь"); void refresh();
    } catch (error: any) {
      if (current !== generation.current) return;
      const detail = error.response?.data?.detail;
      if (detail?.pin_id) setPinId(detail.pin_id);
      setFailed(true);
      if (next >= 3) {
        setOpen(false); setCooldown(Date.now() + 180000);
        message.error("Постановка не подтверждена. Повторите через 3 минуты.");
      } else message.error(error.message || "Постановка не подтверждена. Можно повторить вручную.");
    } finally { if (current === generation.current) { sending.current = false; setBusy(false); } }
  };
  return <Space direction="vertical">
    <Typography.Text>{snapshot ? labels[snapshot.status] || snapshot.status : "Проверка доступности…"}</Typography.Text>
    <Space wrap>
      <Button type="primary" disabled={!snapshot?.allowed || Date.now() < cooldown} onClick={() => {
        setPinId(undefined); setTaskId(undefined); setFailed(false); setRetries(0); setOpen(true);
      }}>Заказать удалённое продление</Button>
      <Button onClick={() => void refresh()}>Проверить результат</Button>
    </Space>
    {snapshot?.reason && <Typography.Text type="secondary">{snapshot.reason}</Typography.Text>}
    <Modal title="Удалённое продление сертификата" open={open} onCancel={() => { if (!busy) setOpen(false); }}
      closable={!busy} maskClosable={!busy} footer={<Space>
        <Button disabled={busy} onClick={() => setOpen(false)}>Закрыть</Button>
        {!taskId && <Button type="primary" loading={busy} onClick={() => void send()}>
          {failed ? `Повторить (${retries + 1}/3)` : "Заказать продление"}</Button>}
      </Space>}>
      <Alert type={taskId ? "success" : "info"} showIcon message={taskId ? "Команда поставлена в очередь" : "Сервер создаст PIN и поставит команду в очередь"}
        description="Терминал может быть оффлайн. Он выполнит продление после подключения, пока срок ожидания не истёк. Результат доступен по кнопке проверки и в истории команд." />
      {taskId && <Typography.Paragraph copyable>{taskId}</Typography.Paragraph>}
      {snapshot?.expires_at && <Typography.Text>Срок ожидания: {new Date(snapshot.expires_at).toLocaleString()}</Typography.Text>}
    </Modal>
  </Space>;
}
