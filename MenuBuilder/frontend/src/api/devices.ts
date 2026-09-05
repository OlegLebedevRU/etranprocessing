import client from "./client";

export interface DeviceTagItem {
  tag: string;
  value: string;
  id?: number;
}

export interface DeviceViolationDetails {
  violation_type?: "SN_COLLISION" | "DEVICE_CLONE" | string;
  reason?: string;
  detected_at?: string;
  flapping_count?: number;
  host_switches?: number;
  cert_validities?: string[];
  conflicting_hosts?: string[];
}

export interface DeviceAuditEventItem {
  id: number;
  device_id: number;
  org_id?: number;
  event_type:
    | "PROVISIONED"
    | "SN_COLLISION"
    | "DEVICE_CLONE"
    | "BLOCKED"
    | "UNBLOCKED"
    | string;
  actor?: string | null;
  details?: Record<string, any> | null;
  created_at: string;
}

export function formatPeerHost(host?: string | null): string {
  if (!host) return "—";
  const str = String(host).trim();

  // Match Erlang IPv4-mapped IPv6 tuple format: "{0,0,0,0,0,65535,21485,65262}"
  const tupleMatch = str.match(
    /\{?\s*0\s*,\s*0\s*,\s*0\s*,\s*0\s*,\s*0\s*,\s*65535\s*,\s*(\d+)\s*,\s*(\d+)\s*\}?/
  );
  if (tupleMatch) {
    const w1 = parseInt(tupleMatch[1], 10);
    const w2 = parseInt(tupleMatch[2], 10);
    const b1 = (w1 >> 8) & 255;
    const b2 = w1 & 255;
    const b3 = (w2 >> 8) & 255;
    const b4 = w2 & 255;
    return `${b1}.${b2}.${b3}.${b4}`;
  }

  // Match 4-element Erlang tuple like "{192,168,1,1}"
  const ipv4TupleMatch = str.match(
    /\{?\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})\s*\}?/
  );
  if (ipv4TupleMatch) {
    return `${ipv4TupleMatch[1]}.${ipv4TupleMatch[2]}.${ipv4TupleMatch[3]}.${ipv4TupleMatch[4]}`;
  }

  return str;
}

export function formatConflictingHosts(hosts?: string[] | null): string[] {
  if (!hosts || !Array.isArray(hosts)) return [];
  return hosts.map((h) => formatPeerHost(h));
}

export interface DeviceConnectionDetailsRaw {
  user?: string;
  name?: string;
  conn_name?: string | null;
  connected_at?: number | string;
  peer_host?: string;
  peer_port?: number;
  peer_cert_subject?: string;
  peer_cert_validity?: string;
  protocol?: string;
  ssl?: boolean;
  ssl_cipher?: string;
  ssl_protocol?: string;
  bytes_received?: number;
  bytes_sent?: number;
  client_properties?: Record<string, any>;
}

export interface DeviceConnectionRaw {
  device_id: number;
  client_id?: string;
  connected_at?: string;
  checked_at?: string;
  last_checked_result: boolean;
  app_connect?: boolean | null;
  svc_connect?: boolean | null;
  is_app_available?: boolean | null;
  is_svc_available?: boolean | null;
  is_blocked?: boolean;
  violation_type?: "SN_COLLISION" | "DEVICE_CLONE" | string | null;
  violation_details?: DeviceViolationDetails | null;
  recent_audit_events?: DeviceAuditEventItem[] | null;
  details?: DeviceConnectionDetailsRaw | null;
}

export interface DeviceRawResult {
  id: number;
  device_id: number;
  sn: string;
  device_gauges?: any[];
  connection?: DeviceConnectionRaw | null;
  device_tags?: DeviceTagItem[];
}

export interface DeviceConnectionInfo {
  is_online?: boolean;
  connected_at?: string;
  last_connected_at?: string;
  checked_at?: string;
  last_checked_result?: boolean;
  is_blocked?: boolean;
  violation_type?: "SN_COLLISION" | "DEVICE_CLONE" | string | null;
  violation_details?: DeviceViolationDetails | null;
  recent_audit_events?: DeviceAuditEventItem[] | null;
  ip?: string;
  app_version?: string;
  details?: DeviceConnectionDetailsRaw | null;
  app_connect?: boolean | null;
  svc_connect?: boolean | null;
  is_app_available?: boolean | null;
  is_svc_available?: boolean | null;
}

export interface DeviceGaugeItem {
  type: string;
  updated_at: string;
  gauges?: Record<string, any[]>;
}

export interface DeviceItem {
  id?: number;
  device_id: number;
  sn: string;
  created_at?: string;
  is_deleted?: boolean;
  connection?: (DeviceConnectionInfo & {
    is_blocked?: boolean;
    violation_type?: string | null;
    violation_details?: DeviceViolationDetails | null;
    recent_audit_events?: DeviceAuditEventItem[] | null;
  }) | null;
  device_tags?: DeviceTagItem[];
  device_gauges?: DeviceGaugeItem[];
}

export interface DeviceListItem {
  id?: number;
  device_id: number;
  sn: string;
  status: "online" | "offline" | "blocked" | "unknown";
  is_blocked?: boolean;
  violation_type?: "SN_COLLISION" | "DEVICE_CLONE" | string | null;
  violation_details?: DeviceViolationDetails | null;
  recent_audit_events?: DeviceAuditEventItem[] | null;
  ageSeconds?: number;
  app?: string;
  sys?: string;
  description?: string;
  cmds?: string;
  tags: DeviceTagItem[];
  connection?: DeviceConnectionInfo | null;
}

export interface DeviceStats {
  total: number;
  online: number;
  offline: number;
  blocked: number;
}

export interface DeviceListRawResponse {
  items: DeviceItem[];
  total: number;
  page: number;
  size: number;
  pages: number;
  stats: DeviceStats;
}

export interface DeviceListResponse {
  items: DeviceListItem[];
  total: number;
  page: number;
  size: number;
  pages: number;
  stats: DeviceStats;
}

export interface GetDevicesParams {
  orgId?: number;
  org_id?: number;
  page?: number;
  size?: number;
  q?: string;
  status?: string;
  deviceId?: number;
  device_id?: number;
  sortBy?: string;
  sort_by?: string;
  sortOrder?: "asc" | "desc";
  sort_order?: "asc" | "desc";
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
  let sys = "";

  tags.forEach((val) => {
    if (val.tag === "name" || val.tag === "description") {
      description = description ? `${description} ${val.value}` : val.value;
    } else if (val.tag === "cmd") {
      cmds = cmds ? `${cmds} ${val.value}` : val.value;
    } else if (val.tag === "app") {
      app = val.value;
    } else if (val.tag === "sys") {
      sys = val.value.toLowerCase().trim();
    }
  });

  const isBlocked = Boolean(device.connection?.is_blocked);
  const isOnline = Boolean(
    !isBlocked &&
      (device.connection?.last_checked_result || device.connection?.is_online)
  );

  let status: "online" | "offline" | "blocked" | "unknown";
  if (isBlocked) {
    status = "blocked";
  } else if (isOnline) {
    status = "online";
  } else {
    status = "offline";
  }

  let ageSeconds: number | undefined;
  const lastConnected =
    device.connection?.connected_at || device.connection?.last_connected_at;
  if (lastConnected) {
    const connTime = new Date(lastConnected).getTime();
    if (!isNaN(connTime)) {
      ageSeconds = Math.max(0, Math.floor((Date.now() - connTime) / 1000));
    }
  }

  return {
    id: device.id,
    device_id: device.device_id,
    sn: device.sn,
    status,
    is_blocked: isBlocked,
    violation_type: device.connection?.violation_type || null,
    violation_details: device.connection?.violation_details || null,
    recent_audit_events: device.connection?.recent_audit_events || null,
    ageSeconds,
    app,
    sys,
    description,
    cmds,
    tags,
    connection: device.connection,
  };
}

export async function getDevices(
  orgIdOrParams: number | GetDevicesParams,
  options?: Omit<GetDevicesParams, "orgId" | "org_id">
): Promise<DeviceListResponse> {
  const queryParams: Record<string, any> = {};

  if (typeof orgIdOrParams === "number") {
    queryParams.org_id = orgIdOrParams;
    if (options) {
      if (options.page !== undefined) queryParams.page = options.page;
      if (options.size !== undefined) queryParams.size = options.size;
      if (options.q !== undefined && options.q.trim() !== "") queryParams.q = options.q.trim();
      if (options.status !== undefined && options.status !== "all") queryParams.status = options.status;
      if (options.deviceId !== undefined) queryParams.device_id = options.deviceId;
      if (options.device_id !== undefined) queryParams.device_id = options.device_id;
      if (options.sortBy !== undefined) queryParams.sort_by = options.sortBy;
      if (options.sort_by !== undefined) queryParams.sort_by = options.sort_by;
      if (options.sortOrder !== undefined) queryParams.sort_order = options.sortOrder;
      if (options.sort_order !== undefined) queryParams.sort_order = options.sort_order;
    }
  } else {
    const p = orgIdOrParams;
    const orgId = p.org_id ?? p.orgId;
    if (orgId !== undefined) queryParams.org_id = orgId;
    if (p.page !== undefined) queryParams.page = p.page;
    if (p.size !== undefined) queryParams.size = p.size;
    if (p.q !== undefined && p.q.trim() !== "") queryParams.q = p.q.trim();
    if (p.status !== undefined && p.status !== "all") queryParams.status = p.status;
    if (p.deviceId !== undefined) queryParams.device_id = p.deviceId;
    if (p.device_id !== undefined) queryParams.device_id = p.device_id;
    if (p.sortBy !== undefined) queryParams.sort_by = p.sortBy;
    if (p.sort_by !== undefined) queryParams.sort_by = p.sort_by;
    if (p.sortOrder !== undefined) queryParams.sort_order = p.sortOrder;
    if (p.sort_order !== undefined) queryParams.sort_order = p.sort_order;
  }

  const { data } = await client.get<DeviceListRawResponse | DeviceItem[]>("/internal/v1/devices/", {
    params: queryParams,
  });

  if (Array.isArray(data)) {
    const items = data.map(mapDeviceToListItem);
    return {
      items,
      total: items.length,
      page: 1,
      size: items.length || 20,
      pages: 1,
      stats: {
        total: items.length,
        online: items.filter((i) => i.status === "online").length,
        offline: items.filter((i) => i.status === "offline").length,
        blocked: items.filter((i) => i.status === "blocked" || i.is_blocked).length,
      },
    };
  }

  return {
    items: (data?.items || []).map(mapDeviceToListItem),
    total: data?.total ?? 0,
    page: data?.page ?? 1,
    size: data?.size ?? (queryParams.size || 20),
    pages: data?.pages ?? 0,
    stats: data?.stats ?? { total: 0, online: 0, offline: 0, blocked: 0 },
  };
}

export async function getDeviceDetail(
  orgId: number,
  deviceId: number
): Promise<DeviceListItem | null> {
  try {
    const { data } = await client.get<DeviceItem>(`/internal/v1/devices/${deviceId}`, {
      params: { org_id: orgId },
    });
    return data ? mapDeviceToListItem(data) : null;
  } catch (err: any) {
    if (err?.response?.status === 404) {
      return null;
    }
    try {
      const res = await getDevices(orgId, { deviceId, size: 1 });
      const found = res.items.find((d) => d.device_id === deviceId);
      if (found) return found;
    } catch {
      // ignore fallback error
    }
    throw err;
  }
}

export async function getDeviceRaw(
  orgId: number,
  deviceId: number
): Promise<DeviceRawResult | null> {
  try {
    const { data } = await client.get<DeviceRawResult>(`/internal/v1/devices/${deviceId}`, {
      params: { org_id: orgId },
    });
    return data;
  } catch (err: any) {
    if (err?.response?.status === 404) {
      return null;
    }
    try {
      const { data } = await client.get<DeviceListRawResponse | DeviceRawResult[]>("/internal/v1/devices/", {
        params: { org_id: orgId, device_id: deviceId },
      });
      if (Array.isArray(data)) {
        return data.length > 0 ? data[0] : null;
      }
      if (data && Array.isArray((data as any).items)) {
        return (data as any).items.length > 0 ? (data as any).items[0] : null;
      }
    } catch {
      // ignore fallback error
    }
    throw err;
  }
}

export async function triggerDeviceProvisioning(deviceId: number): Promise<any> {
  const { data } = await client.post(`/admin/terminals/${deviceId}/iot-provision`);
  return data;
}

export async function updateDeviceTag(
  orgId: number,
  deviceId: number,
  tag: { tag: string; value: string }
): Promise<any> {
  const { data } = await client.put(`/internal/v1/devices/${deviceId}`, tag, {
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
  const { data } = await client.get<TaskListResponse>("/internal/v1/device-tasks/", {
    params: { org_id: orgId, device_id: deviceId, page, size },
  });
  return data;
}

export async function createDeviceTask(
  orgId: number,
  payload: TaskCreateInput
): Promise<TaskItem> {
  const { data } = await client.post<TaskItem>("/internal/v1/device-tasks/", payload, {
    params: { org_id: orgId },
  });
  return data;
}

export async function getTaskDetail(orgId: number, taskId: string): Promise<TaskItem> {
  const { data } = await client.get<TaskItem>(`/internal/v1/device-tasks/${taskId}`, {
    params: { org_id: orgId },
  });
  return data;
}

export async function deleteDeviceTask(orgId: number, taskId: string): Promise<any> {
  const { data } = await client.delete(`/internal/v1/device-tasks/${taskId}`, {
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
  const { data } = await client.get<EventListResponse>("/internal/v1/device-events/", {
    params: { org_id: orgId, device_id: deviceId, page, size },
  });
  return data;
}

export function getDiagnosticsWsUrl(sn: string, orgId: number): string {
  const proto = window.location.protocol === "https:" ? "wss:" : "ws:";
  const host = window.location.host;
  return `${proto}//${host}/api/internal/v1/diagnostics/ws/devices/${encodeURIComponent(sn)}?org_id=${orgId}`;
}
