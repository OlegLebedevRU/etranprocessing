import client from "./client";

export interface CatalogCategory {
  id: number;
  org_id: number;
  name: string;
  parent_id: number | null;
  sort_order: number;
  depth: number;
  items_count: number;
  created_at?: string;
  updated_at?: string;
  children?: CatalogCategory[];
}

export interface CatalogCategoryCreate {
  name: string;
  parent_id?: number | null;
  sort_order?: number;
}

export interface CatalogCategoryUpdate {
  name?: string;
  parent_id?: number | null;
  sort_order?: number;
}

export interface CatalogItem {
  id: number;
  org_id: number;
  category_id: number;
  tsp_code: number;
  name: string;
  printname: string | null;
  price: number;
  protypenumber: number;
  category_name?: string | null;
  created_at?: string;
  updated_at?: string;
}

export interface CatalogItemCreate {
  category_id: number;
  name: string;
  tsp_code?: number;
  printname?: string | null;
  price?: number;
  protypenumber?: number;
}

export interface CatalogItemUpdate {
  category_id?: number;
  name?: string;
  printname?: string | null;
  price?: number;
  protypenumber?: number;
}

export interface CatalogPropagateResponse {
  updated_variants_count: number;
  updated_services_count: number;
  affected_variant_names: string[];
}

export const getCatalogCategories = () =>
  client.get<CatalogCategory[]>("/catalog/categories");

export const createCatalogCategory = (data: CatalogCategoryCreate) =>
  client.post<CatalogCategory>("/catalog/categories", data);

export const updateCatalogCategory = (id: number, data: CatalogCategoryUpdate) =>
  client.put<CatalogCategory>(`/catalog/categories/${id}`, data);

export const deleteCatalogCategory = (id: number) =>
  client.delete(`/catalog/categories/${id}`);

export const getCatalogItems = (categoryId?: number, search?: string) =>
  client.get<CatalogItem[]>("/catalog/items", {
    params: {
      ...(categoryId ? { category_id: categoryId } : {}),
      ...(search ? { search } : {}),
    },
  });

export const getFreeCatalogTsp = () =>
  client.get<{ tsp_code: number }[]>("/catalog/free-tsp");

export const createCatalogItem = (data: CatalogItemCreate) =>
  client.post<CatalogItem>("/catalog/items", data);

export const updateCatalogItem = (id: number, data: CatalogItemUpdate) =>
  client.put<CatalogItem>(`/catalog/items/${id}`, data);

export const deleteCatalogItem = (id: number) =>
  client.delete(`/catalog/items/${id}`);

export const propagateCatalogToMenus = () =>
  client.post<CatalogPropagateResponse>("/catalog/propagate-to-menus");
