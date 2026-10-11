import client from "./client";
import type { AxiosRequestConfig } from "axios";

export interface CameraProbe {
  status?: string; observed_at?: string; enabled?: boolean | null; state?: string;
  openipc_version?: string; majestic_version?: string;
  uptime_sec?: number; majestic_uptime_sec?: number;
  clock?: { status?: string; offset_sec?: number };
  metrics?: Record<string, number>;
}
export interface CameraState {
  camera_id: string; status?: string; stale?: boolean; revision?: string | null;
  config?: { video0?: { size?: string; fps?: number; bitrate?: number }; nightMode?: { lightMonitor?: boolean } } | null;
  identity?: CameraProbe | null; power?: CameraProbe | null;
  telemetry?: CameraProbe & { health?: CameraProbe | null };
  capabilities?: { quality?: { presets?: { id: "medium" | "hd"; available: boolean }[] };
    day_night?: { write?: boolean; modes?: string[] }; power?: { available?: boolean; actions?: string[] } };
}
export interface CameraSnapshot {
  snapshot_id: string; operation_id?: string | null; observed_at?: string;
  freshness_max_age_sec?: number; cameras: CameraState[];
}
export interface CameraObservation { device_id: number; snapshot: CameraSnapshot | null; lease_active: boolean | null; terminal_online: boolean }
export interface CameraCommand {
  operation_id: string; action: "refresh" | "quality" | "day_night" | "power";
  value?: string; expected_revision?: string; expected_power_on?: boolean; allow_disruptive?: boolean;
}
export interface CameraResult {
  status_code: number; result: { status?: string; error?: string; snapshot_id?: string;
    applied_fields?: string[]; field_errors?: { field?: string; reason?: string }[] } | string;
}
export interface CameraTask { id: string; status: number; results?: CameraResult[] }

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
