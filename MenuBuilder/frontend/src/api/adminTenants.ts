import client from "./client";
import { invalidateCache, withCache } from "./cache";

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

export async function listAvailableTenants(forceFresh = false): Promise<OrgItem[]> {
  if (forceFresh) {
    invalidateCache("available_tenants");
  }
  return withCache<OrgItem[]>(
    "available_tenants",
    async () => {
      const { data } = await client.get<OrgItem[]>("/admin/tenants/available");
      return data;
    },
    120_000
  );
}

export async function switchTenant(orgId: number): Promise<SwitchTenantResponse> {
  invalidateCache();
  const { data } = await client.post<SwitchTenantResponse>("/admin/tenants/switch", {
    org_id: orgId,
  });
  return data;
}
