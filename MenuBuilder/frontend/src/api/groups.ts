import client from "./client";

export interface Group {
  id: number;
  org_id: number;
  number: number;
  name: string;
  parent_id: number | null;
}

export interface GroupCreate {
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

export const getGroups = (orgId?: number) =>
  client.get<Group[]>("/groups", { params: orgId ? { org_id: orgId } : {} });

export const getGroup = (id: number) =>
  client.get<Group>(`/groups/${id}`);

export const createGroup = (data: GroupCreate) =>
  client.post<Group>("/groups", data);

export const updateGroup = (id: number, data: GroupUpdate) =>
  client.put<Group>(`/groups/${id}`, data);

export const deleteGroup = (id: number) =>
  client.delete(`/groups/${id}`);
