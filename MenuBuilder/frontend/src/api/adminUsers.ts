import client from "./client";

export interface UserItem {
  id: number;
  username: string;
  org_id?: number | null;
  org_name?: string | null;
  role_id: number;
  role: string;
  is_active: boolean;
  is_superuser: boolean;
  full_name?: string | null;
  created_at?: string | null;
  updated_at?: string | null;
}

export interface UserListResponse {
  total: number;
  items: UserItem[];
}

export interface CreateUserData {
  username: string;
  password: string;
  org_id?: number | null;
  role_id?: number;
  role?: string;
  is_active?: boolean;
  is_superuser?: boolean;
  full_name?: string | null;
}

export interface UpdateUserData {
  password?: string | null;
  org_id?: number | null;
  role_id?: number | null;
  role?: string | null;
  is_active?: boolean | null;
  is_superuser?: boolean | null;
  full_name?: string | null;
}

export interface UserSessionItem {
  id: number;
  user_id: number;
  ip_address?: string | null;
  user_agent?: string | null;
  expires_at: string;
  created_at: string;
  last_used_at?: string | null;
  is_revoked: boolean;
}

export async function fetchUsers(params?: {
  org_id?: number;
  role_id?: number;
  role?: string;
  search?: string;
  is_active?: boolean;
  limit?: number;
  offset?: number;
}): Promise<UserListResponse> {
  const { data } = await client.get<UserListResponse>("/admin/users", { params });
  return data;
}

export async function createUser(payload: CreateUserData): Promise<UserItem> {
  const { data } = await client.post<UserItem>("/admin/users", payload);
  return data;
}

export async function updateUser(id: number, payload: UpdateUserData): Promise<UserItem> {
  const { data } = await client.put<UserItem>(`/admin/users/${id}`, payload);
  return data;
}

export async function deleteUser(id: number): Promise<{ ok: boolean; message: string }> {
  const { data } = await client.delete<{ ok: boolean; message: string }>(`/admin/users/${id}`);
  return data;
}

export async function fetchUserSessions(userId: number): Promise<UserSessionItem[]> {
  const { data } = await client.get<UserSessionItem[]>(`/admin/users/${userId}/sessions`);
  return data;
}

export async function revokeAllUserSessions(userId: number): Promise<{ ok: boolean; revoked_count: number }> {
  const { data } = await client.post<{ ok: boolean; revoked_count: number }>(`/admin/users/${userId}/sessions/revoke`);
  return data;
}

export async function revokeUserSession(userId: number, sessionId: number): Promise<{ ok: boolean; message: string }> {
  const { data } = await client.post<{ ok: boolean; message: string }>(`/admin/users/${userId}/sessions/${sessionId}/revoke`);
  return data;
}
