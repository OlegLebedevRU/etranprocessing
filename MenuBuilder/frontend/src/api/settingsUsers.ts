import client from "./client";

export interface TenantUserItem {
  id: number;
  username: string;
  full_name: string | null;
  role_id: number;
  role: string;
  is_active: boolean;
  permissions: string[];
  created_at: string | null;
}

export interface CreateTenantUserPayload {
  username: string;
  password: string;
  full_name?: string | null;
  permissions: string[];
}

export interface UpdateTenantUserPayload {
  full_name?: string | null;
  permissions?: string[];
  is_active?: boolean;
}

export interface ChangePasswordPayload {
  password: string;
}

export async function listTenantUsers(orgId?: number): Promise<TenantUserItem[]> {
  const params = orgId ? { org_id: orgId } : undefined;
  const { data } = await client.get<TenantUserItem[]>("/settings/users", { params });
  return data;
}

export async function createTenantUser(payload: CreateTenantUserPayload): Promise<TenantUserItem> {
  const { data } = await client.post<TenantUserItem>("/settings/users", payload);
  return data;
}

export async function updateTenantUser(
  userId: number,
  payload: UpdateTenantUserPayload
): Promise<TenantUserItem> {
  const { data } = await client.put<TenantUserItem>(`/settings/users/${userId}`, payload);
  return data;
}

export async function changeTenantUserPassword(
  userId: number,
  payload: ChangePasswordPayload
): Promise<{ ok: boolean; message: string }> {
  const { data } = await client.post<{ ok: boolean; message: string }>(
    `/settings/users/${userId}/change-password`,
    payload
  );
  return data;
}

export async function toggleTenantUserActive(
  userId: number,
  isActive?: boolean
): Promise<{ ok: boolean; is_active: boolean; message: string }> {
  const { data } = await client.post<{ ok: boolean; is_active: boolean; message: string }>(
    `/settings/users/${userId}/toggle-active`,
    isActive !== undefined ? { is_active: isActive } : {}
  );
  return data;
}
