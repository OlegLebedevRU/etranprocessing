import { useEffect, useState } from "react";
import { Alert, Button, Card, Input, Modal, Space, Table, Tag, Typography, message } from "antd";
import { CopyOutlined, DeleteOutlined, PlusOutlined } from "@ant-design/icons";
import { useSession } from "../../session/SessionContext";
import { createToken, listTokens, revokeToken, type TokenInfo } from "../../api/profile";

const { Paragraph, Text, Title } = Typography;

export default function McpPage() {
  const { user } = useSession();
  const [tokens, setTokens] = useState<TokenInfo[]>([]);
  const [name, setName] = useState("L4mcp");
  const [created, setCreated] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const allowed = [1, 3, 5].includes(user?.role_id ?? -1);
  const url = `${window.location.origin}/api/mcp/proxy`;

  const refresh = async () => {
    if (allowed) setTokens(await listTokens());
  };
  useEffect(() => { void refresh().catch(() => message.error("Не удалось загрузить токены")); }, [allowed]);

  const issue = async () => {
    setBusy(true);
    try {
      const result = await createToken(name.trim() || "L4mcp", 30);
      setCreated(result.token);
      await refresh();
    } catch { message.error("Не удалось создать токен. Выберите организацию и повторите попытку."); }
    finally { setBusy(false); }
  };

  const remove = (jti: string) => Modal.confirm({
    title: "Отозвать API-токен?",
    content: "Подключённый агент сразу потеряет доступ к L4mcp.",
    onOk: async () => { await revokeToken(jti); await refresh(); },
  });

  if (!allowed) return <Alert type="warning" message="L4mcp доступен ролям 1, 3 и 5." />;

  return (
    <Space direction="vertical" size="large" style={{ width: "100%", maxWidth: 900 }}>
      <Title level={3}>L4mcp</Title>
      <Paragraph>Подключите агента к отчётам, сертификатам и консоли своих терминалов. Для консоли назовите точный device_id: перед командой проверяются тег sys, статус терминала и svc_online службы l4con.</Paragraph>
      <Card title="API-токен" extra={<Button icon={<PlusOutlined />} type="primary" loading={busy} onClick={issue}>Создать на 30 дней</Button>}>
        <Space direction="vertical" style={{ width: "100%" }}>
          <Input value={name} onChange={e => setName(e.target.value)} maxLength={100} aria-label="Имя API-токена" />
          {created && <Alert type="success" showIcon message="Скопируйте токен сейчас: повторно он не показывается."
            description={<Space direction="vertical" style={{ width: "100%" }}>
              <Text code copyable={{ text: created }} style={{ overflowWrap: "anywhere" }}>{created}</Text>
              <Button icon={<CopyOutlined />} onClick={() => void navigator.clipboard.writeText(created)}>Скопировать</Button>
              <Button onClick={() => setCreated(null)}>Скрыть</Button>
            </Space>} />}
          <Table size="small" pagination={false} rowKey="jti" scroll={{ x: 500 }} dataSource={tokens} columns={[
            { title: "Имя", dataIndex: "name" },
            { title: "Истекает", dataIndex: "expires_at", render: (v: string) => new Date(v).toLocaleDateString("ru-RU") },
            { title: "Статус", render: (_: unknown, t: TokenInfo) => <Tag color={t.revoked_at ? "red" : "green"}>{t.revoked_at ? "Отозван" : "Активен"}</Tag> },
            { title: "", render: (_: unknown, t: TokenInfo) => t.revoked_at ? null : <Button danger icon={<DeleteOutlined />} onClick={() => remove(t.jti)}>Отозвать</Button> },
          ]} />
        </Space>
      </Card>
      <Card title="Подключение к агенту">
        <Paragraph>Сохраните токен в переменной окружения <Text code>L4MCP_TOKEN</Text> на своей машине. Не вставляйте его в конфигурацию проекта или переписку.</Paragraph>
        <Text strong>Codex: ~/.codex/config.toml</Text>
        <pre>{`[mcp_servers.l4mcp]\nurl = "${url}"\nbearer_token_env_var = "L4MCP_TOKEN"`}</pre>
        <Text strong>Claude Code: .mcp.json</Text>
        <pre>{JSON.stringify({ mcpServers: { l4mcp: { type: "http", url, headers: { Authorization: "Bearer ${L4MCP_TOKEN}" } } } }, null, 2)}</pre>
        <Paragraph type="secondary">Перезапустите агент после изменения настроек. Запрашивайте один терминал или ограниченный диапазон; PIN выдаётся максимум для пяти терминалов за вызов.</Paragraph>
      </Card>
    </Space>
  );
}
