import client from "./client";

export interface TokenInfo {
  jti: string;
  name: string | null;
  expires_at: string;
  created_at: string;
  last_used_at: string | null;
  revoked_at: string | null;
}

export interface CreateTokenResponse {
  token: string;
  jti: string;
  name: string;
  expires_at: string;
  expires_days: number;
}

export async function createToken(name: string, expiresDays: number = 30): Promise<CreateTokenResponse> {
  const { data } = await client.post<CreateTokenResponse>("/profile/tokens", {
    name,
    expires_days: expiresDays,
  });
  return data;
}

export async function listTokens(): Promise<TokenInfo[]> {
  const { data } = await client.get<{ tokens: TokenInfo[] }>("/profile/tokens");
  return data.tokens;
}

export async function revokeToken(jti: string): Promise<void> {
  await client.delete(`/profile/tokens/${jti}`);
}

export async function getProfile(): Promise<{ username: string; org_id: number }> {
  const { data } = await client.get("/profile/me");
  return data;
}
