import { useEffect, useRef, useState } from "react";
import { Alert, Button, Card, Modal, Space, Typography, message } from "antd";
import client from "../api/client";
import { confirmPayment } from "../api/billing";
import { requestRenewalPermission, type PaymentRequiredResponse } from "../api/certificate-pin";
import { useSession } from "../session/SessionContext";
import { formatMoneyMinor } from "../utils/billing";

import { isRenewalInProgress, useCertificateRenewalState, type CertificateRenewalState } from "../hooks/useCertificateRenewalState";

const labels: Record<string, string> = { none: "Продление ещё не заказывалось", pending: "Ожидает терминал",
  issued_waiting_identity: "Сертификат выпущен; ожидается подтверждение использования", confirmed: "Новый сертификат используется терминалом",
  installation_failed: "Сертификат выпущен, но установка завершилась ошибкой",
  installation_unconfirmed: "Сертификат выпущен, установка не подтверждена — требуется вмешательство",
  failed: "Команда завершилась ошибкой; подробности в истории команд", cancelled: "Команда отменена",
  expired: "Срок ожидания истёк" };

export default function CertificateRenewal({ deviceId, confirmation = "modal", onInteractionChange, state, onSelect, blockedReason }: {
  deviceId: number; confirmation?: "modal" | "inline"; onInteractionChange?: (locked: boolean) => void;
  state?: CertificateRenewalState; onSelect?: () => void; blockedReason?: string;
}) {
  const { user } = useSession();
  const localState = useCertificateRenewalState(deviceId, !state);
  const { snapshot, loading, loadFailed, refresh, markQueued } = state ?? localState;
  const inProgress = isRenewalInProgress(snapshot);
  const [open, setOpen] = useState(false);
  const [resumeInstallation, setResumeInstallation] = useState(false);
  const [busy, setBusy] = useState(false);
  const [pinId, setPinId] = useState<number>();
  const [taskId, setTaskId] = useState<string>();
  const [failed, setFailed] = useState(false);
  const [retries, setRetries] = useState(0);
  const [cooldown, setCooldown] = useState(0);
  const [payment, setPayment] = useState<PaymentRequiredResponse>();
  const [orderId, setOrderId] = useState<string>();
  const [paymentConfirmed, setPaymentConfirmed] = useState(false);
  const sending = useRef(false);
  const generation = useRef(0);
  const endpoint = `/devices/${deviceId}/certificate-renewal`;
  useEffect(() => { onInteractionChange?.(busy || open); }, [busy, open, onInteractionChange]);
  useEffect(() => () => onInteractionChange?.(false), [onInteractionChange]);
  useEffect(() => { generation.current += 1; sending.current = false; setBusy(false); setOpen(false);
    setPinId(undefined); setTaskId(undefined); setFailed(false); setRetries(0); setResumeInstallation(false);
    setPayment(undefined); setOrderId(undefined); setPaymentConfirmed(false); setCooldown(0);
    return () => { generation.current += 1; }; }, [deviceId, user?.org_id]);
  useEffect(() => {
    if (!cooldown) return;
    const timer = window.setTimeout(() => setCooldown(0), Math.max(0, cooldown - Date.now()));
    return () => window.clearTimeout(timer);
  }, [cooldown]);
  const send = async (checkPayment = false, simulate = false) => {
    if (sending.current || taskId || blockedReason || loading || loadFailed ||
      (resumeInstallation ? !snapshot?.recoverable : !snapshot?.allowed || inProgress)) return;
    const current = generation.current;
    sending.current = true; setBusy(true);
    const next = failed ? retries + 1 : 0;
    setRetries(next);
    try {
      if (checkPayment && orderId) {
        await confirmPayment(orderId, simulate);
        if (current !== generation.current) return;
        setPaymentConfirmed(true);
      }
      const { data } = await client.post<{ task_id: string; pin_id: number; expires_at: string }>(endpoint,
        { pin_id: pinId, order_id: orderId, resume_installation: resumeInstallation });
      if (current !== generation.current) return;
      setPinId(data.pin_id); setTaskId(data.task_id); setFailed(false); markQueued(data.expires_at);
      message.success(resumeInstallation ? "Повторная установка поставлена в очередь" : "Продление поставлено в очередь"); void refresh();
    } catch (error: any) {
      if (current !== generation.current) return;
      const detail = error.response?.data?.detail;
      if (error.response?.status === 402 && detail?.code === "certificate_payment_required") {
        try {
          const permission = await requestRenewalPermission(detail.terminal_id);
          if (current !== generation.current) return;
          if (permission.status === "payment_required") {
            setPayment(permission); setOrderId(permission.order_id); setPaymentConfirmed(false);
          } else message.info("Условия изменились. Повторите заказ продления.");
        } catch (paymentError: any) {
          if (current === generation.current) message.error(paymentError.message || "Оплата недоступна");
        }
        return;
      }
      if (error.response?.status === 409 || error.response?.status === 410) {
        message.error(typeof detail === "string" ? detail : "Требуется вмешательство оператора"); void refresh(); return;
      }
      if (detail?.pin_id) setPinId(detail.pin_id);
      if (detail?.order_id) setOrderId(detail.order_id);
      if (checkPayment && !detail?.pin_id) {
        message.error(error.message || "Оплата ещё не подтверждена"); return;
      }
      setFailed(true);
      if (next >= 3) {
        setOpen(false); setCooldown(Date.now() + 180000);
        message.error("Постановка не подтверждена. Повторите через 3 минуты.");
      } else message.error(error.message || "Постановка не подтверждена. Можно повторить вручную.");
    } finally { if (current === generation.current) { sending.current = false; setBusy(false); } }
  };
  const confirmationTitle = resumeInstallation ? "Повторная установка сертификата" : "Удалённое продление сертификата";
  const confirmationFooter = <Space>
        <Button disabled={busy} onClick={() => setOpen(false)}>Закрыть</Button>
        {!taskId && payment && !paymentConfirmed && <>
          {payment.payment_url?.startsWith("https://") && <Button href={payment.payment_url}
            target="_blank" rel="noopener noreferrer" disabled={busy}>Перейти к оплате</Button>}
          <Button type="primary" loading={busy} onClick={() => void send(true)}>Проверить оплату и поставить в очередь</Button>
          {user?.is_superuser && <Button disabled={busy} onClick={() => void send(true, true)}>Эмулировать оплату</Button>}
        </>}
        {!taskId && (!payment || paymentConfirmed) && <Button type="primary" loading={busy} onClick={() => void send()}>
          {failed ? `Повторить (${retries + 1}/3)` : resumeInstallation ? "Повторить установку" : "Заказать продление"}</Button>}
      </Space>;
  const confirmationBody = <>
      {payment && !paymentConfirmed && <Alert type="warning" showIcon title="Требуется оплата сертификата"
        description={formatMoneyMinor(payment.amount_minor, payment.currency)} style={{ marginBottom: 12 }} />}
      <Alert type={taskId ? "success" : "info"} showIcon title={taskId ? "Команда поставлена в очередь" : resumeInstallation ? "Установить ранее выпущенный сертификат" : "Сервер создаст PIN и поставит команду в очередь"}
        description={resumeInstallation ? "Команда использует прежнюю заявку, сохранённый сертификат и ключ. Новый сертификат не выпускается. Если данные восстановления отсутствуют, требуется вмешательство оператора." : "Терминал может быть оффлайн. Он выполнит продление после подключения, пока срок ожидания не истёк. Результат доступен по кнопке проверки и в истории команд."} />
      {taskId && <Typography.Paragraph copyable>{taskId}</Typography.Paragraph>}
      {snapshot?.expires_at && <Typography.Text>Срок ожидания: {new Date(snapshot.expires_at).toLocaleString()}</Typography.Text>}
</>;
  if (onSelect) return <Space orientation="vertical" style={{ width: "100%" }}>
    <Typography.Text>{snapshot ? labels[snapshot.status] || snapshot.status : "Проверка продления…"}</Typography.Text>
    {snapshot?.reason && <Typography.Text type="secondary">{snapshot.reason}</Typography.Text>}
    {blockedReason && <Typography.Text type="secondary">{blockedReason}</Typography.Text>}
    <Space wrap>
      <Button type="primary" aria-label="Выбрать удалённое продление" loading={!snapshot && loading} disabled={loading || loadFailed || inProgress || !!blockedReason}
        onClick={onSelect}>Выбрать удалённое продление</Button>
      <Button aria-label="Проверить" loading={loading} onClick={() => void refresh()}>Проверить</Button>
      {snapshot?.installation_pending && <Button disabled={!!blockedReason} onClick={onSelect}>Открыть восстановление установки</Button>}
    </Space>
    {inProgress && <Typography.Text type="secondary">Повторный запуск недоступен, пока текущая заявка не завершена.</Typography.Text>}
  </Space>;
  return <Space orientation="vertical">
    <Typography.Text type="secondary">Нужен агент с поддержкой RPC 7011; постановка команды не подтверждает её выполнение.</Typography.Text>
    <Typography.Text>{snapshot ? labels[snapshot.status] || snapshot.status : "Проверка доступности…"}</Typography.Text>
    <Space wrap>
      <Button type="primary" disabled={loading || !snapshot?.allowed || inProgress || !!blockedReason || Date.now() < cooldown} onClick={() => {
        setResumeInstallation(false); setPinId(undefined); setTaskId(undefined); setFailed(false); setRetries(0);
        setPayment(undefined); setOrderId(undefined); setPaymentConfirmed(false); setOpen(true);
      }}>Продлить сертификат на терминале</Button>
      {snapshot?.installation_pending && <Button type="primary" disabled={loading || loadFailed || !snapshot.recoverable || !!blockedReason || Date.now() < cooldown} onClick={() => {
        setResumeInstallation(true); setPinId(snapshot.recovery_pin_id); setTaskId(undefined); setFailed(false); setRetries(0);
        setPayment(undefined); setOrderId(undefined); setPaymentConfirmed(false); setOpen(true);
      }}>Повторить установку выпущенного сертификата</Button>}
      <Button aria-label="Проверить результат" loading={loading} onClick={() => void refresh()}>Проверить результат</Button>
    </Space>
    {snapshot?.installation_pending && <Alert type={snapshot.requires_intervention ? "error" : "warning"} showIcon
      title="Новый выпуск заблокирован до завершения установки"
      description={snapshot.recoverable
        ? `Можно повторить установку прежнего сертификата с сохранённым ключом. ${snapshot.exit_code != null ? `Код ошибки команды: ${snapshot.exit_code}.` : ""}`
        : "Повторная установка сейчас недоступна. Сохраните историю команды и обратитесь к оператору; не удаляйте сохранённые данные продления."} />}
    {snapshot?.reason && <Typography.Text type="secondary">{snapshot.reason}</Typography.Text>}
    {confirmation === "inline" ? open && <Card title={confirmationTitle}>
      <Space orientation="vertical" style={{ width: "100%" }}>{confirmationBody}{confirmationFooter}</Space>
    </Card> : <Modal title={confirmationTitle} open={open} onCancel={() => { if (!busy) setOpen(false); }}
      closable={!busy} mask={{ closable: !busy }} footer={confirmationFooter}>{confirmationBody}</Modal>}

  </Space>;
}
