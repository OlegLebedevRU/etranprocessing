import {
  AppstoreOutlined,
  FolderOutlined,
  MobileOutlined,
  QuestionCircleOutlined,
} from "@ant-design/icons";
import SectionLayout from "../components/SectionLayout";

export default function MenuManagementLayout() {
  return (
    <SectionLayout
      title="Управление меню"
      items={[
        { key: "terminals", icon: <MobileOutlined />, label: "Терминалы" },
        { key: "variants", icon: <AppstoreOutlined />, label: "Меню" },
        { key: "catalog", icon: <FolderOutlined />, label: "Каталог" },
        { key: "help", icon: <QuestionCircleOutlined />, label: "Помощь" },
      ]}
      resolvePath={(key) => `/menu/${key}`}
      resolveKey={(pathname) => {
        if (pathname.startsWith("/menu/catalog")) return "catalog";
        if (pathname.startsWith("/menu/variants")) return "variants";
        if (pathname.startsWith("/menu/help")) return "help";
        return "terminals";
      }}
    />
  );
}
