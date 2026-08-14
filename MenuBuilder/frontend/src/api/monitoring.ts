import client from "./client";

export interface MonitoringTerminal {
  terminal_id: number;
  device_id: number;
  sn: string;
  org_id: number;
  is_active: boolean;
  slots: boolean[]; // 12 slots, 10-min each, last 2 hours
}

export interface MonitoringResponse {
  start: string;
  now: string;
  items: MonitoringTerminal[];
}

export const getMonitoring = () =>
  client.get<MonitoringResponse>("/monitoring");
