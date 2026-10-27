import { createContext, useContext } from "react";
import type { ReactNode } from "react";

export const CameraToolsContext = createContext<{ tools: ReactNode; summary: ReactNode } | null>(null);
export const useCameraTools = () => useContext(CameraToolsContext);
