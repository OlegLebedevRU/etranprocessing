import { lazy, Suspense } from "react";
import { Routes, Route, Navigate } from "react-router";
import { Spin } from "antd";
import { SessionProvider, useSession } from "./session/SessionContext";

const AppLayout = lazy(() => import("./routes/layout"));
const LoginPage = lazy(() => import("./routes/login"));
const MenuManagementLayout = lazy(() => import("./routes/menu-management"));
const TerminalsPage = lazy(() => import("./routes/terminals"));
const VariantsPage = lazy(() => import("./routes/variants"));
const CatalogPage = lazy(() => import("./routes/catalog"));
const MenuHelpPage = lazy(() => import("./routes/menu-help"));
const MonitoringPage = lazy(() => import("./routes/monitoring"));
const ReportsPage = lazy(() => import("./routes/reports"));
const IntegrationsPage = lazy(() => import("./routes/integrations"));
const BillingPage = lazy(() => import("./routes/billing"));
const DevicesPage = lazy(() => import("./routes/devices"));
const AdminLayout = lazy(() => import("./routes/admin-layout"));
const AdminOrganizationsPage = lazy(() => import("./routes/admin-organizations"));
const AdminTerminalsPage = lazy(() => import("./routes/admin-terminals"));
const AdminUsersPage = lazy(() => import("./routes/admin-users"));
const SettingsLayout = lazy(() => import("./routes/settings-layout"));
const ProfileSettingsPage = lazy(() => import("./routes/settings/ProfileSettingsPage"));
const TerminalsSettingsPage = lazy(() => import("./routes/settings/TerminalsSettingsPage"));
const VerifyEmailPage = lazy(() => import("./routes/settings/VerifyEmailPage"));

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
  const { user, loading } = useSession();
  if (loading) {
    return <LoadingFallback />;
  }
  if (!user) {
    return <Navigate to="/login" replace />;
  }
  return <>{children}</>;
}

export default function App() {
  return (
    <SessionProvider>
      <Suspense fallback={<LoadingFallback />}>
        <Routes>
          <Route path="/login" element={<LoginPage />} />
          <Route path="/settings/verify-email" element={<VerifyEmailPage />} />
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
              <Route path="catalog" element={<CatalogPage />} />
              <Route path="help" element={<MenuHelpPage />} />
            </Route>
            <Route path="reports" element={<ReportsPage />} />
            <Route path="billing" element={<BillingPage />} />
            <Route path="devices" element={<DevicesPage />} />
            <Route path="integrations" element={<IntegrationsPage />} />
            <Route path="settings" element={<SettingsLayout />}>
              <Route
                index
                element={<Navigate to="/settings/profile" replace />}
              />
              <Route path="profile" element={<ProfileSettingsPage />} />
              <Route path="terminals" element={<TerminalsSettingsPage />} />
              <Route path="verify-email" element={<VerifyEmailPage />} />
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
              element={<Navigate to="/settings/profile" replace />}
            />
            <Route path="*" element={<Navigate to="/monitoring" replace />} />
          </Route>
        </Routes>
      </Suspense>
    </SessionProvider>
  );
}
