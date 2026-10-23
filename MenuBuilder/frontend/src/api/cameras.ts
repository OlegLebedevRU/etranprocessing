import client from "./client";
import type { AxiosRequestConfig } from "axios";

export type CameraValue = string | number | boolean;
export type CameraPatch = Record<string, Record<string, CameraValue>>;
export interface CameraField {
  path: string; type: "boolean" | "integer" | "number" | "string";
  read: boolean; write: boolean; disruptive: boolean; reason: string | null;
  minimum: number | null; maximum: number | null; values: CameraValue[];
}

export interface CameraProbe {
  status?: string; observed_at?: string; enabled?: boolean | null; state?: string;
  openipc_version?: string; majestic_version?: string;
  uptime_sec?: number; majestic_uptime_sec?: number;
  clock?: { status?: string; offset_sec?: number };
  metrics?: Record<string, number>;
}
export interface CameraState {
  camera_id: string; status?: string; stale?: boolean; revision?: string | null;
  camera_contract_revision?: string;
  auth_source?: "native" | "custom" | "none";
  has_credentials?: boolean;
  discovery?: { generation: number; state: string; physical_port: 2; ethernet_mode: "switch" | "ports";
    candidates: { candidate_id: string; mac: string; ip: string | null; native_api: string; auth: string; port_verified: boolean }[] };
  config?: { [section: string]: Record<string, CameraValue | undefined> | undefined;
    video0?: { codec?: string; size?: string; fps?: number; bitrate?: number }; nightMode?: { lightMonitor?: boolean } } | null;
  identity?: CameraProbe | null; power?: CameraProbe | null;
  telemetry?: CameraProbe & { health?: CameraProbe | null };
  capabilities?: { methods?: number[]; fields?: CameraField[];
    illumination?: { white: string; ir: string; global_enable: string; pwm: string };
    quality?: { presets?: { id: "medium" | "hd"; available: boolean }[] };
    day_night?: { write?: boolean; modes?: string[] }; power?: { available?: boolean; actions?: string[] } };
}
export interface CameraSnapshot {
  snapshot_id: string; operation_id?: string | null; observed_at?: string;
  freshness_max_age_sec?: number; cameras: CameraState[];
}
export interface CameraObservation { device_id: number; snapshot: CameraSnapshot | null; lease_active: boolean | null; terminal_online: boolean }
export interface CameraCommand {
  operation_id: string; action: "refresh" | "quality" | "day_night" | "power" | "patch" | "discover" | "cancel_discovery" | "bind" | "unbind" | "clear_credentials";
  value?: string; expected_revision?: string; expected_power_on?: boolean; allow_disruptive?: boolean;
  patch?: CameraPatch;
  target_operation_id?: string; candidate_mac?: string; discovery_generation?: number;
}
export interface CameraResult {
  status_code: number; result: { status?: string; error?: string; snapshot_id?: string; operation_id?: string;
    applied_fields?: string[]; field_errors?: { field?: string; reason?: string }[] } | string;
}
export interface CameraTask { id: string; status: number; results?: CameraResult[] }

export interface CameraCredentialMetadata {
  vault_ready: boolean; recipient_ready: boolean; has_active: boolean;
  active_operation_id?: string | null; pending_operation_id?: string | null;
  last_operation_id?: string | null; last_state?: string | null;
  latest_operation_id?: string | null; latest_state?: string | null;
}
export interface CameraCredentialChange {
  action: "replace" | "redeliver" | "clear"; operation_id: string; expected_revision: string;
  expires_at_ms: number; allow_disruptive: true;
  username?: string; password?: string; source_operation_id?: string;
}
export async function getCameraCredentialMetadata(device: number, signal?: AbortSignal): Promise<CameraCredentialMetadata> {
  return (await client.get(`/cameras/${device}/credentials`, { signal })).data;
}
export async function changeCameraCredentials(device: number, body: CameraCredentialChange): Promise<{ id: string; state: string }> {
  const config: AxiosRequestConfig & { _retry: boolean } = { _retry: true };
  return (await client.post(`/cameras/${device}/credentials`, body, config)).data;
}

export async function getCamera(device: number, signal?: AbortSignal): Promise<CameraObservation> {
  return (await client.get(`/cameras/${device}`, { signal })).data;
}
export async function commandCamera(device: number, body: CameraCommand): Promise<{ id: string }> {
  // The shared auth interceptor otherwise replays POST after a 401. A camera
  // command may already exist upstream: admission must remain a single attempt.
  const config: AxiosRequestConfig & { _retry: boolean } = { _retry: true };
  return (await client.post(`/cameras/${device}/commands`, body, config)).data;
}
export async function getCameraCommand(device: number, id: string, signal?: AbortSignal): Promise<CameraTask> {
  return (await client.get(`/cameras/${device}/commands/${id}`, { signal })).data;
}
