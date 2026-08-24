import client from "./client";

export interface MenuVariant {
  id: number;
  org_id?: number;
  name: string;
  version?: number;
  created_at: string;
}

export interface MenuVariantCreate {
  name: string;
}

export interface MenuVariantDuplicate {
  source_variant_id: number;
  new_name?: string;
}

export const getMenuVariants = () =>
  client.get<MenuVariant[]>("/menu-variants");

export const getMenuVariant = (id: number) =>
  client.get<MenuVariant>(`/menu-variants/${id}`);

export const createMenuVariant = (data: MenuVariantCreate) =>
  client.post<MenuVariant>("/menu-variants", data);

export const deleteMenuVariant = (id: number) =>
  client.delete(`/menu-variants/${id}`);

export const duplicateMenuVariant = (data: MenuVariantDuplicate) =>
  client.post<MenuVariant>("/menu-variants/duplicate", data);
