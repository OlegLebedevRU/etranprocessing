import { useState } from "react";
import {
  Alert,
  Button,
  DatePicker,
  Input,
  InputNumber,
  Modal,
  Space,
  Switch,
  Table,
  message,
} from "antd";
import dayjs from "dayjs";
import {
  correctSubscription,
  getSubscriptions,
  type Subscription,
} from "../api/subscriptions";
import { subscriptionLabels } from "../utils/subscriptionPresentation";

export default function AdminSubscriptionsPanel() {
  const [tenant, setTenant] = useState<number | null>(null);
  const [rows, setRows] = useState<Subscription[]>([]);
  const [selected, setSelected] = useState<Subscription | null>(null);
  const [until, setUntil] = useState<string | null>(null);
  const [revoke, setRevoke] = useState(false);
  const [reason, setReason] = useState("");
  const [operation, setOperation] = useState("");
  const [busy, setBusy] = useState(false);
  const load = async () => {
    if (!tenant) return;
    setBusy(true);
    try {
      setRows((await getSubscriptions(tenant)).items);
    } catch {
      setRows([]);
      message.error("Не удалось загрузить подписки организации L4Desk.");
    } finally {
      setBusy(false);
    }
  };
  const save = async () => {
    if (!selected) return;
    setBusy(true);
    try {
      await correctSubscription(selected.terminal_id, {
        paid_until: revoke ? null : until,
        expected_paid_until: selected.paid_until,
        reason,
        operation_id: operation,
      });
      setSelected(null);
      message.success("Срок изменён, операция записана в аудит.");
      await load();
    } catch {
      message.error(
        "Корректировка не подтверждена. Обновите данные; при повторе используется тот же ключ операции.",
      );
    } finally {
      setBusy(false);
    }
  };
  return (
    <Space orientation="vertical" style={{ width: "100%" }}>
      <Alert
        type="info"
        title="Подписки терминалов"
        description="Корректировка меняет только срок. Она не включает отключённый администратором терминал и не создаёт денежную проводку. Причина сохраняется в аудите."
      />
      <Space>
        <InputNumber
          min={1}
          placeholder="ID организации"
          value={tenant}
          onChange={(value) => {
            setTenant(value);
            setRows([]);
          }}
        />
        <Button loading={busy} disabled={!tenant} onClick={() => void load()}>
          Показать
        </Button>
      </Space>
      <Table
        rowKey="terminal_id"
        dataSource={rows}
        columns={[
          { title: "Терминал", dataIndex: "name" },
          { title: "Статус", render: (_, t) => subscriptionLabels[t.state] },
          {
            title: "Оплачен до",
            render: (_, t) =>
              t.paid_until
                ? new Date(t.paid_until).toLocaleString("ru-RU")
                : "—",
          },
          {
            title: "Действие",
            render: (_, t) => (
              <Button
                disabled={t.is_free}
                onClick={() => {
                  setSelected(t);
                  setUntil(t.paid_until);
                  setReason("");
                  setRevoke(false);
                  setOperation(crypto.randomUUID());
                }}
              >
                Корректировать срок
              </Button>
            ),
          },
        ]}
      />
      <Modal
        title={`Корректировка: ${selected?.name || ""}`}
        open={selected !== null}
        onCancel={() => {
          if (!busy) setSelected(null);
        }}
        onOk={() => void save()}
        confirmLoading={busy}
        okText="Сохранить с аудитом"
        okButtonProps={{
          disabled: reason.trim().length < 10 || (!revoke && !until),
        }}
      >
        <Space orientation="vertical" style={{ width: "100%" }}>
          <span>Новый срок по времени браузера</span>
          <DatePicker
            showTime
            disabled={revoke}
            value={until ? dayjs(until) : null}
            onChange={(value) => setUntil(value?.toISOString() || null)}
          />
          <Space>
            <Switch checked={revoke} onChange={setRevoke} />
            Отозвать оплаченный срок
          </Space>
          <Input.TextArea
            placeholder="Причина: минимум 10 символов"
            value={reason}
            maxLength={500}
            onChange={(event) => setReason(event.target.value)}
          />
        </Space>
      </Modal>
    </Space>
  );
}
