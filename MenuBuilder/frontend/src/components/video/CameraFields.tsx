import { useState } from "react";
import { Alert, Button, Collapse, Input, InputNumber, Select, Space, Switch, Typography } from "antd";
import { CameraResolutionField } from "./CameraResolutionField";
import type { CameraPatch, CameraState, CameraValue } from "../../api/cameras";
import { cameraPatch, editableCameraFields, reportedCameraField } from "../../utils/cameraDraft";

const labels: Record<string, string> = {
  "video0.codec": "Кодек камеры", "video0.size": "Размер кадра", "video0.fps": "Частота кадров, FPS",
  "video0.bitrate": "Битрейт, кбит/с", "video0.gopSize": "Интервал ключевых кадров", "video0.rcMode": "Управление битрейтом",
  "image.mirror": "Отражение по горизонтали", "image.flip": "Отражение по вертикали", "image.rotate": "Поворот",
  "image.contrast": "Контраст", "image.brightness": "Яркость", "image.luminance": "Яркость", "image.saturation": "Насыщенность",
  "image.hue": "Оттенок", "image.sharpness": "Резкость", "nightMode.lightMonitor": "Автоматический день / ночь",
};

export function CameraFields({ camera, disabled, group, onApply }: {
  camera?: CameraState; disabled: boolean;
  group?: "video" | "image";
  onApply: (patch: CameraPatch, revision: string) => Promise<boolean>;
}) {
  const [draft, setDraft] = useState<{ revision: string; values: Record<string, CameraValue>; base: Record<string, CameraValue | undefined> } | null>(null);
  const fields = editableCameraFields(camera).filter(field => !group || field.path.startsWith(group === "video" ? "video0." : "image."));
  const changed = !!draft && Object.keys(draft.values).length > 0;
  const size = draft?.values["video0.size"];
  const invalidSize = typeof size === "string" && size !== "" && !/^[1-9]\d*x[1-9]\d*$/.test(size)
    && !fields.find(field => field.path === "video0.size")?.values.includes(size);
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
    {fields.filter(field => field.path === "video0.size" || group !== "video").map(field => {
      const value = draft?.values[field.path] ?? reportedCameraField(camera, field.path);
      return <label key={field.path} style={{ display: "flex", justifyContent: "space-between", gap: 12, alignItems: "center", flexWrap: "wrap" }}>
        <span>{labels[field.path]}</span>
        {field.path === "video0.size" ? <CameraResolutionField field={field} value={typeof value === "string" ? value : ""} disabled={blocked} onChange={value => edit(field.path, value)} /> :
          field.type === "boolean" ? <Switch aria-label={labels[field.path]} disabled={blocked} checked={value === true} onChange={value => edit(field.path, value)} /> :
          field.values.length ? <Select aria-label={labels[field.path]} style={{ minWidth: 150 }} disabled={blocked} value={value as string | number | undefined}
            options={field.values.filter(item => typeof item !== "boolean").map(item => ({ value: item as string | number, label: String(item) }))} onChange={value => edit(field.path, value)} /> :
          field.type === "integer" || field.type === "number" ? <InputNumber aria-label={labels[field.path]} disabled={blocked} value={typeof value === "number" ? value : null}
            min={field.minimum ?? undefined} max={field.maximum ?? undefined} precision={field.type === "integer" ? 0 : undefined}
            onChange={value => { if (value !== null) edit(field.path, value); }} /> :
            <Input aria-label={labels[field.path]} disabled={blocked} style={{ maxWidth: 180 }} value={typeof value === "string" ? value : ""} placeholder={field.path === "video0.size" ? "Native / 2592x1944" : undefined}
              onChange={event => edit(field.path, event.target.value)} />}
      </label>;
    })}
    {group === "video" && <Collapse size="small" items={[{ key: "advanced", label: "Дополнительно", children: fields.filter(f => f.path !== "video0.size").map(field => {
      const value = draft?.values[field.path] ?? reportedCameraField(camera, field.path);
      return <label key={field.path} style={{ display: "flex", justifyContent: "space-between", alignItems: "center", gap: 8, marginBottom: 8 }}>
        <span>{labels[field.path]}</span>
        {field.values.length ? <Select aria-label={labels[field.path]} style={{ width: 145 }} disabled={blocked} value={value as string | number}
          options={field.values.filter(v => typeof v !== "boolean").map(v => ({ value: v as string | number, label: String(v) }))} onChange={v => edit(field.path, v)} /> :
          field.type === "integer" || field.type === "number" ? <InputNumber aria-label={labels[field.path]} disabled={blocked} value={typeof value === "number" ? value : null}
            min={field.minimum ?? undefined} max={field.maximum ?? undefined} precision={field.type === "integer" ? 0 : undefined}
            onChange={v => { if (v !== null) edit(field.path, v); }} /> :
            <Input aria-label={labels[field.path]} disabled={blocked} value={String(value ?? "")} style={{ width: 145 }} onChange={e => edit(field.path, e.target.value)} />}
      </label>;
    }) }]} />}
    <Space wrap>
      <Button type="primary" disabled={blocked || !changed || conflict || invalidSize} onClick={async () => { if (draft && await onApply(cameraPatch(draft.values, fields), draft.revision)) setDraft(null); }}>Применить</Button>
      <Button disabled={!draft || disabled} onClick={() => setDraft(null)}>Сбросить</Button>
    </Space>
    {invalidSize && <Typography.Text type="danger">Укажите ширину и высоту кадра.</Typography.Text>}
    <Typography.Text type="secondary" style={{ fontSize: 11 }}>Применяется только изменение. Native и нулевые FPS/битрейт сохраняют Auto. Настройка записывается в камеру.</Typography.Text>
  </Space>;
}
