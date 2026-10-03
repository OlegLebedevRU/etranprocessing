import { useCallback, useEffect, useMemo, useState } from "react";
import {
  Alert,
  Button,
  Card,
  Empty,
  Modal,
  Select,
  Space,
  Spin,
  Table,
  Tabs,
  Tag,
  Typography,
  message,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import { useSession } from "../../session/SessionContext";
import {
  checkOrder,
  createOrder,
  getOrders,
  getQuote,
  getSubscriptions,
  getUsage,
  type Order,
  type Quote,
  type Subscription,
  type SubscriptionSummary,
  type UsageDay,
} from "../../api/subscriptions";

import {
  canPurchase,
  duration,
  subscriptionLabels as labels,
  usageByDay,
  usageTotals,
} from "../../utils/subscriptionPresentation";

const money = (kopecks: number) =>
  new Intl.NumberFormat("ru-RU", {
    style: "currency",
    currency: "RUB",
    maximumFractionDigits: 0,
  }).format(kopecks / 100);
const date = (at: string | null) =>
  at
    ? new Date(at).toLocaleString("ru-RU", {
        day: "numeric",
        month: "long",
        year: "numeric",
        hour: "2-digit",
        minute: "2-digit",
      })
    : "—";

const orderLabels: Record<Order["status"], string> = {
  pending: "Платёж проверяется",
  waiting_for_capture: "Платёж проверяется",
  succeeded: "Оплачен",
  canceled: "Отменён",
};

function TerminalSubscriptionsPage() {
  const [summary, setSummary] = useState<SubscriptionSummary | null>(null);
  const [orders, setOrders] = useState<Order[]>([]);
  const [usage, setUsage] = useState<UsageDay[]>([]);
  const [usageDays, setUsageDays] = useState(30);
  const [usageTerminal, setUsageTerminal] = useState<number | undefined>();
  const [usageError, setUsageError] = useState("");
  const totals = useMemo(() => usageTotals(usage), [usage]);
  const daily = useMemo(() => usageByDay(usage), [usage]);
  const maxDaily = Math.max(1, ...daily.map((d) => d.seconds));
  const [selected, setSelected] = useState<number[]>([]);
  const [months, setMonths] = useState(1);
  const [quote, setQuote] = useState<Quote | null>(null);
  const [operationId, setOperationId] = useState<string | null>(null);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const load = useCallback(async () => {
    try {
      const [s, o] = await Promise.all([getSubscriptions(), getOrders()]);
      setSummary(s);
      setOrders(o);
      setError("");
    } catch {
      setError(
        "Не удалось загрузить подписки. Обновите страницу или повторите позже.",
      );
    }
  }, []);
  useEffect(() => {
    void load();
  }, [load]);
  const refreshPayment = async (id: number) => {
    setBusy(true);
    try {
      const o = await checkOrder(id);
      message.info(orderLabels[o.status]);
      await load();
    } catch {
      message.warning(
        "Платёж пока не подтверждён. Повторите проверку этого же заказа позже.",
      );
    } finally {
      setBusy(false);
    }
  };
  useEffect(() => {
    if (
      !orders.some(
        (o) => o.status === "pending" || o.status === "waiting_for_capture",
      )
    )
      return;
    const timer = window.setInterval(() => {
      void load();
    }, 15000);
    return () => window.clearInterval(timer);
  }, [orders, load]);
  const review = async (targets = selected) => {
    setBusy(true);
    try {
      setQuote(
        await getQuote(targets.map((terminal_id) => ({ terminal_id, months }))),
      );
      setOperationId(crypto.randomUUID());
    } catch {
      message.error(
        "Не удалось подготовить корзину. Обновите данные и попробуйте снова.",
      );
    } finally {
      setBusy(false);
    }
  };
  const pay = async () => {
    if (!quote || !operationId) return;
    setBusy(true);
    try {
      const order = await createOrder(
        quote.snapshot.items,
        quote.quote_hash,
        operationId,
      );
      await load();
      if (
        order.confirmation_url &&
        (order.status === "pending" || order.status === "waiting_for_capture")
      )
        window.location.assign(order.confirmation_url);
      else {
        message.info(orderLabels[order.status]);
        setQuote(null);
        setOperationId(null);
      }
    } catch {
      message.warning(
        "Заказ мог быть создан. Проверьте историю платежей; повтор этой корзины использует тот же номер заказа.",
      );
      await load();
    } finally {
      setBusy(false);
    }
  };
  useEffect(() => {
    let active = true;
    const end = new Date();
    const start = new Date(end);
    start.setDate(start.getDate() - usageDays + 1);
    void getUsage({
      start: start.toISOString().slice(0, 10),
      end: end.toISOString().slice(0, 10),
      terminal_id: usageTerminal,
    })
      .then((rows) => {
        if (active) {
          setUsage(rows);
          setUsageError("");
        }
      })
      .catch(() => {
        if (active)
          setUsageError("Не удалось загрузить статистику за выбранный период.");
      });
    return () => {
      active = false;
    };
  }, [usageDays, usageTerminal]);
  const columns: ColumnsType<Subscription> = [
    { title: "Терминал", dataIndex: "name" },
    {
      title: "Статус",
      render: (_, t) => (
        <Space direction="vertical">
          <Tag
            color={
              t.allowed ? (t.state === "grace" ? "orange" : "green") : "default"
            }
          >
            {labels[t.state]}
          </Tag>
          <Typography.Text type="secondary">{t.reason}</Typography.Text>
        </Space>
      ),
    },
    {
      title: "Срок",
      render: (_, t) =>
        t.is_free ? (
          "Без ограничения времени"
        ) : (
          <>
            <div>Оплачен до {date(t.paid_until)}</div>
            {t.state === "grace" && (
              <div>Продлите до {date(t.grace_until)}</div>
            )}
          </>
        ),
    },
    {
      title: "Действие",
      render: (_, t) =>
        t.is_free ? (
          "Оплата не нужна"
        ) : (
          <Button
            aria-label={t.action || "Оплата пока недоступна"}
            disabled={busy || !canPurchase(t, !!summary?.payments_enabled)}
            loading={busy}
            onClick={() => {
              setSelected([t.terminal_id]);
              void review([t.terminal_id]);
            }}
          >
            {t.action || "Оплата пока недоступна"}
          </Button>
        ),
    },
  ];
  if (error)
    return (
      <Alert
        type="error"
        title={error}
        action={<Button onClick={() => void load()}>Повторить</Button>}
      />
    );
  if (!summary) return <Spin />;
  const urgent = summary.items.filter(
    (t) => t.state === "grace" || t.state === "expired",
  );
  const subscriptionContent = (
    <Space orientation="vertical" style={{ width: "100%" }} size="large">
      <Alert
        type={summary.payments_enabled ? "info" : "warning"}
        title={
          summary.payments_enabled
            ? `Один терминал бесплатно. Каждый дополнительный — ${money(summary.month_price_kopecks)} в месяц.`
            : "Сейчас доступен один бесплатный терминал. Ещё два можно подготовить, но подключить их получится после запуска оплаты."
        }
        description="Время использования не влияет на стоимость. Автосписаний нет: подписку продлеваете вы."
      />
      {urgent.length > 0 && (
        <Alert
          type="warning"
          title={`Требуют внимания: ${urgent.map((t) => t.name).join(", ")}`}
          description="Выберите терминалы ниже и продлите подписку, чтобы сохранить или восстановить доступ."
        />
      )}
      <Table
        rowKey="terminal_id"
        columns={columns}
        dataSource={summary.items}
        scroll={{ x: 720 }}
        locale={{
          emptyText: (
            <Empty description="Создайте первый терминал — он будет бесплатным" />
          ),
        }}
        rowSelection={{
          selectedRowKeys: selected,
          onChange: (keys) => {
            setSelected(keys.map(Number));
            setQuote(null);
            setOperationId(null);
          },
          getCheckboxProps: (t) => ({
            disabled: !canPurchase(t, summary.payments_enabled),
          }),
        }}
      />
      <Card title="Продление выбранных терминалов">
        <Space wrap>
          <Select
            value={months}
            onChange={(value) => {
              setMonths(value);
              setQuote(null);
              setOperationId(null);
            }}
            options={[1, 3, 6, 12].map((value) => ({
              value,
              label: `${value} мес.`,
            }))}
          />
          <Button
            type="primary"
            disabled={!summary.payments_enabled || selected.length === 0}
            loading={busy}
            onClick={() => void review()}
          >
            {summary.payments_enabled
              ? "Посмотреть стоимость"
              : "Оплата пока недоступна"}
          </Button>
        </Space>
      </Card>
    </Space>
  );
  const historyContent = (
    <Table
      rowKey="id"
      dataSource={orders}
      scroll={{ x: 560 }}
      columns={[
        { title: "Дата", render: (_, o) => date(o.created_at) },
        {
          title: "Назначение",
          render: (_, o) =>
            o.items
              .map(
                (i) =>
                  `${summary.items.find((t) => t.terminal_id === i.terminal_id)?.name || `№${i.terminal_id}`}: ${i.months} мес.`,
              )
              .join(", "),
        },
        { title: "Сумма", render: (_, o) => money(o.amount_kopecks) },
        { title: "Результат", render: (_, o) => orderLabels[o.status] },
        {
          title: "Действие",
          render: (_, o) =>
            o.status === "pending" || o.status === "waiting_for_capture" ? (
              <Space wrap>
                {o.confirmation_url && summary.payments_enabled && (
                  <Button href={o.confirmation_url}>Продолжить оплату</Button>
                )}
                <Button
                  loading={busy}
                  onClick={() => void refreshPayment(o.id)}
                >
                  Проверить платёж
                </Button>
              </Space>
            ) : (
              "—"
            ),
        },
      ]}
    />
  );
  const usageContent = (
    <Space orientation="vertical" style={{ width: "100%" }}>
      <Alert
        type="info"
        title="Использование ресурсов"
        description="Подтверждённое время видео и консоли. Эти показатели помогают планировать нагрузку и не влияют на оплату. Длительности сессий суммируются; это не измерение CPU или сетевого трафика."
      />
      <Space wrap>
        <Select
          aria-label="Период статистики"
          value={usageDays}
          onChange={setUsageDays}
          options={[7, 30, 90].map((value) => ({
            value,
            label: `Последние ${value} дней`,
          }))}
        />
        <Select
          aria-label="Терминал статистики"
          style={{ minWidth: 220 }}
          allowClear
          placeholder="Все терминалы"
          value={usageTerminal}
          onChange={setUsageTerminal}
          options={summary.items.map((t) => ({
            value: t.terminal_id,
            label: t.name,
          }))}
        />
      </Space>
      {usageError && <Alert type="error" title={usageError} />}
      <Card>
        <Space wrap size="large">
          <Typography.Text>Видео: {duration(totals.video)}</Typography.Text>
          <Typography.Text>Консоль: {duration(totals.console)}</Typography.Text>
          <Typography.Text strong>
            Всего: {duration(totals.video + totals.console)}
          </Typography.Text>
        </Space>
      </Card>
      <div role="img" aria-label="Длительность использования по дням">
        {daily.map((day) => (
          <div
            key={day.date}
            style={{
              display: "flex",
              alignItems: "center",
              gap: 12,
              marginBottom: 4,
            }}
          >
            <span style={{ minWidth: 90 }}>{day.date}</span>
            <div
              style={{
                width: `${(day.seconds / maxDaily) * 65}%`,
                minWidth: 2,
                height: 12,
                background: "#1677ff",
                borderRadius: 3,
              }}
            />
            <span>{duration(day.seconds)}</span>
          </div>
        ))}
      </div>
      <Table
        rowKey={(d) => `${d.terminal_id}:${d.date}`}
        dataSource={usage}
        columns={[
          { title: "Дата", dataIndex: "date" },
          {
            title: "Терминал",
            render: (_, d) =>
              summary.items.find((t) => t.terminal_id === d.terminal_id)
                ?.name || String(d.terminal_id),
          },
          { title: "Видео", render: (_, d) => duration(d.video_seconds) },
          { title: "Консоль", render: (_, d) => duration(d.console_seconds) },
          {
            title: "Всего",
            render: (_, d) => duration(d.video_seconds + d.console_seconds),
          },
        ]}
      />
    </Space>
  );
  return (
    <Space orientation="vertical" style={{ width: "100%" }} size="large">
      <Typography.Title level={2}>Подписки</Typography.Title>
      <Button onClick={() => void load()}>Обновить</Button>
      <Tabs
        items={[
          {
            key: "subscriptions",
            label: "Подписки",
            children: subscriptionContent,
          },
          { key: "history", label: "Платежи", children: historyContent },
          { key: "usage", label: "Использование", children: usageContent },
        ]}
      />
      <Modal
        title="Проверьте покупку"
        open={quote !== null}
        onCancel={() => {
          if (!busy) setQuote(null);
        }}
        footer={
          <Button
            type="primary"
            loading={busy}
            aria-label={`Оплатить ${money(quote?.snapshot.amount_kopecks || 0)}`}
            disabled={busy || !quote?.available}
            onClick={() => void pay()}
          >
            Оплатить {money(quote?.snapshot.amount_kopecks || 0)}
          </Button>
        }
      >
        <p>Пакет: {months} мес. для каждого выбранного терминала.</p>
        {quote?.preview.map((item) => (
          <p key={item.terminal_id}>
            {
              summary.items.find((t) => t.terminal_id === item.terminal_id)
                ?.name
            }
            : новый срок до {date(item.paid_until)}
          </p>
        ))}
        <p>
          Всего: {money(quote?.snapshot.amount_kopecks || 0)}. Автосписаний нет.
        </p>
      </Modal>
    </Space>
  );
}

export default function LicensesPage() {
  const { user } = useSession();
  // Changing tenants discards the previous cart, quote and all pending UI responses.
  return <TerminalSubscriptionsPage key={`${user?.org_id}:${user?.role_id}`} />;
}
