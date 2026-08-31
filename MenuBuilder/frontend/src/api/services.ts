import client from "./client";

export interface Service {
  id: number;
  menu_variant_id: number;
  group_id: number;
  tsp_code: number;
  name: string;
  printname: string | null;
  price: number;
  protypenumber: number;
  catalog_item_id?: number | null;
}

export interface ServiceCreate {
  group_id: number;
  name: string;
  tsp_code?: number;
  printname?: string | null;
  price?: number;
  protypenumber?: number;
  catalog_item_id?: number | null;
  add_to_catalog?: boolean;
  catalog_category_id?: number | null;
}

export interface ServiceUpdate {
  name?: string;
  printname?: string | null;
  price?: number;
  protypenumber?: number;
  catalog_item_id?: number | null;
}

export const getServices = (groupId?: number) =>
  client.get<Service[]>("/services", {
    params: groupId ? { group_id: groupId } : {},
  });

export const getService = (id: number) =>
  client.get<Service>(`/services/${id}`);

export const createService = (data: ServiceCreate) =>
  client.post<Service>("/services", data);

export const updateService = (id: number, data: ServiceUpdate) =>
  client.put<Service>(`/services/${id}`, data);

export const deleteService = (id: number) =>
  client.delete(`/services/${id}`);

export const getFreeTsp = (groupId: number, fromCatalog: boolean = false) =>
  client.get<{ tsp_code: number }[]>("/services/free-tsp", {
    params: { group_id: groupId, from_catalog: fromCatalog },
  });
