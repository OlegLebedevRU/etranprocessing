import { useCallback, useEffect, useRef, useState } from "react";
import { Button, Card, Input, InputNumber, Modal, Select, Space, Table, Tag, Typography, message } from "antd";
import { useNavigate } from "react-router";
import client from "../api/client";
import { generateTerminalPin } from "../api/admin";
import { useAdminDeviceEvents } from "../components/AdminDeviceEventsContext";

type EventState = "new" | "in_progress" | "closed";
interface DeviceEvent {
  id: number; terminal_id: number; device_id: number; sn: string; org_id: number;
  operation_id: string; event_type: string; status: EventState; created_at: string;
  updated_by: string | null; comment: string | null;
  details: { source_org_id?: number; target_org_id?: number; message?: string; error_code?: string };
}
const states = [
  { value: "new", label: "Новое" }, { value: "in_progress", label: "В работе" }, { value: "closed", label: "Закрыто" },
];
const types = [
  { value: "identity_old_certificate", label: "Используется прежний сертификат" },
  { value: "identity_iot_sync_failed", label: "Требуется синхронизация с IoT" },
];

export default function AdminDeviceEventsPage() {
  const navigate = useNavigate();
  const { refresh } = useAdminDeviceEvents();
  const [items, setItems] = useState<DeviceEvent[]>([]);
  const [total, setTotal] = useState(0);
  const [page, setPage] = useState(1);
  const [state, setState] = useState<EventState | undefined>("new");
  const [eventType, setEventType] = useState<string>();
  const [orgId, setOrgId] = useState<number | null>(null);
  const [terminalId, setTerminalId] = useState<number | null>(null);
  const [loading, setLoading] = useState(false);
  const [editing, setEditing] = useState<DeviceEvent | null>(null);
  const [nextState, setNextState] = useState<EventState>("in_progress");
  const [comment, setComment] = useState("");
  const [saving, setSaving] = useState(false);
  const generation = useRef(0);
  const load = useCallback(async () => {
    const requestId = ++generation.current;
    setLoading(true);
    try {
      const { data } = await client.get<{ items: DeviceEvent[]; total: number }>("/admin/device-events", {
        params: { state, event_type: eventType, org_id: orgId ?? undefined, terminal_id: terminalId ?? undefined, page, page_size: 50 },
      });
      if (generation.current === requestId) { setItems(data.items); setTotal(data.total); }
    } catch (error) { if (generation.current === requestId) message.error(error instanceof Error ? error.message : "Не удалось загрузить события"); }
    finally { if (generation.current === requestId) setLoading(false); }
  }, [state, eventType, orgId, terminalId, page]);
  useEffect(() => { void load(); return () => { generation.current++; }; }, [load]);
  const save = async () => {
    if (!editing) return;
    setSaving(true);
    try {
      await client.patch(`/admin/device-events/${editing.id}`, { status: nextState, comment });
      setEditing(null); refresh(); await load();
    } catch (error) { message.error(error instanceof Error ? error.message : "Не удалось сохранить событие"); }
    finally { setSaving(false); }
  };
  const showTransfer = async (item: DeviceEvent) => {
    try {
      const { data } = await client.get(`/admin/terminals/${item.terminal_id}/identity-change`, { params: { operation_id: item.operation_id } });
      if (!data) { message.warning("Операция не найдена"); return; }
      Modal.info({ title: "Операция смены идентичности", content: <Space direction="vertical"><Typography.Text>{data.source_org_id} → {data.target_org_id}</Typography.Text><Typography.Text>Состояние: {data.status}</Typography.Text><Typography.Text>Инициатор: {data.requested_by}</Typography.Text><Typography.Text copyable>{data.operation_id}</Typography.Text></Space> });
    } catch (error) { message.error(error instanceof Error ? error.message : "Не удалось прочитать операцию"); }
  };
  const issuePin = async (item: DeviceEvent) => {
    try {
      const pin = await generateTerminalPin(item.terminal_id);
      Modal.info({ title: `PIN терминала #${item.device_id}`, content: <Space direction="vertical"><Typography.Text copyable strong>{pin.pin}</Typography.Text>{pin.certificate_org_id != null && <Typography.Text>Тенант: #{pin.certificate_org_id}</Typography.Text>}<Typography.Text>Действует до {new Date(pin.expires_at).toLocaleString()}</Typography.Text></Space> });
    } catch (error) { message.error(error instanceof Error ? error.message : "Не удалось выдать PIN"); }
  };
  return <Card title="События устройств" extra={<Button onClick={() => { void load(); refresh(); }}>Обновить</Button>}>
    <Space wrap style={{ marginBottom: 16 }}>
      <Select aria-label="Состояние события" allowClear placeholder="Все состояния" value={state} options={states} style={{ width: 180 }} onChange={value => { setState(value); setPage(1); }} />
      <Select aria-label="Тип события" allowClear placeholder="Все типы" value={eventType} options={types} style={{ width: 290 }} onChange={value => { setEventType(value); setPage(1); }} />
      <InputNumber aria-label="ID тенанта" min={1} placeholder="ID тенанта" value={orgId} onChange={value => { setOrgId(value); setPage(1); }} />
      <InputNumber aria-label="ID записи терминала" min={1} placeholder="ID терминала" value={terminalId} onChange={value => { setTerminalId(value); setPage(1); }} />
    </Space>
    <Table<DeviceEvent> rowKey="id" loading={loading} dataSource={items} pagination={{ current: page, total, pageSize: 50, showSizeChanger: false, onChange: setPage }} scroll={{ x: 1000 }} columns={[
      { title: "Обнаружено", dataIndex: "created_at", render: value => new Date(value).toLocaleString() },
      { title: "Терминал", render: (_, item) => <Space direction="vertical" size={0}><Typography.Text strong>#{item.device_id}</Typography.Text><Typography.Text code>{item.sn}</Typography.Text></Space> },
      { title: "Событие", render: (_, item) => <Space direction="vertical" size={0}><Typography.Text>{types.find(type => type.value === item.event_type)?.label ?? item.event_type}</Typography.Text><Typography.Text type="secondary">{item.details.source_org_id} → {item.details.target_org_id}</Typography.Text><Typography.Text>{item.details.message}</Typography.Text>{item.details.error_code && <Typography.Text code>{item.details.error_code}</Typography.Text>}</Space> },
      { title: "Состояние", render: (_, item) => <Tag color={item.status === "new" ? "error" : item.status === "closed" ? "success" : "processing"}>{states.find(state => state.value === item.status)?.label}</Tag> },
      { title: "Комментарий", render: (_, item) => <Space direction="vertical" size={0}><Typography.Text>{item.comment}</Typography.Text><Typography.Text type="secondary">{item.updated_by}</Typography.Text></Space> },
      { title: "Действия", render: (_, item) => <Space wrap><Button size="small" onClick={() => navigate(`/admin/terminals?terminal_id=${item.terminal_id}&org_id=${item.org_id}`)}>Терминал</Button><Button size="small" onClick={() => void showTransfer(item)}>Передача</Button><Button size="small" onClick={() => void issuePin(item)}>Выдать PIN</Button><Button size="small" onClick={() => { setEditing(item); setNextState(item.status === "new" ? "in_progress" : item.status); setComment(item.comment ?? ""); }}>Обработать</Button></Space> },
    ]} />
    <Modal title="Обработка события" open={Boolean(editing)} onCancel={() => setEditing(null)} onOk={() => void save()} confirmLoading={saving} okText="Сохранить" cancelText="Отмена">
      <Space direction="vertical" style={{ width: "100%" }}><Select aria-label="Новое состояние" value={nextState} options={states} onChange={setNextState} style={{ width: "100%" }} /><Input.TextArea aria-label="Комментарий администратора" value={comment} onChange={event => setComment(event.target.value)} maxLength={4000} rows={4} placeholder="Комментарий администратора" /></Space>
    </Modal>
  </Card>;
}
