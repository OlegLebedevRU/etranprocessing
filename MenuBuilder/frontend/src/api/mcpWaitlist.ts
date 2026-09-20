import client from "./client";

export interface McpWaitlistPayload {
  use_case: string;
  note?: string;
  contact_email?: string;
}

export interface McpWaitlistResponse {
  status: string;
  tenant_id: number;
  use_case: string;
  contact_email?: string;
  registered_at: string;
}

export interface McpWaitlistStatusResponse {
  registered: boolean;
  tenant_id?: number;
  use_case?: string;
  note?: string;
  registered_at?: string;
}

export async function joinMcpWaitlist(payload: McpWaitlistPayload): Promise<McpWaitlistResponse> {
  const { data } = await client.post<McpWaitlistResponse>("/v1/mcp/waitlist", payload);
  return data;
}

export async function getMcpWaitlistStatus(): Promise<McpWaitlistStatusResponse> {
  const { data } = await client.get<McpWaitlistStatusResponse>("/v1/mcp/waitlist/status");
  return data;
}
