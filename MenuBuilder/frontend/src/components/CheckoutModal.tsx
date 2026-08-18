import { useState } from "react";
import {
  Alert,
  Button,
  Descriptions,
  Divider,
  Modal,
  Result,
  Space,
  Table,
  Tag,
  Typography,
  message,
} from "antd";
import { CopyOutlined, SafetyCertificateOutlined } from "@ant-design/icons";
import {
  createCheckout,
  confirmPayment,
  type BillingTerminal,
} from "../api/billing";
import { requestCertificatePin } from "../api/certificate-pin";
import { formatMoneyMinor, formatDate } from "../utils/billing";

const { Text } = Typography;

export interface CartLine {
  terminal: BillingTerminal;
  /** Pay for the license: 1 period if lapsed, plus the advance periods. */
  license: boolean;
  /** Pay for a certificate PIN. */
  cert: boolean;
  licenseAmountMinor: number;
  certAmountMinor: number;
  newExpiresAt: string | null;
}

interface IssuedPin {
  deviceId: number;
  pin: string;
  expiresAt: string;
}

interface CheckoutModalProps {
  open: boolean;
  lines: CartLine[];
  advancePeriods: number;
  onClose: () => void;
  onPaid: () => void;
}

/**
 * Confirms and pays a mixed cart of license renewals and certificate PINs in a
 * single billing order. PIN values are not returned by the confirm endpoint, so
 * they are fetched afterwards via the idempotent certificate-pin endpoint.
 */
export default function CheckoutModal({
  open,
  lines,
  advancePeriods,
  onClose,
  onPaid,
}: CheckoutModalProps) {
  const [paying, setPaying] = useState(false);
  const [paid, setPaid] = useState(false);
  const [pins, setPins] = useState<IssuedPin[]>([]);
  const [error, setError] = useState<string | null>(null);

  const total = lines.reduce(
    (sum, l) => sum + l.licenseAmountMinor + l.certAmountMinor,
    0,
  );
  const licenseCount = lines.filter((l) => l.license).length;
  const certCount = lines.filter((l) => l.cert).length;

  const reset = () => {
    setPaid(false);
    setPins([]);
    setError(null);
  };

  const handleClose = () => {
    reset();
    onClose();
  };

  const handlePay = async () => {
    setPaying(true);
    setError(null);
    try {
      const checkout = await createCheckout({
        items: lines.map((l) => ({
          terminal_id: l.terminal.terminal_id,
          advance_periods: l.license ? advancePeriods : 0,
          include_license: l.license,
          include_cert_pin: l.cert,
        })),
      });
      await confirmPayment(checkout.order_id);

      const certLines = lines.filter((l) => l.cert);
      const issued: IssuedPin[] = [];
      for (const line of certLines) {
        try {
          const res = await requestCertificatePin(line.terminal.terminal_id);
          if (res.status === "pin_ready") {
            issued.push({
              deviceId: line.terminal.device_id,
              pin: res.pin,
              expiresAt: res.expires_at,
            });
          }
        } catch {
          // A PIN that cannot be read back is still issued server-side; the
          // terminal row will show it as available on the next refresh.
        }
      }
      setPins(issued);
      setPaid(true);
      onPaid();
    } catch (e: unknown) {
      setError(describePaymentError(e));
    } finally {
      setPaying(false);
    }
  };

  const handleCopy = async (pin: string) => {
    try {
      await navigator.clipboard.writeText(pin);
      message.success("PIN скопирован");
    } catch {
      message.error("Не удалось скопировать PIN");
    }
  };

  return (
    <Modal
      open={open}
      onCancel={handleClose}
      title="Оплата"
      width={640}
      destroyOnHidden
      footer={
        paid
          ? [
              <Button key="close" type="primary" onClick={handleClose}>
                Готово
              </Button>,
            ]
          : [
              <Button key="cancel" onClick={handleClose}>
                Отмена
              </Button>,
              <Button
                key="pay"
                type="primary"
                loading={paying}
                disabled={lines.length === 0}
                onClick={handlePay}
              >
                Оплатить {formatMoneyMinor(total)}
              </Button>,
            ]
      }
    >
      {!paid && (
        <>
          {error && (
            <Alert
              type="error"
              showIcon
              message="Оплата не прошла"
              description={error}
              style={{ marginBottom: 16 }}
            />
          )}
          <Table
            size="small"
            pagination={false}
            rowKey={(l) => l.terminal.terminal_id}
            dataSource={lines}
            columns={[
              {
                title: "Терминал",
                key: "terminal",
                render: (_: unknown, l: CartLine) => l.terminal.device_id,
              },
              {
                title: "Позиции",
                key: "items",
                render: (_: unknown, l: CartLine) => (
                  <Space direction="vertical" size={0}>
                    {l.license && (
                      <Text>
                        Лицензия
                        {advancePeriods > 0
                          ? ` + ${advancePeriods * (l.terminal.billing_period_months || 1)} мес`
                          : ""}
                        {l.newExpiresAt && (
                          <Text type="secondary">
                            {" "}
                            → {formatDate(l.newExpiresAt)}
                          </Text>
                        )}
                      </Text>
                    )}
                    {l.cert && (
                      <Text>
                        <SafetyCertificateOutlined /> Сертификат
                      </Text>
                    )}
                  </Space>
                ),
              },
              {
                title: "Сумма",
                key: "amount",
                align: "right",
                render: (_: unknown, l: CartLine) =>
                  formatMoneyMinor(l.licenseAmountMinor + l.certAmountMinor),
              },
            ]}
          />
          <Divider style={{ margin: "16px 0 8px" }} />
          <Descriptions size="small" column={1}>
            <Descriptions.Item label="Лицензии">
              {licenseCount} шт.
            </Descriptions.Item>
            <Descriptions.Item label="Сертификаты">
              {certCount} шт.
            </Descriptions.Item>
            <Descriptions.Item label="Итого">
              <Text strong style={{ fontSize: 16 }}>
                {formatMoneyMinor(total)}
              </Text>
            </Descriptions.Item>
          </Descriptions>
        </>
      )}

      {paid && (
        <>
          <Result
            status="success"
            title="Оплата прошла успешно"
            subTitle={`Списано ${formatMoneyMinor(total)}`}
            style={{ padding: "8px 0" }}
          />
          {pins.length > 0 && (
            <>
              <Alert
                type="info"
                showIcon
                style={{ marginBottom: 12 }}
                message="Введите PIN вручную на терминале, чтобы выпустить сертификат"
              />
              <Table
                size="small"
                pagination={false}
                rowKey={(p) => p.deviceId}
                dataSource={pins}
                columns={[
                  { title: "Терминал", dataIndex: "deviceId", key: "deviceId" },
                  {
                    title: "PIN",
                    dataIndex: "pin",
                    key: "pin",
                    render: (pin: string) => (
                      <Space>
                        <Text strong style={{ fontSize: 16, letterSpacing: 2 }}>
                          {pin}
                        </Text>
                        <Button
                          size="small"
                          icon={<CopyOutlined />}
                          onClick={() => handleCopy(pin)}
                        />
                      </Space>
                    ),
                  },
                  {
                    title: "Действителен до",
                    dataIndex: "expiresAt",
                    key: "expiresAt",
                    render: (v: string) => (
                      <Tag>{new Date(v).toLocaleString("ru-RU")}</Tag>
                    ),
                  },
                ]}
              />
            </>
          )}
        </>
      )}
    </Modal>
  );
}

function describePaymentError(e: unknown): string {
  // The axios interceptor collapses errors into Error(detail), so match on text.
  const msg = e instanceof Error ? e.message : String(e);
  if (msg.includes("pending order already exists")) {
    return (
      "По одному из терминалов уже есть незавершённый заказ. " +
      "Дождитесь его закрытия или обновите страницу."
    );
  }
  if (msg.includes("not enabled for this organization")) {
    return "Самостоятельный выпуск PIN не включён для вашей организации.";
  }
  if (msg.includes("administratively disabled")) {
    return "Терминал заблокирован администратором. Обратитесь в поддержку.";
  }
  return msg || "Неизвестная ошибка";
}
