import client from "./client";

export interface OrgItem {
  org_id: number;
  org_name: string;
  name?: string | null;
  is_active: boolean;
}

export interface SwitchTenantResponse {
  access_token: string;
  token_type: string;
  org_id: number;
  org_name: string;
  expires_in: number;
}

export async function listAvailableTenants(): Promise<OrgItem[]> {
  const { data } = await client.get<OrgItem[]>("/admin/tenants/available");
  return data;
}

export async function switchTenant(orgId: number): Promise<SwitchTenantResponse> {
  const { data } = await client.post<SwitchTenantResponse>("/admin/tenants/switch", {
    org_id: orgId,
  });
  return data;
}
