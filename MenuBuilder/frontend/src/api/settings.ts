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

export async function listTerminalsSettings(orgId?: number): Promise<TerminalSettingsItem[]> {
  const params = orgId ? { org_id: orgId } : undefined;
  const { data } = await client.get<TerminalSettingsItem[]>("/settings/terminals", { params });
  return data;
}

export async function updateTerminalSettings(
  terminalId: number,
  payload: UpdateTerminalSettingsPayload
): Promise<TerminalSettingsItem> {
  const { data } = await client.patch<TerminalSettingsItem>(`/settings/terminals/${terminalId}`, payload);
  return data;
}
