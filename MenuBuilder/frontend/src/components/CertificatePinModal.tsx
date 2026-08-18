import { useCallback, useEffect, useState } from "react";
import {
  Alert,
  Button,
  Descriptions,
  Input,
  Modal,
  Result,
  Space,
  Statistic,
  Steps,
  Typography,
  message,
} from "antd";
import {
  CheckCircleFilled,
  CopyOutlined,
  LoadingOutlined,
  SafetyCertificateOutlined,
} from "@ant-design/icons";
import {
  requestCertificatePin,
  type CertificatePinResponse,
} from "../api/certificate-pin";
import { confirmPayment } from "../api/billing";
import { formatMoneyMinor } from "../utils/billing";

const { Text, Paragraph } = Typography;

interface CertificatePinModalProps {
  open: boolean;
  terminal: { terminal_id: number; device_id: number } | null;
  onClose: () => void;
  /** Called once a PIN has been successfully issued (paid or free). */
  onIssued?: () => void;
}

type FlowStep = "checking" | "payment_required" | "paying" | "pin_ready" | "error";

/**
 * Implements the tenant-facing certificate PIN flow from ANALYSIS.md §6:
 * request -> policy/price snapshot -> optional payment -> PIN_READY.
 *
 * This never talks to the CA directly — it only asks the backend for
 * permission to create a PIN. The PIN itself is later entered manually on
 * the terminal, which triggers the existing CHECK/SETUP flow server-side.
 */
export default function CertificatePinModal({
  open,
  terminal,
  onClose,
  onIssued,
}: CertificatePinModalProps) {
  const [step, setStep] = useState<FlowStep>("checking");
  const [result, setResult] = useState<CertificatePinResponse | null>(null);
  const [errorMsg, setErrorMsg] = useState<string | null>(null);
  const [paying, setPaying] = useState(false);
  const [revealed, setRevealed] = useState(false);

  const load = useCallback(async () => {
    if (!terminal) return;
    setStep("checking");
    setErrorMsg(null);
    try {
      const res = await requestCertificatePin(terminal.terminal_id);
      setResult(res);
      setStep(res.status === "pin_ready" ? "pin_ready" : "payment_required");
      if (res.status === "pin_ready") onIssued?.();
    } catch (e: unknown) {
      const msg = e instanceof Error ? e.message : "Ошибка получения PIN";
      setErrorMsg(msg);
      setStep("error");
    }
  }, [terminal, onIssued]);

  useEffect(() => {
    if (open) {
      setRevealed(false);
      load();
    }
  }, [open, load]);

  const handlePay = async () => {
    if (result?.status !== "payment_required") return;
    setPaying(true);
    setStep("paying");
    try {
      // Mock payment provider: confirm immediately (matches billing.tsx pattern).
      await confirmPayment(result.order_id);
      // The PIN is created server-side as part of payment confirmation.
      // Re-request is idempotent and returns the now-ready PIN.
      const res = await requestCertificatePin(result.terminal_id);
      setResult(res);
      if (res.status === "pin_ready") {
        setStep("pin_ready");
        onIssued?.();
      } else {
        // Should not normally happen — surface as payment_required again.
        setStep("payment_required");
      }
    } catch (e: unknown) {
      const msg = e instanceof Error ? e.message : "Ошибка оплаты";
      setErrorMsg(msg);
      setStep("error");
    } finally {
      setPaying(false);
    }
  };

  const handleCopyPin = async () => {
    if (result?.status !== "pin_ready") return;
    try {
      await navigator.clipboard.writeText(result.pin);
      message.success("PIN скопирован в буфер обмена");
    } catch {
      message.error("Не удалось скопировать PIN");
    }
  };

  const stepIndex = { checking: 0, payment_required: 1, paying: 1, pin_ready: 2, error: 0 }[step];

  return (
    <Modal
      open={open}
      onCancel={onClose}
      title={
        <Space>
          <SafetyCertificateOutlined />
          Получение PIN для перевыпуска сертификата
          {terminal && <Text type="secondary">· терминал {terminal.device_id}</Text>}
        </Space>
      }
      footer={
        step === "payment_required"
          ? [
              <Button key="cancel" onClick={onClose}>
                Отмена
              </Button>,
              <Button key="pay" type="primary" loading={paying} onClick={handlePay}>
                Оплатить{" "}
                {result?.status === "payment_required" &&
                  formatMoneyMinor(result.amount_minor, result.currency)}
              </Button>,
            ]
          : step === "error"
            ? [
                <Button key="retry" onClick={load}>
                  Повторить
                </Button>,
                <Button key="close" type="primary" onClick={onClose}>
                  Закрыть
                </Button>,
              ]
            : step === "pin_ready"
              ? [
                  <Button key="close" type="primary" onClick={onClose}>
                    Готово
                  </Button>,
                ]
              : null
      }
      destroyOnHidden
      width={480}
    >
      <Steps
        size="small"
        current={stepIndex}
        status={step === "error" ? "error" : undefined}
        items={[
          { title: "Проверка условий" },
          { title: "Оплата", subTitle: "если требуется" },
          { title: "PIN готов" },
        ]}
        style={{ marginBottom: 24, marginTop: 8 }}
      />

      {(step === "checking" || step === "paying") && (
        <div style={{ textAlign: "center", padding: "24px 0" }}>
          <LoadingOutlined style={{ fontSize: 28 }} spin />
          <Paragraph type="secondary" style={{ marginTop: 12 }}>
            {step === "paying" ? "Подтверждение оплаты…" : "Проверяем условия выпуска PIN…"}
          </Paragraph>
        </div>
      )}

      {step === "error" && errorMsg && (
        <Alert type="error" showIcon message="Не удалось получить PIN" description={errorMsg} />
      )}

      {step === "payment_required" && result?.status === "payment_required" && (
        <Alert
          type="warning"
          showIcon
          message="Требуется оплата"
          description={
            <>
              Перевыпуск сертификата для этого терминала платный. Сумма к оплате:{" "}
              <Text strong>{formatMoneyMinor(result.amount_minor, result.currency)}</Text>.
              После оплаты PIN будет выдан автоматически.
            </>
          }
        />
      )}

      {step === "pin_ready" && result?.status === "pin_ready" && (
        <>
          <Result
            icon={<CheckCircleFilled style={{ color: "#52c41a" }} />}
            title="PIN готов"
            subTitle="Введите его вручную на терминале, чтобы выпустить новый сертификат"
            style={{ padding: "8px 0 16px" }}
          />
          <Space.Compact style={{ width: "100%", marginBottom: 16 }}>
            <Input.Password
              value={result.pin}
              readOnly
              visibilityToggle={{ visible: revealed, onVisibleChange: setRevealed }}
              style={{ fontSize: 20, letterSpacing: 4, textAlign: "center" }}
            />
            <Button icon={<CopyOutlined />} onClick={handleCopyPin}>
              Копировать
            </Button>
          </Space.Compact>
          <Descriptions size="small" column={1} bordered>
            <Descriptions.Item label="Действителен до">
              <Statistic.Countdown
                value={new Date(result.expires_at).getTime()}
                format="D [дн.] HH:mm:ss"
                valueStyle={{ fontSize: 14 }}
              />
            </Descriptions.Item>
          </Descriptions>
          <Alert
            style={{ marginTop: 16 }}
            type="info"
            showIcon
            message="PIN одноразовый и действует ограниченное время. Не передавайте его третьим лицам."
          />
        </>
      )}
    </Modal>
  );
}
