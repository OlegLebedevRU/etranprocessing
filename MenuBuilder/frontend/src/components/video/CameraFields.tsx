import { useState } from "react";
import { Alert, Button, Input, InputNumber, Select, Space, Switch, Typography } from "antd";
import type { CameraPatch, CameraState, CameraValue } from "../../api/cameras";
import { cameraPatch, editableCameraFields, reportedCameraField } from "../../utils/cameraDraft";

const labels: Record<string, string> = {
  "video0.codec": "Кодек камеры", "video0.size": "Размер кадра", "video0.fps": "Частота кадров, FPS",
  "video0.bitrate": "Битрейт, кбит/с", "video0.gopSize": "Интервал ключевых кадров", "video0.rcMode": "Управление битрейтом",
  "image.mirror": "Отражение по горизонтали", "image.flip": "Отражение по вертикали", "image.rotate": "Поворот",
  "image.contrast": "Контраст", "image.luminance": "Яркость", "image.saturation": "Насыщенность",
  "image.hue": "Оттенок", "nightMode.lightMonitor": "Автоматический день / ночь",
};

export function CameraFields({ camera, disabled, onApply }: {
  camera?: CameraState; disabled: boolean;
  onApply: (patch: CameraPatch, revision: string) => Promise<boolean>;
}) {
  const [draft, setDraft] = useState<{ revision: string; values: Record<string, CameraValue>; base: Record<string, CameraValue | undefined> } | null>(null);
  const fields = editableCameraFields(camera);
  const changed = !!draft && Object.keys(draft.values).length > 0;
  const conflict = changed && draft.revision !== camera?.revision;
  const negotiated = camera?.camera_contract_revision === "siplite-camera/1.0.0" && camera.capabilities?.methods?.includes(161);
  const blocked = disabled || !negotiated || !camera?.revision;
  const edit = (path: string, value: CameraValue) => {
    if (blocked) return;
    setDraft(current => {
      const base = current?.base ?? Object.fromEntries(fields.map(field => [field.path, reportedCameraField(camera, field.path)]));
      const values = { ...current?.values };
      if (Object.is(base[path], value)) delete values[path]; else values[path] = value;
      return { revision: current?.revision ?? camera!.revision!, values, base };
    });
  };
  if (!negotiated || !fields.length) return <Typography.Text type="secondary">Настройки видео и изображения появятся после подтверждения новой версии управления камерой.</Typography.Text>;
  return <Space orientation="vertical" style={{ width: "100%" }}>
    {conflict && <Alert type="warning" showIcon title="Настройки камеры изменились. Ваши правки сохранены; перечитайте состояние перед применением." />}
    {fields.map(field => {
      const value = draft?.values[field.path] ?? reportedCameraField(camera, field.path);
      return <label key={field.path} style={{ display: "flex", justifyContent: "space-between", gap: 12, alignItems: "center", flexWrap: "wrap" }}>
        <span>{labels[field.path]}</span>
        {field.type === "boolean" ? <Switch aria-label={labels[field.path]} disabled={blocked} checked={value === true} onChange={value => edit(field.path, value)} /> :
          field.values.length ? <Select aria-label={labels[field.path]} style={{ minWidth: 150 }} disabled={blocked} value={value as string | number | undefined}
            options={field.values.filter(item => typeof item !== "boolean").map(item => ({ value: item as string | number, label: String(item) }))} onChange={value => edit(field.path, value)} /> :
          field.type === "integer" || field.type === "number" ? <InputNumber aria-label={labels[field.path]} disabled={blocked} value={typeof value === "number" ? value : null}
            min={field.minimum ?? undefined} max={field.maximum ?? undefined} precision={field.type === "integer" ? 0 : undefined}
            onChange={value => { if (value !== null) edit(field.path, value); }} /> :
            <Input aria-label={labels[field.path]} disabled={blocked} style={{ maxWidth: 180 }} value={typeof value === "string" ? value : ""} placeholder={field.path === "video0.size" ? "Native / 2592x1944" : undefined}
              onChange={event => edit(field.path, event.target.value)} />}
      </label>;
    })}
    <Space wrap>
      <Button type="primary" disabled={blocked || !changed || conflict} onClick={async () => { if (draft && await onApply(cameraPatch(draft.values, fields), draft.revision)) setDraft(null); }}>Применить изменения</Button>
      <Button disabled={!draft || disabled} onClick={() => setDraft(null)}>Перечитать / отменить правки</Button>
    </Space>
    <Typography.Text type="secondary">Применяются только изменённые поля. Пустой размер — Native: Majestic берёт фактический размер источника. Нулевые FPS и битрейт означают Auto, если камера их поддерживает. Наблюдаемый формат показан отдельно и не записывается вместо Auto. Для изменений сначала остановите просмотр.</Typography.Text>
  </Space>;
}
