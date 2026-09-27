/** Resolve a deep link only against the current tenant's authorized device list. */
export function linkedDevice<T extends { device_id: number; sn?: string | null }>(
  items: T[], params: URLSearchParams,
): T | null | undefined {
  const id = params.get("device_id");
  if (id !== null) {
    if (!/^[1-9]\d*$/.test(id)) return null;
    return items.find(item => String(item.device_id) === id) ?? null;
  }
  const sn = params.get("sn");
  if (sn !== null) return items.find(item => !!sn && item.sn === sn) ?? null;
  return undefined;
}
