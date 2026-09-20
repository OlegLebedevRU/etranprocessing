import { lazy, Suspense, useState } from "react";
import { Routes, Route, Navigate } from "react-router";
import { Button, Modal, Space, Spin, Typography } from "antd";
import { LockOutlined, LogoutOutlined } from "@ant-design/icons";
import { SessionProvider, useSession } from "./session/SessionContext";
import { logout } from "./api/auth";
import { notifySessionEvent } from "./api/session";
import {
  PERMISSION_BILLING_VIEW,
  PERMISSION_MONITORING_VIEW,
  PERMISSION_SETTINGS_TERMINALS_VIEW,
  PERMISSION_VIDEO_VIEW,
  getDefaultRouteForViewer,
  hasAnyReportPermission,
  hasPermission,
} from "./utils/permissions";
import { getNavigationProfile } from "./utils/navigationProfile";

const { Paragraph } = Typography;

const AppLayout = lazy(() => import("./routes/layout"));
const LoginPage = lazy(() => import("./routes/login"));
const RegisterPage = lazy(() => import("./routes/register"));
const ConfirmRegistrationPage = lazy(() => import("./routes/register-confirm"));
const MenuManagementLayout = lazy(() => import("./routes/menu-management"));
const TerminalsPage = lazy(() => import("./routes/terminals"));
const VariantsPage = lazy(() => import("./routes/variants"));
const CatalogPage = lazy(() => import("./routes/catalog"));
const MenuHelpPage = lazy(() => import("./routes/menu-help"));
const MonitoringPage = lazy(() => import("./routes/monitoring"));
const VideoSurveillancePage = lazy(() => import("./routes/video-surveillance"));
const ReportsPage = lazy(() => import("./routes/reports"));
const IntegrationsPage = lazy(() => import("./routes/integrations"));
const BillingPage = lazy(() => import("./routes/billing"));
const DevicesPage = lazy(() => import("./routes/devices"));
const ConsolePage = lazy(() => import("./routes/console/ConsolePage"));
const McpPromoPage = lazy(() => import("./routes/mcp/McpPromoPage"));
const LicensesPage = lazy(() => import("./routes/licenses/LicensesPage"));
const AdminLayout = lazy(() => import("./routes/admin-layout"));
const AdminOrganizationsPage = lazy(() => import("./routes/admin-organizations"));
const AdminTerminalsPage = lazy(() => import("./routes/admin-terminals"));
const AdminUsersPage = lazy(() => import("./routes/admin-users"));
const SettingsLayout = lazy(() => import("./routes/settings-layout"));
const ProfileSettingsPage = lazy(() => import("./routes/settings/ProfileSettingsPage"));
const TerminalsSettingsPage = lazy(() => import("./routes/settings/TerminalsSettingsPage"));
const UserSettingsPage = lazy(() => import("./routes/settings/UserSettingsPage"));
const VerifyEmailPage = lazy(() => import("./routes/settings/VerifyEmailPage"));

function LoadingFallback() {
  return (
    <div
      style={{
        display: "flex",
        flexDirection: "column",
        justifyContent: "center",
        alignItems: "center",
        minHeight: "50vh",
        width: "100%",
        gap: 16,
      }}
    >
      <Spin size="large" />
      <span style={{ color: "#8c8c8c", fontSize: 14 }}>Загрузка интерфейса...</span>
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

function DefaultRouteResolver() {
  const { user } = useSession();
  const [modalOpen] = useState(true);

  if (!user) {
    return <Navigate to="/login" replace />;
  }

  if (user.role_id !== 4) {
    const profile = getNavigationProfile(user);
    if (profile === "l4desk") {
      return <Navigate to="/video" replace />;
    }
    return <Navigate to="/monitoring" replace />;
  }

  const defaultRoute = getDefaultRouteForViewer(user);
  if (defaultRoute) {
    return <Navigate to={defaultRoute} replace />;
  }

  // Viewer with no permissions assigned
  return (
    <Modal
      open={modalOpen}
      closable={false}
      maskClosable={false}
      footer={[
        <Button
          key="logout"
          type="primary"
          danger
          icon={<LogoutOutlined />}
          onClick={async () => {
            notifySessionEvent({ type: "logout" });
            await logout();
            window.location.href = "/login";
          }}
        >
          Выйти из системы
        </Button>,
      ]}
      title={
        <Space>
          <LockOutlined style={{ color: "#ff4d4f" }} />
          <span>Доступ ограничен</span>
        </Space>
      }
    >
      <Paragraph style={{ marginTop: 16 }}>
        Вам необходимо обратиться к Главному пользователю для назначения доступов к разделам.
      </Paragraph>
    </Modal>
  );
}

function SettingsIndexResolver() {
  const { user } = useSession();
  if (user?.role_id === 4) {
    return <Navigate to="/settings/terminals" replace />;
  }
  return <Navigate to="/settings/profile" replace />;
}

function ViewerGuard({
  children,
  permission,
  requireTenantAdmin,
  forbiddenForRole4,
}: {
  children: React.ReactNode;
  permission?: string;
  requireTenantAdmin?: boolean;
  forbiddenForRole4?: boolean;
}) {
  const { user } = useSession();
  if (!user) return <Navigate to="/login" replace />;

  if (forbiddenForRole4 && user.role_id === 4) {
    const fallback = getDefaultRouteForViewer(user);
    return fallback ? <Navigate to={fallback} replace /> : <DefaultRouteResolver />;
  }

  if (requireTenantAdmin) {
    const isTenantAdmin = user.is_superuser || user.role_id === 1 || user.role_id === 3;
    if (!isTenantAdmin) {
      const fallback = getDefaultRouteForViewer(user);
      return fallback ? <Navigate to={fallback} replace /> : <DefaultRouteResolver />;
    }
  }

  if (permission && user.role_id === 4) {
    let allowed = false;
    if (permission === "reports") {
      allowed = hasAnyReportPermission(user);
    } else {
      allowed = hasPermission(user, permission);
    }
    if (!allowed) {
      const fallback = getDefaultRouteForViewer(user);
      return fallback ? <Navigate to={fallback} replace /> : <DefaultRouteResolver />;
    }
  }

  return <>{children}</>;
}

export default function App() {
  return (
    <SessionProvider>
      <Suspense fallback={<LoadingFallback />}>
        <Routes>
          <Route path="/login" element={<LoginPage />} />
          <Route path="/register" element={<RegisterPage />} />
          <Route path="/register/confirm" element={<ConfirmRegistrationPage />} />
          <Route path="/settings/verify-email" element={<VerifyEmailPage />} />
          <Route
            element={
              <RequireAuth>
                <AppLayout />
              </RequireAuth>
            }
          >
            <Route index element={<DefaultRouteResolver />} />
            <Route
              path="monitoring"
              element={
                <ViewerGuard permission={PERMISSION_MONITORING_VIEW}>
                  <MonitoringPage />
                </ViewerGuard>
              }
            />
            <Route
              path="video"
              element={
                <ViewerGuard permission={PERMISSION_VIDEO_VIEW}>
                  <VideoSurveillancePage />
                </ViewerGuard>
              }
            />
            <Route
              path="menu"
              element={
                <ViewerGuard forbiddenForRole4>
                  <MenuManagementLayout />
                </ViewerGuard>
              }
            >
              <Route index element={<Navigate to="/menu/terminals" replace />} />
              <Route path="terminals" element={<TerminalsPage />} />
              <Route path="variants" element={<VariantsPage />} />
              <Route path="catalog" element={<CatalogPage />} />
              <Route path="help" element={<MenuHelpPage />} />
            </Route>
            <Route
              path="reports"
              element={
                <ViewerGuard permission="reports">
                  <ReportsPage />
                </ViewerGuard>
              }
            />
            <Route
              path="billing"
              element={
                <ViewerGuard permission={PERMISSION_BILLING_VIEW}>
                  <BillingPage />
                </ViewerGuard>
              }
            />
            <Route path="licenses" element={<LicensesPage />} />
            <Route path="devices" element={<DevicesPage />} />
            <Route path="console" element={<ConsolePage />} />
            <Route path="mcp" element={<McpPromoPage />} />
            <Route
              path="integrations"
              element={
                <ViewerGuard forbiddenForRole4>
                  <IntegrationsPage />
                </ViewerGuard>
              }
            />
            <Route path="settings" element={<SettingsLayout />}>
              <Route index element={<SettingsIndexResolver />} />
              <Route
                path="profile"
                element={
                  <ViewerGuard forbiddenForRole4>
                    <ProfileSettingsPage />
                  </ViewerGuard>
                }
              />
              <Route
                path="terminals"
                element={
                  <ViewerGuard permission={PERMISSION_SETTINGS_TERMINALS_VIEW}>
                    <TerminalsSettingsPage />
                  </ViewerGuard>
                }
              />
              <Route
                path="users"
                element={
                  <ViewerGuard requireTenantAdmin>
                    <UserSettingsPage />
                  </ViewerGuard>
                }
              />
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
            <Route path="*" element={<DefaultRouteResolver />} />
          </Route>
        </Routes>
      </Suspense>
    </SessionProvider>
  );
}
