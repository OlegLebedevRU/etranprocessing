import client from "./client";

export interface LoginResponse {
  access_token: string;
  token_type: string;
  expires_in: number;
  master_token?: string | null;
  is_superuser?: boolean;
  role?: string;
  org_id?: number | null;
}

export interface UserInfo {
  username: string;
  org_id?: number | null;
  role?: string;
  is_superuser?: boolean;
  can_switch_org?: boolean;
  token_type?: string;
  is_impersonated?: boolean;
  org_name?: string | null;
}

export async function login(username: string, password: string): Promise<LoginResponse> {
  const { data } = await client.post<LoginResponse>("/auth/login", { username, password });
  return data;
}

export async function getMe(): Promise<UserInfo> {
  const { data } = await client.get<UserInfo>("/auth/me");
  return data;
}
