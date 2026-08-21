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
}

export interface ServiceCreate {
  group_id: number;
  name: string;
  tsp_code?: number;
  printname?: string;
  price?: number;
  protypenumber?: number;
}

export interface ServiceUpdate {
  name?: string;
  printname?: string;
  price?: number;
  protypenumber?: number;
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

export const getFreeTsp = (groupId: number) =>
  client.get<{ tsp_code: number }[]>("/services/free-tsp", {
    params: { group_id: groupId },
  });
