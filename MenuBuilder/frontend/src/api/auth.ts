import client from "./client";

export interface LoginResponse {
  access_token: string;
  token_type: string;
  expires_in: number;
}

export interface UserInfo {
  username: string;
}

export async function login(username: string, password: string): Promise<LoginResponse> {
  const { data } = await client.post<LoginResponse>("/auth/login", { username, password });
  return data;
}

export async function getMe(): Promise<UserInfo> {
  const { data } = await client.get<UserInfo>("/auth/me");
  return data;
}
