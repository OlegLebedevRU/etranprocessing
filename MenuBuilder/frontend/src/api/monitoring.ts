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
  last_payment_at: string | null;
  license_expires_at: string | null;
  cert_serial: string | null;
  cert_not_valid_after: string | null;
  address?: string | null;
  note?: string | null;
  terminal_type_id?: number;
  terminal_type_name?: string | null;
  created_at?: string | null;
}

export interface MonitoringResponse {
  start: string;
  now: string;
  total: number;
  page: number;
  page_size: number;
  items: MonitoringTerminal[];
}

export const getMonitoring = (page = 1, pageSize = 20, search?: string) =>
  client.get<MonitoringResponse>("/monitoring", {
    params: {
      page,
      page_size: pageSize,
      search: search || undefined,
    },
  });
