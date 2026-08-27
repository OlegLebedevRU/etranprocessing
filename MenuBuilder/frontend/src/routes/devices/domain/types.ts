export enum TaskStatus {
  READY = 0,
  PENDING = 1,
  LOCK = 2,
  DONE = 3,
  EXPIRED = 4,
  DELETED = 5,
  FAILED = 6,
  UNDEFINED = 7,
}

export type DtFormat =
  | "empty"
  | "objectArray"
  | "stringArray"
  | "numberArray"
  | "objectFields"
  | "dbWrite"
  | "fullscreenJpeg"
  | "custom";

export type NvsType = "i8" | "u8" | "i16" | "u16" | "i32" | "u32" | "str";

export interface MethodFieldDefinition {
  name: string;
  label: string;
  type: "string" | "number" | "boolean" | "select" | "json";
  defaultValue?: any;
  tooltip?: string;
  required?: boolean;
  placeholder?: string;
  options?: Array<{ label: string; value: string | number }>;
  validation?: {
    min?: number;
    max?: number;
    regex?: string;
    regexMessage?: string;
  };
}

export interface DeviceTagContextItem {
  tag: string;
  value: string;
  id?: number;
}

export interface DeviceContext {
  deviceId: number;
  sn?: string;
  orgId?: number;
  role?: string;
  tags?: DeviceTagContextItem[];
  appVersion?: string;
  status?: string;
  capabilities?: string[];
  [key: string]: any;
}

export interface MethodDefinition {
  code: number;
  name: string;
  label: string;
  description: string;
  dtFormat: DtFormat;
  supportsMultiple?: boolean;
  fields?: MethodFieldDefinition[];
  isCustom?: boolean;
  allowedRoles?: string[];
  requiredCapabilities?: string[];
  requiredTags?: string[];
  deviceFilter?: (context: DeviceContext) => boolean;
  metadata?: Record<string, any>;
}

export interface TaskResultItem {
  id: number;
  ext_id?: number;
  status_code: number;
  result?: Record<string, any> | any[] | null;
}

export interface TaskDetail {
  id: string;
  ext_task_id: string;
  device_id: number;
  method_code: number;
  status: TaskStatus | number;
  priority?: number;
  ttl?: number;
  payload?: {
    dt?: any[];
    [key: string]: any;
  } | null;
  created_at: number | string;
  pending_at?: number | string | null;
  locked_at?: number | string | null;
  deleted_at?: number | string | null;
  results?: TaskResultItem[] | null;
  [key: string]: any;
}

export interface TaskCreatePayload {
  ext_task_id: string;
  device_id: number;
  method_code: number;
  priority: number;
  ttl: number;
  payload?: {
    dt: any[];
  };
}
