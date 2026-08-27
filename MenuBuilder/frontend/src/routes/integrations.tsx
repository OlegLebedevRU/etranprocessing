import { ApiOutlined, KeyOutlined } from "@ant-design/icons";
import SectionLayout from "../components/SectionLayout";

export default function IntegrationsLayout() {
  return (
    <SectionLayout
      title="Интеграции"
      items={[
        {
          key: "api-connection",
          icon: <ApiOutlined />,
          label: "Подключение АПИ",
        },
        { key: "tokens", icon: <KeyOutlined />, label: "API-токены" },
      ]}
      resolvePath={(key) => `/integrations/${key}`}
      resolveKey={(pathname) =>
        pathname.includes("/integrations/tokens") ? "tokens" : "api-connection"
      }
    />
  );
}
