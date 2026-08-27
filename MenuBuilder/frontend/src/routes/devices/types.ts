export * from "./domain";
import { METHOD_CATALOG_DEFINITIONS, type MethodDefinition } from "./domain";

export interface MethodCatalogItem {
  code: number;
  label: string;
  description: string;
  defaultParams?: Record<string, any>;
  fields?: Array<{
    name: string;
    label: string;
    type: "string" | "number" | "boolean" | "select" | "json";
    defaultValue?: any;
    tooltip?: string;
  }>;
}

export const METHOD_CATALOG: MethodCatalogItem[] = METHOD_CATALOG_DEFINITIONS.map((m: MethodDefinition) => ({
  code: m.code,
  label: m.label,
  description: m.description,
  fields: m.fields?.map((f) => ({
    name: f.name,
    label: f.label,
    type: f.type,
    defaultValue: f.defaultValue,
    tooltip: f.tooltip,
  })),
}));
