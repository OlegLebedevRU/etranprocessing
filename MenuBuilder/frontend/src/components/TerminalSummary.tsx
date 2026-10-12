import { useCallback, useEffect, useRef, useState } from "react";
import { Alert, Drawer, Spin, Tabs } from "antd";
import { getDeviceRaw, type DeviceRawResult } from "../api/devices";
import type { TerminalSettingsItem } from "../api/settings";
import DevicePassportTab from "../routes/devices/DevicePassportTab";
import DeviceTagsTab from "../routes/devices/DeviceTagsTab";

export default function TerminalSummary({ terminal, orgId, canEdit, onClose }: {
  terminal: TerminalSettingsItem; orgId: number; canEdit: boolean; onClose: () => void;
}) {
  const [snapshot, setSnapshot] = useState<DeviceRawResult | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(false);
  const generation = useRef(0);
  const refresh = useCallback(async () => {
    const current = generation.current;
    setLoading(true); setError(false);
    try {
      const result = await getDeviceRaw(orgId, terminal.device_id);
      if (current === generation.current) { setSnapshot(result); setError(!result); }
    } catch { if (current === generation.current) setError(true); }
    finally { if (current === generation.current) setLoading(false); }
  }, [orgId, terminal.device_id]);
  useEffect(() => { generation.current++; void refresh(); return () => { generation.current++; }; }, [refresh]);
  return <Drawer title={`Сводка терминала ${terminal.device_id}`} open onClose={onClose}
    width="min(880px, 100vw)" destroyOnHidden>
    <Spin spinning={loading}>
      {error && <Alert type="warning" showIcon title="Не удалось получить паспорт терминала" />}
      {snapshot && <Tabs items={[
        { key: "passport", label: "Паспорт", children: <DevicePassportTab deviceId={terminal.device_id}
          sn={terminal.sn} orgId={orgId} snapshot={snapshot} onRefresh={refresh} readOnly /> },
        { key: "tags", label: "Теги", children: <DeviceTagsTab deviceId={terminal.device_id}
          orgId={orgId} readOnly={!canEdit} tags={snapshot.device_tags || []} onTagsUpdated={() => void refresh()} /> },
      ]} />}
    </Spin>
  </Drawer>;
}
