import client from "./client";
import type { AxiosRequestConfig } from "axios";
import type { browserVideoCodecs } from "../utils/browserVideoCodecs";

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
  actual_track?: { codec: "h264" | "h265"; codec_profile?: string; width: number; height: number; fps: number;
    level_id?: number; bit_depth?: number; chroma?: string } | null;
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
  ircut?: IrcutReport;
  media_observations?: { physical_camera_mac?: string }[];
}
export interface IrcutReport {
  ircut_revision: string; methods?: number[]; camera_mac: string; boot_id: string; revision: string;
  session_id: string; state: string; reason: string; phase: number; stage: number; frame: number;
  configured_pair?: number[] | null; parked: boolean; configuration_restored: boolean; recovery_required: boolean;
  optical?: { reversible: boolean; delta: number; noise: number; threshold: number };
}
export interface IrcutCommand {
  operation_id: string; action: "inspect" | "start" | "status" | "cancel" | "recover";
  camera_mac: string; session_id?: string; boot_id?: string; expected_revision?: string; allow_disruptive?: boolean;
}
export async function commandCameraIrcut(device: number, body: IrcutCommand): Promise<{ id: string }> {
  return (await client.post(`/cameras/${device}/ircut`, body, { _retry: true } as AxiosRequestConfig)).data;
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
    ircut?: IrcutReport;
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

export interface CameraPreparation {
  source_id: "cam:ip000001"; physical_camera_mac: string; discovery_generation: number;
  codec_policy: "auto" | "h264" | "h265";
  profile_id: "low" | "default";
  browser_codecs: ReturnType<typeof browserVideoCodecs>;
}
export interface CameraAdmissionTicket { ticket: string }
export async function prepareCamera(device: number, body: CameraPreparation, signal: AbortSignal): Promise<CameraAdmissionTicket> {
  return (await client.post(`/cameras/${device}/admission/prepare`, body,
    { signal, timeout: 8000, _retry: true } as AxiosRequestConfig)).data;
}
export async function cameraEnrollment(device: number, body: CameraAdmissionTicket, signal: AbortSignal): Promise<{ enrolled: boolean }> {
  return (await client.post(`/cameras/${device}/admission/enrollment`, body,
    { signal, timeout: 5000, _retry: true } as AxiosRequestConfig)).data;
}
export async function cameraReadiness(device: number, body: CameraAdmissionTicket, signal: AbortSignal): Promise<CameraAdmissionTicket> {
  return (await client.post(`/cameras/${device}/admission/readiness`, body,
    { signal, timeout: 6500, _retry: true } as AxiosRequestConfig)).data;
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
