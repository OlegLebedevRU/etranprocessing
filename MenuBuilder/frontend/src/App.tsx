import { Routes, Route, Navigate } from "react-router";
import AppLayout from "./routes/layout";
import LoginPage from "./routes/login";
import TerminalsPage from "./routes/terminals";
import VariantsPage from "./routes/variants";
import MonitoringPage from "./routes/monitoring";
import ReportsPage from "./routes/reports";
import ProfilePage from "./routes/profile";
import BillingPage from "./routes/billing";

function RequireAuth({ children }: { children: React.ReactNode }) {
  const token = localStorage.getItem("mb_token");
  if (!token) {
    return <Navigate to="/login" replace />;
  }
  return <>{children}</>;
}

export default function App() {
  return (
    <Routes>
      <Route path="/login" element={<LoginPage />} />
      <Route
        element={
          <RequireAuth>
            <AppLayout />
          </RequireAuth>
        }
      >
        <Route index element={<TerminalsPage />} />
        <Route path="variants" element={<VariantsPage />} />
        <Route path="monitoring" element={<MonitoringPage />} />
        <Route path="reports" element={<ReportsPage />} />
        <Route path="billing" element={<BillingPage />} />
        <Route path="profile" element={<ProfilePage />} />
        <Route path="*" element={<Navigate to="/" replace />} />
      </Route>
    </Routes>
  );
}
