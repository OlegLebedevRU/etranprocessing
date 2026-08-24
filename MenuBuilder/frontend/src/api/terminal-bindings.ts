import client from "./client";

export interface TerminalInfo {
  terminal_id: number;
  device_id: number;
  sn: string;
  org_id: number;
  is_active: boolean;
  binding_id: number | null;
  menu_variant_id: number | null;
  menu_variant_name: string | null;
  loaded_version?: number | null;
  loaded_at?: string | null;
  current_version?: number | null;
  is_latest?: boolean;
}

export interface TerminalsResponse {
  total: number;
  page: number;
  page_size: number;
  items: TerminalInfo[];
}

export interface TerminalBindingCreate {
  device_id: number;
  menu_variant_id: number;
}

export const getTerminals = (page = 1, pageSize = 50, search?: string) =>
  client.get<TerminalsResponse>("/terminals", {
    params: {
      page,
      page_size: pageSize,
      search: search || undefined,
    },
  });

export const createOrUpdateBinding = (data: TerminalBindingCreate) =>
  client.post("/bindings", data);

export const deleteBinding = (bindingId: number) =>
  client.delete(`/bindings/${bindingId}`);
