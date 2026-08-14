import client from "./client";

export interface TerminalBinding {
  id: number;
  device_id: number;
  menu_variant_id: number;
  menu_variant_name: string | null;
  created_at: string;
}

export interface TerminalBindingCreate {
  device_id: number;
  menu_variant_id: number;
}

export const getTerminalBindings = () =>
  client.get<TerminalBinding[]>("/terminal-bindings");

export const createOrUpdateBinding = (data: TerminalBindingCreate) =>
  client.post<TerminalBinding>("/terminal-bindings", data);

export const deleteBinding = (id: number) =>
  client.delete(`/terminal-bindings/${id}`);
