import { Routes, Route, Navigate } from "react-router";
import AppLayout from "./routes/layout";
import LoginPage from "./routes/login";
import MenuManagementLayout from "./routes/menu-management";
import TerminalsPage from "./routes/terminals";
import VariantsPage from "./routes/variants";
import MonitoringPage from "./routes/monitoring";
import ReportsPage from "./routes/reports";
import IntegrationsLayout from "./routes/integrations";
import ApiTokensPage from "./routes/api-tokens";
import BillingPage from "./routes/billing";
import AdminLayout from "./routes/admin-layout";
import AdminOrganizationsPage from "./routes/admin-organizations";
import AdminTerminalsPage from "./routes/admin-terminals";

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
        <Route index element={<Navigate to="/monitoring" replace />} />
        <Route path="monitoring" element={<MonitoringPage />} />
        <Route path="menu" element={<MenuManagementLayout />}>
          <Route index element={<Navigate to="/menu/terminals" replace />} />
          <Route path="terminals" element={<TerminalsPage />} />
          <Route path="variants" element={<VariantsPage />} />
        </Route>
        <Route path="reports" element={<ReportsPage />} />
        <Route path="billing" element={<BillingPage />} />
        <Route path="integrations" element={<IntegrationsLayout />}>
          <Route
            index
            element={<Navigate to="/integrations/tokens" replace />}
          />
          <Route path="tokens" element={<ApiTokensPage />} />
        </Route>
        <Route path="admin" element={<AdminLayout />}>
          <Route
            index
            element={<Navigate to="/admin/organizations" replace />}
          />
          <Route path="organizations" element={<AdminOrganizationsPage />} />
          <Route path="terminals" element={<AdminTerminalsPage />} />
        </Route>
        {/* Legacy paths kept so existing bookmarks keep working */}
        <Route
          path="terminals"
          element={<Navigate to="/menu/terminals" replace />}
        />
        <Route
          path="variants"
          element={<Navigate to="/menu/variants" replace />}
        />
        <Route
          path="profile"
          element={<Navigate to="/integrations/tokens" replace />}
        />
        <Route path="*" element={<Navigate to="/monitoring" replace />} />
      </Route>
    </Routes>
  );
}
