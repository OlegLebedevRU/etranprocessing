import client from "./client";

export interface DeviceTagItem {
  tag: string;
  value: string;
  id?: number;
}

export interface DeviceConnectionInfo {
  is_online?: boolean;
  last_connected_at?: string;
  last_checked_result?: boolean;
  ip?: string;
  app_version?: string;
}

export interface DeviceGaugeItem {
  type: string;
  updated_at: string;
  gauges?: Record<string, any[]>;
}

export interface DeviceItem {
  device_id: number;
  sn: string;
  created_at?: string;
  is_deleted?: boolean;
  connection?: DeviceConnectionInfo | null;
  device_tags?: DeviceTagItem[];
  device_gauges?: DeviceGaugeItem[];
}

export interface DeviceListItem {
  device_id: number;
  sn: string;
  status: "online" | "offline" | "unknown";
  ageSeconds?: number;
  app?: string;
  description?: string;
  cmds?: string;
  tags: DeviceTagItem[];
  connection?: DeviceConnectionInfo | null;
}

export interface TaskResultItem {
  id: number;
  ext_id?: number;
  status_code: number;
  result?: Record<string, any> | any[] | null;
}

export interface TaskItem {
  id: string;
  ext_task_id?: string;
  device_id: number;
  org_id?: number;
  method_code: number;
  status: number; // 0: READY, 1: PENDING, 2: LOCK, 3: DONE, 4: EXPIRED, 5: DELETED, 6: FAILED, 7: UNDEFINED
  priority?: number;
  ttl?: number;
  ttl_minutes?: number;
  payload?: {
    dt?: any[];
    [key: string]: any;
  } | null;
  params?: Record<string, any>;
  created_at: number | string;
  pending_at?: number | string | null;
  locked_at?: number | string | null;
  deleted_at?: number | string | null;
  updated_at?: string;
  results?: TaskResultItem[] | any;
}

export interface TaskListResponse {
  items: TaskItem[];
  total: number;
  page: number;
  size: number;
  pages?: number;
}

export interface TaskCreateInput {
  ext_task_id?: string;
  device_id: number;
  method_code: number;
  priority?: number;
  ttl?: number;
  ttl_minutes?: number;
  payload?: {
    dt?: any[];
    [key: string]: any;
  };
  params?: Record<string, any>;
}

export interface TaskCreateResponse {
  id: string;
  created_at: number;
}

export interface DeviceEventItem {
  id?: string | number;
  dev_event_id?: string | number;
  device_id: number;
  org_id?: number;
  event_type_code: number;
  payload?: any;
  created_at: string;
}

export interface EventListResponse {
  items: DeviceEventItem[];
  total: number;
  page: number;
  size: number;
  pages?: number;
}

export function mapDeviceToListItem(device: DeviceItem): DeviceListItem {
  const tags = device.device_tags || [];
  let description = "";
  let cmds = "";
  let app = "";

  tags.forEach((val) => {
    if (val.tag === "name" || val.tag === "description") {
      description = description ? `${description} ${val.value}` : val.value;
    } else if (val.tag === "cmd") {
      cmds = cmds ? `${cmds} ${val.value}` : val.value;
    } else if (val.tag === "app") {
      app = val.value;
    }
  });

  const isOnline = Boolean(device.connection?.last_checked_result || device.connection?.is_online);
  const status: "online" | "offline" | "unknown" = isOnline ? "online" : "offline";

  let ageSeconds: number | undefined;
  if (device.connection?.last_connected_at) {
    const connTime = new Date(device.connection.last_connected_at).getTime();
    ageSeconds = Math.max(0, Math.floor((Date.now() - connTime) / 1000));
  }

  return {
    device_id: device.device_id,
    sn: device.sn,
    status,
    ageSeconds,
    app,
    description,
    cmds,
    tags,
    connection: device.connection,
  };
}

export async function getDevices(orgId: number, deviceId?: number): Promise<DeviceListItem[]> {
  const params: Record<string, any> = { org_id: orgId };
  if (deviceId !== undefined) {
    params.device_id = deviceId;
  }
  const { data } = await client.get<DeviceItem[]>("/v1/devices/", { params });
  return (data || []).map(mapDeviceToListItem);
}

export async function updateDeviceTag(
  orgId: number,
  deviceId: number,
  tag: { tag: string; value: string }
): Promise<any> {
  const { data } = await client.put(`/v1/devices/${deviceId}`, tag, {
    params: { org_id: orgId },
  });
  return data;
}

export async function getDeviceTasks(
  orgId: number,
  deviceId: number,
  page: number = 1,
  size: number = 50
): Promise<TaskListResponse> {
  const { data } = await client.get<TaskListResponse>("/v1/device-tasks/", {
    params: { org_id: orgId, device_id: deviceId, page, size },
  });
  return data;
}

export async function createDeviceTask(
  orgId: number,
  payload: TaskCreateInput
): Promise<TaskItem> {
  const { data } = await client.post<TaskItem>("/v1/device-tasks/", payload, {
    params: { org_id: orgId },
  });
  return data;
}

export async function getTaskDetail(orgId: number, taskId: string): Promise<TaskItem> {
  const { data } = await client.get<TaskItem>(`/v1/device-tasks/${taskId}`, {
    params: { org_id: orgId },
  });
  return data;
}

export async function deleteDeviceTask(orgId: number, taskId: string): Promise<any> {
  const { data } = await client.delete(`/v1/device-tasks/${taskId}`, {
    params: { org_id: orgId },
  });
  return data;
}

export async function getDeviceEvents(
  orgId: number,
  deviceId: number,
  page: number = 1,
  size: number = 50
): Promise<EventListResponse> {
  const { data } = await client.get<EventListResponse>("/v1/device-events/", {
    params: { org_id: orgId, device_id: deviceId, page, size },
  });
  return data;
}

export function getDiagnosticsWsUrl(sn: string, orgId: number): string {
  const proto = window.location.protocol === "https:" ? "wss:" : "ws:";
  const host = window.location.host;
  return `${proto}//${host}/api/v1/diagnostics/ws/devices/${encodeURIComponent(sn)}?org_id=${orgId}`;
}
