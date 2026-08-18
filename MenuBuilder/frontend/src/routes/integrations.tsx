import { KeyOutlined } from "@ant-design/icons";
import SectionLayout from "../components/SectionLayout";

export default function IntegrationsLayout() {
  return (
    <SectionLayout
      title="Интеграции"
      items={[{ key: "tokens", icon: <KeyOutlined />, label: "API-токены" }]}
      resolvePath={(key) => `/integrations/${key}`}
      resolveKey={() => "tokens"}
    />
  );
}
