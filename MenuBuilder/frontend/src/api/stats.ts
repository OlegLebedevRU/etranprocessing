import client from "./client";

interface Stats {
  groups: number;
  services: number;
  tsp_codes: number;
  avg_price: number;
}

export const getStats = (variantId?: number) =>
  client.get<Stats>("/stats", { params: variantId ? { variant_id: variantId } : {} });
