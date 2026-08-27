import { lazy, Suspense } from "react";
import { Routes, Route, Navigate } from "react-router";
import { Spin } from "antd";

const AppLayout = lazy(() => import("./routes/layout"));
const LoginPage = lazy(() => import("./routes/login"));
const MenuManagementLayout = lazy(() => import("./routes/menu-management"));
const TerminalsPage = lazy(() => import("./routes/terminals"));
const VariantsPage = lazy(() => import("./routes/variants"));
const MonitoringPage = lazy(() => import("./routes/monitoring"));
const ReportsPage = lazy(() => import("./routes/reports"));
const IntegrationsLayout = lazy(() => import("./routes/integrations"));
const ApiConnectionPage = lazy(() => import("./routes/api-connection"));
const ApiTokensPage = lazy(() => import("./routes/api-tokens"));
const BillingPage = lazy(() => import("./routes/billing"));
const DevicesPage = lazy(() => import("./routes/devices"));
const AdminLayout = lazy(() => import("./routes/admin-layout"));
const AdminOrganizationsPage = lazy(() => import("./routes/admin-organizations"));
const AdminTerminalsPage = lazy(() => import("./routes/admin-terminals"));
const AdminUsersPage = lazy(() => import("./routes/admin-users"));

function LoadingFallback() {
  return (
    <div
      style={{
        display: "flex",
        justifyContent: "center",
        alignItems: "center",
        minHeight: "40vh",
        width: "100%",
      }}
    >
      <Spin size="large" />
    </div>
  );
}

function RequireAuth({ children }: { children: React.ReactNode }) {
  const token = localStorage.getItem("mb_token");
  if (!token) {
    return <Navigate to="/login" replace />;
  }
  return <>{children}</>;
}

export default function App() {
  return (
    <Suspense fallback={<LoadingFallback />}>
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
          <Route path="devices" element={<DevicesPage />} />
          <Route path="integrations" element={<IntegrationsLayout />}>
            <Route
              index
              element={<Navigate to="/integrations/api-connection" replace />}
            />
            <Route path="api-connection" element={<ApiConnectionPage />} />
            <Route path="tokens" element={<ApiTokensPage />} />
          </Route>
          <Route path="admin" element={<AdminLayout />}>
            <Route
              index
              element={<Navigate to="/admin/organizations" replace />}
            />
            <Route path="organizations" element={<AdminOrganizationsPage />} />
            <Route path="terminals" element={<AdminTerminalsPage />} />
            <Route path="users" element={<AdminUsersPage />} />
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
    </Suspense>
  );
}
