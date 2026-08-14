import { Routes, Route, Navigate } from "react-router";
import AppLayout from "./routes/layout";
import TerminalsPage from "./routes/terminals";
import VariantsPage from "./routes/variants";
import MonitoringPage from "./routes/monitoring";
import ReportsPage from "./routes/reports";

export default function App() {
  return (
    <Routes>
      <Route element={<AppLayout />}>
        <Route index element={<TerminalsPage />} />
        <Route path="variants" element={<VariantsPage />} />
        <Route path="monitoring" element={<MonitoringPage />} />
        <Route path="reports" element={<ReportsPage />} />
        <Route path="*" element={<Navigate to="/" replace />} />
      </Route>
    </Routes>
  );
}
