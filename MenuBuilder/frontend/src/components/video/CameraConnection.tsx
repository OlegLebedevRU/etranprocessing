import { Alert, Button, List, Space, Tag, Typography } from "antd";
import type { CameraState } from "../../api/cameras";

const states: Record<string, string> = {
  idle: "Поиск не запущен", link_down: "Нет связи на port-2", learning_mac: "Определяем MAC на port-2…",
  resolving_ip: "Ищем IP камеры…", probing_api: "Проверяем камеру…", auth_required: "Нужны логин и пароль камеры",
  ready: "Камера найдена", not_found: "Подходящая камера не найдена", cancelled: "Поиск отменён", failed: "Поиск завершился с ошибкой",
};

export function CameraConnection({ camera, disabled, searching, onSearch, onBind, onClearCredentials }: {
  camera?: CameraState; disabled: boolean; searching: boolean;
  onSearch: () => void; onBind: (mac: string, generation: number) => void; onClearCredentials: () => void;
}) {
  const discovery = camera?.discovery;
  const negotiated = camera?.camera_contract_revision === "siplite-camera/1.0.0";
  const canScan = negotiated && camera?.capabilities?.methods?.includes(165);
  const canBind = negotiated && camera?.capabilities?.methods?.includes(166) && !!camera?.revision;
  return <Space orientation="vertical" style={{ width: "100%" }}>
    <Typography.Text>Камера подключается к port-2. Режим сети: {discovery?.ethernet_mode ?? "неизвестен"}.</Typography.Text>
    <Typography.Text>{searching ? "Поиск камеры выполняется…" : states[discovery?.state ?? ""] ?? "Состояние поиска неизвестно"}</Typography.Text>
    <Button disabled={disabled || !canScan} loading={searching} onClick={onSearch}>Найти камеру на port-2</Button>
    {discovery?.state === "auth_required" && <Alert type="warning" showIcon title="Камера найдена, но доступ не подтверждён. Укажите её логин и пароль, когда смена учётных данных станет доступна." />}
    <List size="small" dataSource={discovery?.candidates ?? []} locale={{ emptyText: "Подтверждённых кандидатов пока нет" }} renderItem={candidate =>
      <List.Item actions={[<Button key="bind" disabled={disabled || !canBind || !candidate.port_verified}
        onClick={() => onBind(candidate.mac, discovery!.generation)}>Выбрать</Button>]}>
        <Space wrap><Typography.Text>{candidate.ip ?? "IP ещё не определён"}</Typography.Text><Typography.Text type="secondary">{candidate.mac}</Typography.Text>
          <Tag color={candidate.port_verified ? "green" : "default"}>{candidate.port_verified ? "port-2 подтверждён" : "Порт не подтверждён"}</Tag>
          {candidate.auth === "required" && <Tag color="orange">Нужен пароль</Tag>}</Space>
      </List.Item>} />
    <Typography.Text type="secondary">Учётные данные: {camera?.auth_source === "custom" ? "пользовательские" : camera?.auth_source === "native" ? "штатные" : "не подтверждены"}. Пароль не отображается.</Typography.Text>
    <Button disabled={disabled || !canBind || camera?.auth_source !== "custom"} onClick={onClearCredentials}>Вернуть штатные учётные данные</Button>
    {!negotiated && <Typography.Text type="secondary">Поиск и привязка станут доступны после обновления терминала.</Typography.Text>}
  </Space>;
}
