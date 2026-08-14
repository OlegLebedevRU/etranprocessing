import client from "./client";

export interface Group {
  id: number;
  menu_variant_id: number;
  org_id: number;
  number: number;
  name: string;
  parent_id: number | null;
}

export interface GroupCreate {
  menu_variant_id: number;
  org_id: number;
  name: string;
  parent_id?: number | null;
  number?: number;
}

export interface GroupUpdate {
  name?: string;
  parent_id?: number | null;
  number?: number;
}

export const getGroups = (menuVariantId: number) =>
  client.get<Group[]>("/groups", { params: { menu_variant_id: menuVariantId } });

export const getGroup = (id: number) =>
  client.get<Group>(`/groups/${id}`);

export const createGroup = (data: GroupCreate) =>
  client.post<Group>("/groups", data);

export const updateGroup = (id: number, data: GroupUpdate) =>
  client.put<Group>(`/groups/${id}`, data);

export const deleteGroup = (id: number) =>
  client.delete(`/groups/${id}`);
