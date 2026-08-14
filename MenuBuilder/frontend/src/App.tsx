import { Routes, Route, Navigate } from "react-router";
import AppLayout from "./routes/layout";
import DashboardPage from "./routes/dashboard";
import GroupsPage from "./routes/groups";
import ServicesPage from "./routes/services";
import TerminalsPage from "./routes/terminals";

export default function App() {
  return (
    <Routes>
      <Route element={<AppLayout />}>
        <Route index element={<DashboardPage />} />
        <Route path="groups" element={<GroupsPage />} />
        <Route path="groups/:groupId" element={<ServicesPage />} />
        <Route path="terminals" element={<TerminalsPage />} />
        <Route path="*" element={<Navigate to="/" replace />} />
      </Route>
    </Routes>
  );
}
