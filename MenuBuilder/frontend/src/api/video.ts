import client from "./client";

export interface VideoSessionResponse {
  mountpoint_id: number;
  sn: string;
  janus_ws: string;
  session_ttl_sec: number;
  pin?: string;
}

export interface VideoStatusResponse {
  streaming: boolean;
  rtp_packets: number;
  bytes: number;
  idle_sec: number | null;
  sn: string;
}

export async function createVideoSession(
  deviceId: number
): Promise<VideoSessionResponse> {
  const { data } = await client.post<VideoSessionResponse>(
    `/v1/video/devices/${deviceId}/session`
  );
  return data;
}

export async function getVideoSessionStatus(
  deviceId: number
): Promise<VideoStatusResponse> {
  const { data } = await client.get<VideoStatusResponse>(
    `/v1/video/devices/${deviceId}/session/status`
  );
  return data;
}

export function getJanusWsUrl(wsPath: string): string {
  const proto = window.location.protocol === "https:" ? "wss:" : "ws:";
  const host = window.location.host;
  const path = wsPath.startsWith("/") ? wsPath : `/${wsPath}`;
  return `${proto}//${host}${path}`;
}

export interface ControlScreen {
  virtual_x: number;
  virtual_y: number;
  virtual_width: number;
  virtual_height: number;
}

export interface DisplaySource {
  id: string;
  name: string;
  desktop_id?: string;
  resolution?: string;
  width?: number;
  height?: number;
  x?: number;
  y?: number;
  is_primary?: boolean;
  primary?: boolean;
  policy?: "input" | "view" | "denied";
}

export interface CameraSource {
  id: string;
  camera_id?: string;
  name: string;
  available?: boolean;
}

export interface DeviceInventory {
  displays: DisplaySource[];
  cameras: CameraSource[];
}

export interface StreamPresenceInfo {
  state?: "stopped" | "starting" | "running" | "stopping" | "failed" | string;
  mode?: "desktop" | "usb-camera" | string;
  source_id?: string;
  stream_instance_id?: string;
  profile?: string;
  reason?: string;
  restart_count?: number;
}

export interface StreamStateResponse {
  sn: string;
  stream?: StreamPresenceInfo | null;
  ingress?: VideoStatusResponse | null;
}

export interface ControlAgentStatus {
  online: boolean;
  desktop_available: boolean;
  screen?: ControlScreen | null;
  last_seen_at?: string | null;
  stale?: boolean;
  inventory?: DeviceInventory | null;
  stream?: StreamPresenceInfo | null;
}

export interface ControlLeaseStatus {
  active: boolean;
  mine: boolean;
  lease_id?: string | null;
  scope?: "console" | "view" | "stream" | "input" | string | null;
  owner_role?: string | null;
  owner_masked?: string | null;
  owner_user_id?: string | null;
  expires_at?: string | null;
  stream_instance_id?: string | null;
  selected_desktop_id?: string | null;
}

export interface ControlStatus {
  agent: ControlAgentStatus;
  lease: ControlLeaseStatus;
}

export interface ControlLease {
  lease_id: string;
  expires_at: string;
  keepalive_sec: number;
  ws_path: string;
  scope?: string;
  owner_role?: string | null;
  owner_masked?: string | null;
  owner_user_id?: string | null;
  owner_session_id?: string | null;
}

export interface StreamStartResponse {
  stream_instance_id: string;
  result: "started" | "already_running" | "switched" | string;
  state?: string;
}

export interface StreamStopResponse {
  result: "stopped" | "already_stopped" | string;
}

export interface ClickResult {
  command_id?: string;
  client_ref?: string;
  result: "injected" | "nack" | "unconfirmed";
  code?: string;
  message?: string;
  latency_ms?: number;
}

export type ControlWsOutbound =
  | { type: "hello"; v: number }
  | {
      type: "presence";
      v?: number;
      agent?: string;
      status: "online" | "offline";
      desktop_available: boolean;
      screen?: ControlScreen | null;
      timestamp?: string;
    }
  | {
      type: "stream_state";
      state?: string;
      mode?: string;
      source_id?: string;
      stream_instance_id?: string;
      reason?: string;
    }
  | {
      type: "click_result";
      command_id?: string;
      lease_id?: string;
      sn?: string;
      client_ref?: string;
      result: "injected" | "nack";
      code?: string;
      message?: string;
      latency_ms?: number;
    }
  | {
      type: "error";
      code: "rate_limited" | "invalid_message" | "lease_inactive" | "payload_too_large" | string;
      message?: string;
    }
  | {
      type: "lease_revoked";
      reason?: string;
    };

export interface ControlWsKey {
  type: "key_event" | "key";
  kind: "down" | "up" | "press";
  vk: number;
  text?: string;
  client_ref?: string;
}

export type ControlWsInbound =
  | { type: "pointer_move"; x: number; y: number }
  | { type: "mouse_click"; x: number; y: number; button: "left"; client_ref?: string }
  | ControlWsKey
  | { type: "keepalive" }
  | { type: "release" };

export async function getControlStatus(deviceId: number): Promise<ControlStatus> {
  const { data } = await client.get<ControlStatus>(`/v1/video/devices/${deviceId}/control/status`);
  return data;
}

export async function acquireControlLease(
  deviceId: number,
  scope: "console" | "view" | "stream" | "input" = "input",
  ttlSec?: number,
  sessionId?: string
): Promise<ControlLease> {
  const { data } = await client.post<ControlLease>(`/v1/video/devices/${deviceId}/control/lease`, {
    scope,
    ttl_sec: ttlSec,
    session_id: sessionId,
  });
  return data;
}

export async function changeControlScope(
  deviceId: number,
  scope: "console" | "view" | "stream" | "input"
): Promise<ControlLease> {
  const { data } = await client.post<ControlLease>(`/v1/video/devices/${deviceId}/control/scope`, {
    scope,
  });
  return data;
}

export async function keepaliveControlLease(deviceId: number, leaseId: string): Promise<void> {
  await client.post(`/v1/video/devices/${deviceId}/control/keepalive`, { lease_id: leaseId });
}

export async function releaseControlLease(deviceId: number, leaseId: string): Promise<void> {
  await client.delete(`/v1/video/devices/${deviceId}/control/lease/${leaseId}`);
}

export async function getDeviceInventory(
  deviceId: number,
  refresh = false
): Promise<DeviceInventory> {
  const { data } = await client.get<DeviceInventory>(`/v1/video/devices/${deviceId}/inventory`, {
    params: { refresh: refresh ? 1 : 0 },
  });
  return data;
}

export async function startDeviceStream(
  deviceId: number,
  payload: {
    mode: "desktop" | "usb-camera";
    source_id: string;
    profile?: string;
    lease_id?: string;
  }
): Promise<StreamStartResponse> {
  const { data } = await client.post<StreamStartResponse>(
    `/v1/video/devices/${deviceId}/stream/start`,
    payload
  );
  return data;
}

export async function stopDeviceStream(
  deviceId: number,
  leaseId?: string
): Promise<StreamStopResponse> {
  const { data } = await client.post<StreamStopResponse>(
    `/v1/video/devices/${deviceId}/stream/stop`,
    leaseId ? { lease_id: leaseId } : {}
  );
  return data;
}

export async function getDeviceStreamState(deviceId: number): Promise<StreamStateResponse> {
  const { data } = await client.get<StreamStateResponse>(`/v1/video/devices/${deviceId}/stream/state`);
  return data;
}

export async function sendControlEvent(
  deviceId: number,
  event:
    | { lease_id: string; type: "pointer_move"; x: number; y: number }
    | { lease_id: string; type: "mouse_click"; x: number; y: number; button?: "left"; client_ref?: string }
    | { lease_id: string; type: "key" | "key_event"; kind: "down" | "up" | "press"; vk: number; text?: string; client_ref?: string }
): Promise<any> {
  const { data } = await client.post(`/v1/video/devices/${deviceId}/control/events`, event);
  return data;
}

export function getControlWsUrl(wsPath: string): string {
  const proto = window.location.protocol === "https:" ? "wss:" : "ws:";
  const host = window.location.host;
  const path = wsPath.startsWith("/") ? wsPath : `/${wsPath}`;
  return `${proto}//${host}${path}`;
}
