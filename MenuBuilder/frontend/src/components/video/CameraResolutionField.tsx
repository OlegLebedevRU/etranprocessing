import { useState } from "react";
import { InputNumber, Select, Space, Typography } from "antd";
import type { CameraField } from "../../api/cameras";

export function CameraResolutionField({ field, value, disabled, onChange }: {
  field: CameraField; value: string; disabled: boolean; onChange: (size: string) => void;
}) {
  const [custom, setCustom] = useState(false);
  const match = /^(\d+)x(\d+)$/.exec(value);
  const [width, setWidth] = useState<number | null>(match ? Number(match[1]) : null);
  const [height, setHeight] = useState<number | null>(match ? Number(match[2]) : null);
  // Presets are requests, not a whitelist or promises of native sensor modes.
  const sizes = field.values.length ? field.values.filter((v): v is string => typeof v === "string") : ["", "1920x1080", "2048x1536", "2592x1944"];
  const choices = [...new Set([...sizes, value])];
  return <Space orientation="vertical" size={6} style={{ width: "100%" }}>
    <Select virtual={false} aria-label="Разрешение камеры" style={{ width: "100%" }} disabled={disabled} value={custom ? "custom" : value}
      options={[...choices.map(size => {
        const parsed = /^(\d+)x(\d+)$/.exec(size);
        const mp = parsed ? (Number(parsed[1]) * Number(parsed[2]) / 1e6).toFixed(1).replace(/\.0$/, "") : "";
        return { value: size, label: size === "" ? "Native / Auto" : parsed ? `${mp} MP · ${parsed[1]} × ${parsed[2]}` : size };
      }), { value: "custom", label: "Свой размер…" }]}
      onChange={next => {
        setCustom(next === "custom");
        if (next !== "custom") onChange(next);
        else {
          const current = /^(\d+)x(\d+)$/.exec(value);
          setWidth(current ? Number(current[1]) : null); setHeight(current ? Number(current[2]) : null);
          if (!current) onChange("x");
        }
      }} />
    {custom && <Space.Compact style={{ width: "100%" }}>
      <InputNumber aria-label="Ширина кадра" placeholder="Ширина" min={1} precision={0} disabled={disabled} value={width}
        onChange={w => { setWidth(w); onChange(`${w ?? ""}x${height ?? ""}`); }} />
      <InputNumber aria-label="Высота кадра" placeholder="Высота" min={1} precision={0} disabled={disabled} value={height}
        onChange={h => { setHeight(h); onChange(`${width ?? ""}x${h ?? ""}`); }} />
    </Space.Compact>}
    <Typography.Text type="secondary" style={{ fontSize: 11 }}>Пресет — запрос камере. Фактический размер показан в состоянии потока.</Typography.Text>
  </Space>;
}
