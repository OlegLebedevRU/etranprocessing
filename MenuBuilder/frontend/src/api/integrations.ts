import client from "./client";

export interface ApiKeyData {
  has_key: boolean;
  org_id: number;
  api_key: string | null;
  name: string | null;
  is_active: boolean;
  created_at: string | null;
  updated_at: string | null;
}

export interface ProvisionApiKeyParams {
  org_id?: number;
  api_key?: string;
  name?: string;
  is_active?: boolean;
}

export interface ToggleApiKeyParams {
  org_id?: number;
  is_active: boolean;
}

export async function getApiKey(params?: {
  org_id?: number;
  mask?: boolean;
}): Promise<ApiKeyData> {
  const { data } = await client.get<ApiKeyData>("/integrations/api-key", {
    params,
  });
  return data;
}

export async function revealApiKey(org_id?: number): Promise<ApiKeyData> {
  const { data } = await client.get<ApiKeyData>("/integrations/api-key/reveal", {
    params: org_id ? { org_id } : undefined,
  });
  return data;
}

export async function provisionApiKey(
  payload: ProvisionApiKeyParams
): Promise<ApiKeyData> {
  const { data } = await client.post<ApiKeyData>(
    "/integrations/api-key/provision",
    payload
  );
  return data;
}

export async function toggleApiKeyActive(
  payload: ToggleApiKeyParams
): Promise<ApiKeyData> {
  const { data } = await client.post<ApiKeyData>(
    "/integrations/api-key/toggle-active",
    payload
  );
  return data;
}

export async function deleteApiKey(
  org_id?: number
): Promise<{ status: string; org_id: number }> {
  const { data } = await client.delete<{ status: string; org_id: number }>(
    "/integrations/api-key",
    {
      params: org_id ? { org_id } : undefined,
    }
  );
  return data;
}
