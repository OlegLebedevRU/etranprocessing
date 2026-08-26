import client from "./client";

export interface LoginResponse {
  access_token: string;
  refresh_token?: string | null;
  token_type: string;
  expires_in: number;
  refresh_expires_in?: number | null;
  master_token?: string | null;
  is_superuser?: boolean;
  role?: string;
  role_id?: number;
  org_id?: number | null;
  user_id?: number | null;
}

export interface UserInfo {
  user_id?: number | null;
  username: string;
  org_id?: number | null;
  role_id?: number;
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

export async function logout(): Promise<void> {
  try {
    await client.post("/auth/logout");
  } finally {
    localStorage.removeItem("mb_token");
    localStorage.removeItem("mb_user");
  }
}

export async function refreshAuthToken(): Promise<LoginResponse> {
  const { data } = await client.post<LoginResponse>("/auth/refresh");
  return data;
}

export async function getMe(): Promise<UserInfo> {
  const { data } = await client.get<UserInfo>("/auth/me");
  return data;
}
