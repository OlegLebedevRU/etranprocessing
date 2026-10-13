import { useEffect, useRef, useState } from "react";
import { Alert, Button, Card, Col, Drawer, Modal, Row, Space, Spin, Steps, Tag, Typography, theme, message } from "antd";
import { ArrowLeftOutlined, CloudSyncOutlined, KeyOutlined } from "@ant-design/icons";
import CertificateRenewal from "./CertificateRenewal";
import { renewTerminalPin, type TerminalPin, type TerminalSettingsItem } from "../api/settings";

import { isRenewalInProgress, useCertificateRenewalState } from "../hooks/useCertificateRenewalState";
import { hasActiveManualPin, useTerminalPinState, type TerminalPinState } from "../hooks/useTerminalPinState";

const { Text, Paragraph, Title } = Typography;
type Flow = "remote" | "manual";

function PinSummary({ pin }: { pin: TerminalPin }) {
  const active = hasActiveManualPin(pin);
  return <Space orientation="vertical" style={{ width: "100%" }}>
    <Tag color={active ? "processing" : "default"}>{active ? "PIN выдан" : pin.status === "consumed" ? "PIN использован" : "PIN истёк"}</Tag>
    {active && pin.pin && <Paragraph copyable={{ text: pin.pin }} style={{ fontSize: 28, letterSpacing: 4, margin: 0 }}>{pin.pin}</Paragraph>}
    <Text type="secondary">Действует до: {new Date(pin.expires_at).toLocaleString("ru-RU")}</Text>
  </Space>;
}

function ManualCertificatePin({ terminal, orgId, state, onIssued, onInteractionChange, blockedReason }: {
  terminal: TerminalSettingsItem; orgId: number; state: TerminalPinState; onIssued: (result: TerminalPin) => void;
  onInteractionChange: (locked: boolean) => void; blockedReason?: string;
}) {
  const operationKey = `l4desk-pin-operation:${orgId}:${terminal.id}`;
  const { pin, loading, loadFailed, reload } = state;
  const [issuing, setIssuing] = useState(false);
  const [confirming, setConfirming] = useState(false);
  const [retry, setRetry] = useState(() => !!sessionStorage.getItem(operationKey));
  const operationRef = useRef<string | null>(null);
  const generation = useRef(0);
  const sending = useRef(false);
  const active = useRef(false);
  const dialog = useRef<ReturnType<typeof Modal.confirm> | null>(null);
  const onIssuedRef = useRef(onIssued); onIssuedRef.current = onIssued;
  useEffect(() => {
    generation.current++; active.current = true;
    return () => { active.current = false; generation.current++; dialog.current?.destroy(); onInteractionChange(false); };
  }, [terminal.id, orgId, onInteractionChange]);
  const issue = async () => {
    if (sending.current || !active.current || loading || loadFailed || hasActiveManualPin(pin) || blockedReason) return;
    const current = generation.current;
    const operation = operationRef.current ?? sessionStorage.getItem(operationKey) ?? crypto.randomUUID();
    operationRef.current = operation;
    sessionStorage.setItem(operationKey, operation);
    sending.current = true; setIssuing(true); onInteractionChange(true);
    try {
      const result = await renewTerminalPin(terminal.id, operation);
      sessionStorage.removeItem(operationKey);
      operationRef.current = null;
      if (current !== generation.current) return;
      setRetry(false);
      message.success("PIN для ручной установки получен"); onIssuedRef.current(result);
    } catch (error: any) {
      if (current !== generation.current) return;
      if (error.response?.status === 409) {
        sessionStorage.removeItem(operationKey); operationRef.current = null; setRetry(false); void reload();
      } else setRetry(true);
      const detail = error.response?.data?.detail;
      message.error(typeof detail === "string" ? detail : "Запрос не подтверждён. Повтор использует ту же заявку.");
    } finally {
      if (current === generation.current) { sending.current = false; setIssuing(false); }
    }
  };

  return <Space orientation="vertical" style={{ width: "100%" }}>
    <Alert type="warning" showIcon title="Установка выполняется инженером на терминале"
      description="Этот способ выдаёт PIN. Он не отправляет команду удалённого продления. Не запускайте ручную установку одновременно с удалённым продлением." />
    {retry && <Alert type="warning" showIcon title="Результат выдачи PIN пока не подтверждён"
      description="Повтор использует прежнюю заявку. Не запускайте удалённое продление, пока не уточните результат выдачи PIN." />}
    {blockedReason && <Alert type="info" showIcon title={blockedReason} />}
    <Card title="PIN для ручной установки">
      <Space orientation="vertical" style={{ width: "100%" }}>
        {loading ? <Spin aria-label="Загрузка PIN" /> : loadFailed ? <Alert type="error" showIcon title="Не удалось проверить текущий PIN"
          action={<Button onClick={() => void reload()}>Повторить проверку PIN</Button>} /> : pin ? <PinSummary pin={pin} /> : <Text type="secondary">PIN для этого устройства ещё не выдан.</Text>}
        <Button type="primary" icon={<KeyOutlined />} aria-label={retry ? "Повторить запрос PIN" : "Выдать PIN для ручной установки"}
          loading={issuing} disabled={loading || issuing || confirming || loadFailed || hasActiveManualPin(pin) || !!blockedReason}
          onClick={() => {
            if (dialog.current || sending.current) return;
            setConfirming(true); onInteractionChange(true);
            dialog.current = Modal.confirm({ title: retry ? "Повторить запрос PIN?" : "Выдать PIN для ручной установки?",
            content: `Устройство ${terminal.device_id}, SN ${terminal.sn}. PIN нужно использовать на этом терминале. Убедитесь, что удалённое продление сейчас не выполняется.`,
            okText: retry ? "Повторить запрос PIN" : "Выдать PIN", cancelText: "Отмена", onOk: issue,
            afterClose: () => { dialog.current = null; if (active.current) { setConfirming(false); onInteractionChange(false); } },
          }); }}>
          {retry ? "Повторить запрос PIN" : "Выдать PIN для ручной установки"}
        </Button>
      </Space>
    </Card>
  </Space>;
}

export default function TerminalCertificateDrawer({ terminal, orgId, canRenew, canIssuePin, onClose, onPinIssued }: {
  terminal: TerminalSettingsItem; orgId: number; canRenew: boolean; canIssuePin: boolean;
  onClose: () => void; onPinIssued: () => void;
}) {
  const [flow, setFlow] = useState<Flow>();
  const [locked, setLocked] = useState(false);
  const renewalState = useCertificateRenewalState(terminal.device_id, canRenew);
  const pinState = useTerminalPinState(terminal.id, orgId, canIssuePin);
  const remoteActive = isRenewalInProgress(renewalState.snapshot);
  const manualActive = hasActiveManualPin(pinState.pin);
  const manualUncertain = !!sessionStorage.getItem(`l4desk-pin-operation:${orgId}:${terminal.id}`);
  const remoteBlockedReason = canIssuePin && (pinState.loading || pinState.loadFailed)
    ? "Сначала нужно проверить текущий ручной PIN"
    : manualActive ? "Ручной PIN уже выдан. Дождитесь завершения ручной установки."
    : manualUncertain ? "Сначала уточните результат выдачи ручного PIN" : undefined;
  const manualBlockedReason = canRenew && (renewalState.loading || renewalState.loadFailed) ? "Сначала нужно проверить состояние удалённого продления"
    : remoteActive ? "Удалённое продление уже выполняется" : undefined;
  const { token } = theme.useToken();
  return <Drawer title={`Сертификат устройства ${terminal.device_id}`} open
    onClose={() => { if (!locked) onClose(); }} closable={!locked} mask={{ closable: !locked }}
    width="min(840px, 100vw)" destroyOnHidden>
    <Space orientation="vertical" size={20} style={{ width: "100%" }}>
      <Text type="secondary">Тенант {orgId} · Устройство {terminal.device_id} · SN {terminal.sn}</Text>
      <Steps size="small" current={flow ? 1 : 0} items={[{ title: "Выберите способ" }, { title: "Проверьте и выполните действие" }]} />
      {!flow ? <Row gutter={[16, 16]} style={{ width: "100%", margin: 0 }}>
        {canRenew && <Col xs={24} md={12} style={{ paddingLeft: 0 }}><Card style={{ height: "100%", background: token.colorPrimaryBg, borderColor: token.colorPrimaryBorder }}>
          <CloudSyncOutlined style={{ fontSize: 32, color: token.colorPrimary }} />
          <Title level={4}>Удалённое продление</Title>
          <Paragraph>Сервер создаст заявку и отправит команду терминалу. PIN вручную вводить не нужно.</Paragraph>
          <Paragraph type="secondary">Для терминала с действующим сертификатом и агентом продления. Офлайн-терминал сможет выполнить команду после подключения.</Paragraph>
          <CertificateRenewal deviceId={terminal.device_id} state={renewalState}
            onSelect={() => setFlow("remote")} blockedReason={remoteBlockedReason} />
        </Card></Col>}
        {canIssuePin && <Col xs={24} md={12} style={{ paddingRight: 0 }}><Card style={{ background: token.colorWarningBg, borderColor: token.colorWarningBorder }}>
          <KeyOutlined style={{ fontSize: 32, color: token.colorWarning }} />
          <Title level={4}>Ручная установка по PIN</Title>
          <Paragraph>Получите PIN и передайте его инженеру для установки сертификата на терминале.</Paragraph>
          <Paragraph type="secondary">Для первичной установки, истёкшего сертификата или ручной работы. Команда удалённого продления не отправляется.</Paragraph>
          <Button aria-label="Выбрать ручную установку" loading={pinState.loading} disabled={pinState.loading || pinState.loadFailed || manualActive || !!manualBlockedReason}
            onClick={() => setFlow("manual")}>Выбрать ручную установку</Button>
          {manualBlockedReason && <Paragraph type="secondary">{manualBlockedReason}</Paragraph>}
        </Card>
          <Card title="PIN для ручной установки" style={{ marginTop: 12 }}>
            {pinState.loading ? <Spin aria-label="Загрузка PIN" /> : pinState.loadFailed
              ? <Alert type="error" showIcon title="Не удалось проверить текущий PIN" />
              : pinState.pin ? <PinSummary pin={pinState.pin} /> : <Text type="secondary">PIN для этого устройства ещё не выдан.</Text>}
            <Button aria-label="Проверить PIN" style={{ marginTop: 12 }} loading={pinState.loading} onClick={() => void pinState.reload()}>Проверить PIN</Button>
          </Card>
        </Col>}
      </Row> : <>
        <Button icon={<ArrowLeftOutlined />} aria-label="Выбрать другой способ" disabled={locked} onClick={() => setFlow(undefined)}>Выбрать другой способ</Button>
        <Alert showIcon type={flow === "remote" ? "info" : "warning"}
          title={flow === "remote" ? "Выбрано удалённое продление" : "Выбрана ручная установка по PIN"} />
        {flow === "remote" ? <CertificateRenewal deviceId={terminal.device_id} confirmation="inline" state={renewalState}
          blockedReason={remoteBlockedReason} onInteractionChange={setLocked} />
          : <ManualCertificatePin terminal={terminal} orgId={orgId} state={pinState} blockedReason={manualBlockedReason}
          onIssued={result => { pinState.acceptIssued(result); setFlow(undefined); onPinIssued(); }} onInteractionChange={setLocked} />}
      </>}
    </Space>
  </Drawer>;
}
