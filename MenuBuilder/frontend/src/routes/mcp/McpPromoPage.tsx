import { useEffect, useState } from "react";
import { Alert, Button, Card, Col, Form, Input, Modal, Row, Space, Table, Tag, Typography, message } from "antd";
import { CopyOutlined, DeleteOutlined, LineChartOutlined, PlusOutlined, RobotOutlined, SecurityScanOutlined, SendOutlined, ThunderboltOutlined } from "@ant-design/icons";
import { useSession } from "../../session/SessionContext";
import { createToken, listTokens, revokeToken, type TokenInfo } from "../../api/profile";
import { getMcpWaitlistStatus, joinMcpWaitlist } from "../../api/mcpWaitlist";

const { Paragraph, Text, Title } = Typography;

const IDEAS = [
  { key: "auto_triage", title: "Диагностика и восстановление", description: "Подсказки по причинам сбоев и проверенным шагам восстановления.", color: "#d48806", icon: <ThunderboltOutlined /> },
  { key: "log_telemetry_analysis", title: "Логи и телеметрия", description: "Поиск аномалий в журналах и показателях терминалов.", color: "#1677ff", icon: <LineChartOutlined /> },
  { key: "fleet_nlp_control", title: "Управление парком через диалог", description: "Типовые операции с терминалами через текстовые запросы.", color: "#389e0d", icon: <RobotOutlined /> },
  { key: "security_audit", title: "Аудит безопасности", description: "Проверка версий ПО, сертификатов и настроек устройств.", color: "#722ed1", icon: <SecurityScanOutlined /> },
];

type FeedbackValues = { contact_email?: string; note?: string };

export default function McpPage() {
  const { user } = useSession();
  const [tokens, setTokens] = useState<TokenInfo[]>([]);
  const [name, setName] = useState("L4mcp");
  const [created, setCreated] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [feedbackBusy, setFeedbackBusy] = useState(false);
  const [feedbackSent, setFeedbackSent] = useState(false);
  const [selectedIdea, setSelectedIdea] = useState(IDEAS[0].key);
  const [feedbackForm] = Form.useForm<FeedbackValues>();
  const allowed = [1, 3, 5].includes(user?.role_id ?? -1);
  const url = `${window.location.origin}/api/mcp/proxy`;

  const refresh = async () => {
    if (allowed) setTokens(await listTokens());
  };
  useEffect(() => { void refresh().catch(() => message.error("Не удалось загрузить токены")); }, [allowed]);
  useEffect(() => {
    if (!allowed) return;
    void getMcpWaitlistStatus().then(status => {
      if (!status.registered) return;
      if (IDEAS.some(idea => idea.key === status.use_case)) setSelectedIdea(status.use_case!);
      feedbackForm.setFieldsValue({ note: status.note ?? "" });
    }).catch(() => undefined);
  }, [allowed, feedbackForm]);

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

  const sendFeedback = async (values: FeedbackValues) => {
    setFeedbackBusy(true);
    try {
      await joinMcpWaitlist({
        use_case: selectedIdea,
        contact_email: values.contact_email?.trim() || undefined,
        note: values.note?.trim() || undefined,
      });
      setFeedbackSent(true);
      message.success("Пожелание отправлено");
    } catch {
      message.error("Не удалось отправить пожелание. Проверьте организацию и повторите попытку.");
    } finally {
      setFeedbackBusy(false);
    }
  };

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
        <Paragraph>Сохраните токен в переменной окружения <Text code>L4MCP_TOKEN</Text> на своей машине. В <Text code>bearer_token_env_var</Text> укажите только имя переменной, не сам токен. Не вставляйте токен в конфигурацию проекта или переписку.</Paragraph>
        <Text strong>Codex: ~/.codex/config.toml</Text>
        <pre style={{ whiteSpace: "pre-wrap", overflowWrap: "anywhere" }}>{`mcp_optional_startup_grace_ms = 0

[mcp_servers.l4mcp]
url = "${url}"
bearer_token_env_var = "L4MCP_TOKEN"
default_tools_approval_mode = "writes"`}</pre>
        <Text strong>Claude Code: .mcp.json</Text>
        <pre style={{ whiteSpace: "pre-wrap", overflowWrap: "anywhere" }}>{JSON.stringify({ mcpServers: { l4mcp: { type: "http", url, headers: { Authorization: "Bearer ${L4MCP_TOKEN}" } } } }, null, 2)}</pre>
        <Paragraph type="secondary">Для Codex настройка writes пропускает просмотр без подтверждения; выдача и отзыв PIN и выполнение команды требуют разрешения. Перезапустите агент после изменения настроек. Запрашивайте один терминал или ограниченный диапазон; PIN выдаётся максимум для пяти терминалов за вызов.</Paragraph>
      </Card>
      <Card title="Что добавить в L4mcp дальше" style={{ width: "100%", borderColor: "#d6e4ff", background: "linear-gradient(135deg, #f8fbff, #fff)" }}>
        <Paragraph type="secondary">Выберите интересное направление и отправьте пожелание. Эти идеи пока не входят в текущий набор инструментов MCP.</Paragraph>
        <Row gutter={[12, 12]} style={{ marginBottom: 20 }}>
          {IDEAS.map(idea => <Col xs={24} sm={12} key={idea.key}>
            <Card
              hoverable
              size="small"
              onClick={() => setSelectedIdea(idea.key)}
              style={{ height: "100%", borderColor: selectedIdea === idea.key ? idea.color : "#e6eaf0", background: selectedIdea === idea.key ? "#f0f7ff" : "#fff" }}
            >
              <Space style={{ display: "flex", flexWrap: "wrap", marginBottom: 8 }}><span style={{ color: idea.color, fontSize: 22 }}>{idea.icon}</span><Text strong>{idea.title}</Text><Tag color={selectedIdea === idea.key ? "blue" : "default"}>{selectedIdea === idea.key ? "Выбрано" : "Идея"}</Tag></Space>
              <Paragraph type="secondary" style={{ marginBottom: 0 }}>{idea.description}</Paragraph>
            </Card>
          </Col>)}
        </Row>
        <Form form={feedbackForm} layout="vertical" onFinish={sendFeedback} initialValues={{ contact_email: user?.username?.includes("@") ? user.username : "" }}>
          <Form.Item name="contact_email" label="Email для ответа" rules={[{ type: "email", message: "Введите корректный email" }]}>
            <Input autoComplete="email" maxLength={128} />
          </Form.Item>
          <Form.Item name="note" label="Пожелание" rules={[{ required: true, whitespace: true, message: "Опишите пожелание" }]}>
            <Input.TextArea rows={3} maxLength={500} showCount placeholder="Что вам нужно от MCP в работе с терминалами?" />
          </Form.Item>
          {feedbackSent && <Alert type="success" showIcon message="Пожелание сохранено. Его можно дополнить и отправить снова." style={{ marginBottom: 12 }} />}
          <Button type="primary" htmlType="submit" icon={<SendOutlined />} loading={feedbackBusy}>Отправить пожелание</Button>
        </Form>
      </Card>
    </Space>
  );
}
