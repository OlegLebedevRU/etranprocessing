import client from "./client";

export interface VideoSessionResponse {
  mountpoint_id: number;
  sn: string;
  janus_ws: string;
  session_ttl_sec: number;
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

export interface ControlAgentStatus {
  online: boolean;
  desktop_available: boolean;
  screen?: ControlScreen | null;
  last_seen_at?: string | null;
  stale?: boolean;
}

export interface ControlLeaseStatus {
  active: boolean;
  mine: boolean;
  owner_user_id?: string | null;
  expires_at?: string | null;
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

export type ControlWsInbound =
  | { type: "pointer_move"; x: number; y: number }
  | { type: "mouse_click"; x: number; y: number; button: "left"; client_ref?: string }
  | { type: "keepalive" }
  | { type: "release" };

export async function getControlStatus(deviceId: number): Promise<ControlStatus> {
  const { data } = await client.get<ControlStatus>(`/v1/video/devices/${deviceId}/control/status`);
  return data;
}

export async function acquireControlLease(deviceId: number): Promise<ControlLease> {
  const { data } = await client.post<ControlLease>(`/v1/video/devices/${deviceId}/control/lease`);
  return data;
}

export async function keepaliveControlLease(deviceId: number, leaseId: string): Promise<void> {
  await client.post(`/v1/video/devices/${deviceId}/control/keepalive`, { lease_id: leaseId });
}

export async function releaseControlLease(deviceId: number, leaseId: string): Promise<void> {
  await client.delete(`/v1/video/devices/${deviceId}/control/lease/${leaseId}`);
}

export function getControlWsUrl(wsPath: string): string {
  const proto = window.location.protocol === "https:" ? "wss:" : "ws:";
  const host = window.location.host;
  const path = wsPath.startsWith("/") ? wsPath : `/${wsPath}`;
  return `${proto}//${host}${path}`;
}
