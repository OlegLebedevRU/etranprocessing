import client from "./client";

export interface MonitoringTerminal {
  terminal_id: number;
  device_id: number;
  sn: string;
  org_id: number;
  is_active: boolean;
  slots: boolean[];
  lastnumconn: number;
  validator_state: string;
  validator_type: number;
  cash_amount: number;
  printer_state: string;
  printer_fr: number;
  printer_check_counter: number;
  soft_version: string;
}

export interface MonitoringResponse {
  start: string;
  now: string;
  items: MonitoringTerminal[];
}

export const getMonitoring = () =>
  client.get<MonitoringResponse>("/monitoring");
