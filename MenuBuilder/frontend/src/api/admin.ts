import client from "./client";
import { invalidateCache, withCache } from "./cache";

export interface AdminOrg {
  org_id: number;
  org_name: string;
  name: string;
  status: number;
  is_active: boolean;
  timezone?: string;
  email?: string | null;
  phone?: string | null;
  notify_by_email?: boolean;
  created_at?: string | null;
  updated_at?: string | null;
  monthly_price_minor: number;
  currency: string;
  billing_mode?: string;
  min_billing_periods?: number;
  allowed_billing_periods?: string | null;
  default_selection_mode?: string;
  cert_billing_mode: string;
  cert_price_minor?: number | null;
  tenant_pin_creation_enabled: boolean;
  cert_charge_primary_issue: boolean;
  cert_charge_reissue: boolean;
}

export interface AdminOrgCreateInput {
  org_id?: number;
  org_name: string;
  name: string;
  status?: number;
  is_active?: boolean;
  timezone?: string;
  email?: string;
  phone?: string;
  notify_by_email?: boolean;
  monthly_price_minor?: number;
  currency?: string;
  billing_mode?: string;
  min_billing_periods?: number;
  allowed_billing_periods?: string | null;
  default_selection_mode?: string;
  cert_billing_mode?: string;
  cert_price_minor?: number | null;
  tenant_pin_creation_enabled?: boolean;
  cert_charge_primary_issue?: boolean;
  cert_charge_reissue?: boolean;
}

export interface AdminOrgUpdateInput {
  org_name?: string;
  name?: string;
  status?: number;
  is_active?: boolean;
  timezone?: string;
  email?: string | null;
  phone?: string | null;
  notify_by_email?: boolean;
  monthly_price_minor?: number;
  currency?: string;
  billing_mode?: string;
  min_billing_periods?: number;
  allowed_billing_periods?: string | null;
  default_selection_mode?: string;
  cert_billing_mode?: string;
  cert_price_minor?: number | null;
  tenant_pin_creation_enabled?: boolean;
  cert_charge_primary_issue?: boolean;
  cert_charge_reissue?: boolean;
}

export interface TerminalType {
  id: number;
  name: string;
  description?: string | null;
}

export interface AdminTerminal {
  id: number;
  device_id: number;
  sn: string;
  cert_serial?: string | null;
  cert_not_valid_after?: string | null;
  org_id: number;
  org_name?: string | null;
  is_active: boolean;
  timezone?: string | null;
  show_in_monitoring?: boolean;
  address?: string | null;
  note?: string | null;
  terminal_type_id: number;
  terminal_type_name?: string | null;
  created_at?: string | null;
  updated_at?: string | null;
  license_id?: number | null;
  license_expires_at?: string | null;
  license_is_active?: boolean | null;
  license_balance?: number | null;
  license_type?: string | null;
  billing_period_months?: number | null;
  monthly_price_override_minor?: number | null;
  renewal_enabled?: boolean | null;
  deactivation_requested_at?: string | null;
  pending_pin?: string | null;
  pin_expires_at?: string | null;
  pin_status?: string | null;
  iot_provisioned?: boolean;
  iot_provisioned_at?: string | null;
  iot_last_sync_at?: string | null;
  iot_is_online?: boolean;
  iot_last_connected_at?: string | null;
}

export interface AdminTerminalListResponse {
  total: number;
  page: number;
  page_size: number;
  items: AdminTerminal[];
}

export interface AdminTerminalCreateInput {
  device_id: number;
  org_id: number;
  terminal_type_id?: number;
  address?: string;
  note?: string;
  timezone?: string | null;
  is_active?: boolean;
  show_in_monitoring?: boolean;
  iot_provisioned?: boolean;
  license_expires_at?: string;
  billing_period_months?: number;
  renewal_enabled?: boolean;
}

export interface AdminTerminalUpdateInput {
  org_id?: number;
  terminal_type_id?: number;
  address?: string;
  note?: string;
  timezone?: string | null;
  is_active?: boolean;
  show_in_monitoring?: boolean;
  iot_provisioned?: boolean;
  license_expires_at?: string;
  license_is_active?: boolean;
  renewal_enabled?: boolean;
  billing_period_months?: number;
  monthly_price_override_minor?: number | null;
}

export interface GeneratePinResponse {
  pin: string;
  terminal_id: number;
  device_id: number;
  expires_at: string;
}

export interface SetLicenseRequest {
  expires_at: string;
  is_active?: boolean;
  renewal_enabled?: boolean;
}

export interface SetLicenseResponse {
  terminal_id: number;
  license_id: number;
  expires_at: string;
  is_active: boolean;
  renewal_enabled: boolean;
}

export async function getAdminOrganizations(forceFresh = false): Promise<AdminOrg[]> {
  if (forceFresh) {
    invalidateCache("admin_orgs");
  }
  return withCache<AdminOrg[]>(
    "admin_orgs",
    async () => {
      const { data } = await client.get<AdminOrg[]>("/admin/organizations");
      return data;
    },
    120_000
  );
}

export async function createAdminOrganization(
  payload: AdminOrgCreateInput
): Promise<AdminOrg> {
  invalidateCache("admin_orgs");
  invalidateCache("available_tenants");
  const { data } = await client.post<AdminOrg>(
    "/admin/organizations",
    payload
  );
  return data;
}

export async function updateAdminOrganization(
  orgId: number,
  payload: AdminOrgUpdateInput
): Promise<AdminOrg> {
  invalidateCache("admin_orgs");
  invalidateCache("available_tenants");
  const { data } = await client.put<AdminOrg>(
    `/admin/organizations/${orgId}`,
    payload
  );
  return data;
}

export async function getTerminalTypes(): Promise<TerminalType[]> {
  const { data } = await client.get<TerminalType[]>("/admin/terminal-types");
  return data;
}

export async function getNextDeviceId(): Promise<{ next_device_id: number }> {
  const { data } = await client.get<{ next_device_id: number }>(
    "/admin/terminals/next-device-id"
  );
  return data;
}

export async function getAdminTerminals(params?: {
  org_id?: number;
  search?: string;
  is_active?: boolean;
  page?: number;
  page_size?: number;
}): Promise<AdminTerminalListResponse> {
  const { data } = await client.get<AdminTerminalListResponse>(
    "/admin/terminals",
    { params }
  );
  return data;
}

export async function createAdminTerminal(
  payload: AdminTerminalCreateInput
): Promise<AdminTerminal> {
  const { data } = await client.post<AdminTerminal>(
    "/admin/terminals",
    payload
  );
  return data;
}

export async function updateAdminTerminal(
  terminalId: number,
  payload: AdminTerminalUpdateInput
): Promise<AdminTerminal> {
  const { data } = await client.put<AdminTerminal>(
    `/admin/terminals/${terminalId}`,
    payload
  );
  return data;
}

export async function generateTerminalPin(
  terminalId: number
): Promise<GeneratePinResponse> {
  const { data } = await client.post<GeneratePinResponse>(
    `/admin/terminals/${terminalId}/generate-pin`
  );
  return data;
}

export async function setTerminalLicense(
  terminalId: number,
  payload: SetLicenseRequest
): Promise<SetLicenseResponse> {
  const { data } = await client.post<SetLicenseResponse>(
    `/admin/terminals/${terminalId}/set-license`,
    payload
  );
  return data;
}

export async function setTerminalStatus(
  terminalId: number,
  isActive: boolean
): Promise<AdminTerminal> {
  const { data } = await client.post<AdminTerminal>(
    `/admin/terminals/${terminalId}/set-status`,
    { is_active: isActive }
  );
  return data;
}

export interface ProvisionTerminalResponse {
  success: boolean;
  terminal_id: number;
  device_id: number;
  sn: string;
  org_id: number;
  iot_provisioned: boolean;
  iot_provisioned_at?: string | null;
  iot_is_online: boolean;
  rmq_user_status?: string | null;
  error?: string | null;
}

export interface BatchProvisionTerminalsResponse {
  results: ProvisionTerminalResponse[];
}

export async function provisionTerminalToIot(
  terminalId: number
): Promise<ProvisionTerminalResponse> {
  const { data } = await client.post<ProvisionTerminalResponse>(
    `/admin/terminals/${terminalId}/provision-iot`
  );
  return data;
}

export async function provisionBatchTerminalsToIot(
  terminalIds: number[]
): Promise<BatchProvisionTerminalsResponse> {
  const { data } = await client.post<BatchProvisionTerminalsResponse>(
    "/admin/terminals/provision-iot-batch",
    { terminal_ids: terminalIds }
  );
  return data;
}
