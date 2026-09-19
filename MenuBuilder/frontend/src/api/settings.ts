import client from "./client";

export interface ProfileSettings {
  username: string;
  user_id: number;
  role: string;
  role_id: number;
  is_superuser: boolean;
  is_readonly: boolean;
  org_id: number;
  org_name: string;
  email: string | null;
  phone: string | null;
  timezone: string;
  notify_by_email: boolean;
  send_reports: boolean;
  is_email_verified: boolean;
  email_verified_at: string | null;
}

export interface UpdateProfilePayload {
  phone?: string | null;
  timezone?: string | null;
  notify_by_email?: boolean;
  send_reports?: boolean;
}

export interface ChangePasswordPayload {
  old_password: string;
  new_password: string;
}

export interface TerminalReadiness {
  record: "ready" | "pending" | "failed";
  certificate: "pending" | "issued" | "consumed" | "expired" | "failed";
  iot: "pending" | "ready" | "failed";
  online: "online" | "offline";
}

export interface TerminalSettingsItem {
  id: number;
  device_id: number;
  sn: string;
  address: string | null;
  note: string | null;
  timezone: string | null;
  is_active: boolean;
  cert_serial: string | null;
  cert_not_valid_after: string | null;
  created_at: string | null;
  updated_at: string | null;
  // L4Desk fields
  ordinal?: number | null;
  is_free?: boolean;
  readiness?: TerminalReadiness;
  provisioning_state?: string;
  pin_state?: string;
  pin_masked?: string | null;
  last_error?: string | null;
  operation_id?: string | null;
}

export interface OnboardTerminalPayload {
  sn?: string | null;
  address?: string | null;
  note?: string | null;
  timezone?: string | null;
  operation_id?: string | null;
  correlation_id?: string | null;
}

export interface TerminalOnboardResponse {
  terminal_id: number;
  tenant_id: number;
  ordinal: number;
  sn: string;
  device_id?: number | null;
  is_free: boolean;
  operation_id: string;
  correlation_id: string;
  readiness: TerminalReadiness;
  pin: string | null;
  pin_masked: string | null;
  pin_expires_at: string | null;
  agent_release_url: string;
  agent_version: string;
  created_at: string;
  last_error?: string | null;
}

export interface OnboardingStatusResponse {
  enabled: boolean;
  agent_release_url: string;
  agent_version: string;
}

export interface TerminalSettingsListResponse {
  items: TerminalSettingsItem[];
  total_count: number;
  page: number;
  page_size: number;
}

export interface ListTerminalsSettingsParams {
  org_id?: number;
  search?: string;
  sort_by?: string;
  sort_order?: "asc" | "desc";
  page?: number;
  page_size?: number;
}

export interface UpdateTerminalSettingsPayload {
  address?: string | null;
  note?: string | null;
  timezone?: string | null;
}

export async function getProfileSettings(orgId?: number): Promise<ProfileSettings> {
  const params = orgId ? { org_id: orgId } : undefined;
  const { data } = await client.get<ProfileSettings>("/settings/profile", { params });
  return data;
}

export async function updateProfileSettings(payload: UpdateProfilePayload): Promise<ProfileSettings> {
  const { data } = await client.patch<ProfileSettings>("/settings/profile", payload);
  return data;
}

export async function changePassword(payload: ChangePasswordPayload): Promise<{ ok: boolean; message: string }> {
  const { data } = await client.post<{ ok: boolean; message: string }>("/settings/profile/change-password", payload);
  return data;
}

export async function requestEmailVerification(email: string): Promise<{ ok: boolean; message: string; email: string }> {
  const { data } = await client.post<{ ok: boolean; message: string; email: string }>(
    "/settings/profile/request-email-verification",
    { email }
  );
  return data;
}

export async function confirmEmailOtp(
  code: string
): Promise<{ ok: boolean; message: string; email: string; is_email_verified: boolean; email_verified_at?: string }> {
  const { data } = await client.post<{
    ok: boolean;
    message: string;
    email: string;
    is_email_verified: boolean;
    email_verified_at?: string;
  }>("/settings/profile/confirm-email-otp", { code });
  return data;
}

export async function confirmEmailToken(
  token: string
): Promise<{ ok: boolean; message: string; email: string; is_email_verified: boolean }> {
  const { data } = await client.post<{
    ok: boolean;
    message: string;
    email: string;
    is_email_verified: boolean;
  }>("/settings/profile/confirm-email-token", { token });
  return data;
}

export async function listTerminalsSettings(
  params?: ListTerminalsSettingsParams | number
): Promise<TerminalSettingsListResponse> {
  const queryParams = typeof params === "number" ? { org_id: params } : params;
  const { data, headers } = await client.get<TerminalSettingsListResponse | TerminalSettingsItem[]>(
    "/settings/terminals",
    { params: queryParams }
  );
  if (Array.isArray(data)) {
    const rawTotal = headers ? headers["x-total-count"] : undefined;
    const total = rawTotal ? parseInt(rawTotal, 10) : data.length;
    return {
      items: data,
      total_count: isNaN(total) ? data.length : total,
      page: queryParams?.page || 1,
      page_size: queryParams?.page_size || data.length,
    };
  }
  return data;
}

export async function updateTerminalSettings(
  terminalId: number,
  payload: UpdateTerminalSettingsPayload
): Promise<TerminalSettingsItem> {
  const { data } = await client.patch<TerminalSettingsItem>(`/settings/terminals/${terminalId}`, payload);
  return data;
}

export async function getTerminalOnboardingStatus(): Promise<OnboardingStatusResponse> {
  const { data } = await client.get<OnboardingStatusResponse>("/settings/terminals/onboard/status");
  return data;
}

export async function onboardTerminal(
  payload: OnboardTerminalPayload,
  orgId?: number
): Promise<TerminalOnboardResponse> {
  const params = orgId ? { org_id: orgId } : undefined;
  const { data } = await client.post<TerminalOnboardResponse>("/settings/terminals", payload, { params });
  return data;
}

export async function retryTerminalOnboarding(
  terminalId: number
): Promise<TerminalOnboardResponse> {
  const { data } = await client.post<TerminalOnboardResponse>(`/settings/terminals/${terminalId}/retry`);
  return data;
}

export async function getTerminalReadiness(
  terminalId: number
): Promise<TerminalOnboardResponse> {
  const { data } = await client.get<TerminalOnboardResponse>(`/settings/terminals/${terminalId}/readiness`);
  return data;
}

export async function deleteTerminal(
  terminalId: number
): Promise<{ ok: boolean; message: string; earliest_free_terminal_id?: number | null }> {
  const { data } = await client.delete<{ ok: boolean; message: string; earliest_free_terminal_id?: number | null }>(
    `/settings/terminals/${terminalId}`
  );
  return data;
}
