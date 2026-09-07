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
