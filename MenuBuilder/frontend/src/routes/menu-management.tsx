import { AppstoreOutlined, MobileOutlined } from "@ant-design/icons";
import SectionLayout from "../components/SectionLayout";

export default function MenuManagementLayout() {
  return (
    <SectionLayout
      title="Управление меню"
      items={[
        { key: "terminals", icon: <MobileOutlined />, label: "Терминалы" },
        { key: "variants", icon: <AppstoreOutlined />, label: "Меню" },
      ]}
      resolvePath={(key) => `/menu/${key}`}
      resolveKey={(pathname) =>
        pathname.startsWith("/menu/variants") ? "variants" : "terminals"
      }
    />
  );
}
