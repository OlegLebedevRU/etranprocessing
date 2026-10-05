import { useCallback, useEffect, useRef, useState, type Key } from "react";
import { Alert, Badge, Breadcrumb, Button, Card, Col, Empty, Input, Modal, Row, Space, Spin, Table, Tag, Tree, Typography, Upload } from "antd";
import { ArrowLeftOutlined, ArrowRightOutlined, ArrowUpOutlined, DesktopOutlined, DownloadOutlined, FolderOutlined, HddOutlined, HomeOutlined, ReloadOutlined, UploadOutlined } from "@ant-design/icons";
import { useSession } from "../../session/SessionContext";
import { getDevices, type DeviceListItem } from "../../api/devices";
import { listTerminalsSettings } from "../../api/settings";
import { selectableDevices } from "../../utils/terminalPresentation";
import { fmApi, fmError, fmRetryAfter, fmReason, fmStorage, MAX_FM_BYTES, type FmEntry, type FmOperation, type FmReadiness } from "../../api/fileManager";
import { fmJoin, fmParent, fmWithinRoot } from "./navigation";
import { chooseDownloadTarget, saveVerifiedDownload } from "./saveDownload";
import "./files.css";

const { Text, Title } = Typography;
type Active = { device: number; lease: string; abort: AbortController; operation?: string; expiresAt: number };
type Transfer = { name: string; phase: string; closing: boolean; result?: string };

function digest(file: Blob, signal: AbortSignal): Promise<string> {
  return new Promise((resolve, reject) => {
    const worker = new Worker(new URL("./hash.worker.ts", import.meta.url), { type: "module" });
    const cleanup = () => { worker.terminate(); signal.removeEventListener("abort", abort); };
    const abort = () => { cleanup(); reject(new Error("Cancelled")); };
    if (signal.aborted) { abort(); return; }
    signal.addEventListener("abort", abort, { once: true });
    worker.onerror = () => { cleanup(); reject(new Error("Checksum failed")); };
    worker.onmessage = ({ data }: MessageEvent<{ sha256?: string }>) => {
      cleanup(); if (data.sha256) resolve(data.sha256); else reject(new Error("Checksum failed"));
    };
    worker.postMessage({ file });
  });
}

async function delay(signal: AbortSignal, ms = 700) {
  await new Promise<void>((resolve, reject) => {
    const abort = () => { clearTimeout(timer); reject(new Error("Cancelled")); };
    const timer = setTimeout(() => { signal.removeEventListener("abort", abort); resolve(); }, ms);
    if (signal.aborted) { abort(); return; }
    signal.addEventListener("abort", abort, { once: true });
  });
}

async function wait(api: ReturnType<typeof fmApi>, id: string, signal: AbortSignal, expected: string, timeout = 45000): Promise<FmOperation> {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    signal.throwIfAborted();
    const result = await api.status(id);
    if (result.state === expected) return result;
    if (["failed", "cancelled"].includes(result.state)) throw new Error(result.error_code || "fm_operation_failed");
    await delay(signal);
  }
  throw new Error("fm_ack_timeout");
}

export default function FilesPage() {
  const { user } = useSession();
  if (!user || ![1, 2, 3, 5].includes(user.role_id ?? 0)) return <Alert type="warning" title="Для файлового менеджера нужны права управления терминалом." />;
  if (typeof user.org_id !== "number" || user.org_id <= 0) return <Alert type="info" title="Выберите организацию для работы с файлами терминала." />;
  return <TenantFiles key={user.org_id} org={user.org_id} />;
}

function TenantFiles({ org }: { org: number }) {
  const [devices, setDevices] = useState<(DeviceListItem & { terminalAddress?: string })[]>([]);
  const [availability, setAvailability] = useState<Record<number, FmReadiness>>({});
  const [device, setDevice] = useState<number>();
  const [roots, setRoots] = useState<Record<number, string[]>>({});
  const [expanded, setExpanded] = useState<Key[]>([]);
  const [path, setPath] = useState("");
  const [address, setAddress] = useState("");
  const [entries, setEntries] = useState<FmEntry[]>([]);
  const [offset, setOffset] = useState(0);
  const [more, setMore] = useState(false);
  const [search, setSearch] = useState("");
  const [busy, setBusy] = useState(false);
  const [loading, setLoading] = useState(true);
  const [connected, setConnected] = useState(false);
  const [phase, setPhase] = useState("");
  const [error, setError] = useState("");
  const [notice, setNotice] = useState("");
  const [transfer, setTransfer] = useState<Transfer>();
  const [selectedFile, setSelectedFile] = useState<string>();
  const [history, setHistory] = useState<string[]>([""]);
  const [historyIndex, setHistoryIndex] = useState(0);
  const [drains, setDrains] = useState<Record<number, number>>({});
  const [now, setNow] = useState(Date.now());
  const view = useRef(crypto.randomUUID());
  const current = useRef<Active | undefined>(undefined);
  const mounted = useRef(true);
  const locked = useRef(false);
  const generation = useRef(0);
  const closing = useRef<Promise<void> | undefined>(undefined);
  const transferRef = useRef(false);
  const lifetime = useRef(new AbortController());
  const deadlines = useRef<Record<number, number>>({});
  const refreshDevices = useRef<() => Promise<void>>(async () => {});

  const rememberDrain = useCallback((id: number, until: number) => {
    deadlines.current[id] = Math.max(deadlines.current[id] || 0, until);
    if (mounted.current) setDrains({ ...deadlines.current });
  }, []);

  // Single teardown owner. Cancel already closes the lease; do not send a second stop.
  const close = useCallback((message = ""): Promise<void> => {
    if (closing.current) return closing.current;
    const active = current.current;
    current.current = undefined;
    generation.current++;
    active?.abort.abort();
    if (mounted.current) { setConnected(false); setEntries([]); setPath(""); setAddress(""); if (message) setError(message); }
    const work = async () => {
      if (!active) return;
      let until = Math.max(active.expiresAt + 5000, Date.now());
      rememberDrain(active.device, until);
      if (mounted.current) setPhase("Завершаем предыдущий сеанс…");
      try {
        const stopped = await fmApi(active.device, view.current).signal(active.lease, active.operation ? "cancel" : "stop", active.operation);
        if (stopped.retry_after_sec !== undefined) {
          until = Date.now() + stopped.retry_after_sec * 1000;
          deadlines.current[active.device] = until;
          if (mounted.current) setDrains({ ...deadlines.current });
        }
      } catch {
        // Conservative lease bound remains authoritative when delivery is unknown.
        if (mounted.current) setNotice("Подтверждение остановки не получено. Ожидаем безопасного истечения аренды.");
      }
      rememberDrain(active.device, until);
      while (mounted.current && Date.now() < until) {
        if (mounted.current) setPhase(`Завершаем сеанс: ${Math.ceil((until - Date.now()) / 1000)} с`);
        try { await delay(lifetime.current.signal, Math.min(500, until - Date.now())); } catch { break; }
      }
    };
    const promise = work().finally(() => { if (closing.current === promise) closing.current = undefined; });
    closing.current = promise;
    return promise;
  }, [rememberDrain]);

  const setPending = (value: boolean) => { locked.current = value; if (mounted.current) setBusy(value); };
  const fail = async (failure: unknown) => {
    const message = fmError(failure);
    if (mounted.current && transferRef.current) setTransfer(t => t && { ...t, closing: true, phase: "Останавливаем операцию…" });
    await close(message);
    if (mounted.current) {
      setDevice(undefined);
      setTransfer(t => t && { ...t, closing: false, result: message, phase: "Операция завершена с ошибкой" });
    }
  };

  useEffect(() => {
    mounted.current = true; lifetime.current = new AbortController();
    const controller = lifetime.current;
    let refreshing = false;
    const load = async () => {
      if (refreshing) return; refreshing = true;
      try {
        const [first, terminals] = await Promise.all([getDevices(org, { page: 1, size: 100 }), listTerminalsSettings({ org_id: org, all: true })]);
        const items = [...first.items];
        for (let page = 2; page <= first.pages; page++) items.push(...(await getDevices(org, { page, size: 100 })).items);
        const online = selectableDevices(items, terminals.items).filter(item => item.status === "online");
        if (controller.signal.aborted) return;
        setDevices(online.map(item => ({ ...item, terminalAddress: terminals.items.find(terminal => terminal.device_id === item.device_id)?.address || undefined }))); setLoading(false);
        // Bounded concurrency; readiness inspection never acquires a session.
        let index = 0;
        await Promise.all(Array.from({ length: Math.min(4, online.length) }, async () => {
          while (index < online.length && !controller.signal.aborted) {
            const id = online[index++].device_id;
            try {
              const value = await fmApi(id, view.current, controller.signal).readiness();
              if (!controller.signal.aborted) setAvailability(old => ({ ...old, [id]: value }));
            } catch { if (!controller.signal.aborted) setAvailability(old => { const next = { ...old }; delete next[id]; return next; }); }
          }
        }));
      } catch { if (!controller.signal.aborted) { setError("Не удалось обновить список терминалов."); setLoading(false); } }
      finally { refreshing = false; }
    };
    refreshDevices.current = load;
    void load(); const timer = setInterval(() => void load(), 15000);
    const tick = setInterval(() => setNow(Date.now()), 1000);
    const unload = (event: BeforeUnloadEvent) => { if (current.current || locked.current || transferRef.current) { event.preventDefault(); event.returnValue = ""; } };
    window.addEventListener("beforeunload", unload);
    return () => { mounted.current = false; controller.abort(); clearInterval(timer); clearInterval(tick); window.removeEventListener("beforeunload", unload); void close(); };
  }, [org, close]);

  useEffect(() => {
    if (!connected) return;
    let running = false;
    const timer = setInterval(async () => {
      const active = current.current;
      if (!active || running) return; running = true;
      try {
        const api = fmApi(active.device, view.current, active.abort.signal);
        // The server may apply renew even if its response is lost. Keep the worst-case bound.
        active.expiresAt = Math.max(active.expiresAt, Date.now() + 90000);
        const renewed = await api.signal(active.lease, "renew");
        const deadline = Date.now() + 6000;
        let applied = false;
        while (current.current === active && Date.now() < deadline) {
          const state = await api.status(active.lease);
          if (state.applied_expires_at && Date.parse(state.applied_expires_at) >= Date.parse(renewed.expires_at)) { applied = true; break; }
          await delay(active.abort.signal, 500);
        }
        if (current.current === active && !applied) throw new Error("fm_ack_timeout");
      } catch (failure) {
        if (current.current === active) {
          setPending(true); await fail(failure); setPending(false);
        }
      } finally { running = false; }
    }, 15000);
    return () => clearInterval(timer);
  }, [connected, close]); // Active identity fences every asynchronous callback.

  const list = async (active: Active, target: string, pageOffset = 0) => {
    const api = fmApi(active.device, view.current, active.abort.signal);
    const result = await api.navigate(active.lease, target, pageOffset);
    if (result.state !== "completed") throw new Error(result.error_code || "fm_path_or_session_failed");
    if (current.current !== active) throw new Error("Cancelled");
    active.operation = undefined;
    setEntries(result.entries); setPath(target); setAddress(target); setOffset(pageOffset); setMore(result.has_more === true);
    setPhase("Каталог получен");
  };

  const browse = async (target: string, pageOffset = 0, historyTarget?: number) => {
    const active = current.current;
    if (!active || locked.current || transferRef.current) return;
    if (target && !fmWithinRoot(target, roots[active.device] || [])) { setError("Путь вне доступных каталогов терминала."); return; }
    setPending(true); setError(""); setSelectedFile(undefined);
    try {
      if (target) await list(active, target, pageOffset);
      else { setPath(""); setAddress(""); setEntries([]); }
      if (historyTarget !== undefined) setHistoryIndex(historyTarget);
      else if (target !== path) { const next = [...history.slice(0, historyIndex + 1), target]; setHistory(next); setHistoryIndex(next.length - 1); }
    } catch (failure) { if (current.current === active) await fail(failure); }
    finally { setPending(false); }
  };

  const select = async (id?: number, target = "") => {
    if (locked.current || transferRef.current) return;
    if (id !== undefined && id === current.current?.device) { await browse(target); return; }
    if (id && (deadlines.current[id] || 0) > Date.now()) return;
    setPending(true); setError(""); setNotice("");
    try {
      await close();
      if (!mounted.current) return;
      setDevice(id); setHistory([""]); setHistoryIndex(0); setSelectedFile(undefined);
      if (!id) { setPhase("Обновляем онлайн-терминалы…"); await refreshDevices.current(); if(mounted.current)setPhase(""); return; }
      const api = fmApi(id, view.current, lifetime.current.signal);
      setPhase("Проверяем доступность агента…");
      const ready = await api.readiness(); setAvailability(old => ({ ...old, [id]: ready }));
      if (!ready.available) { setError(fmReason[ready.state] || "Агент недоступен"); return; }
      setPhase("Подключаемся к терминалу…");
      const session = await api.start();
      const active: Active = { device: id, lease: session.lease_id, abort: new AbortController(), expiresAt: Date.now() + 90000 };
      if (!mounted.current) { void fmApi(id, view.current).signal(session.lease_id, "stop").catch(() => {}); return; }
      current.current = active;
      const status = await wait(fmApi(id, view.current, active.abort.signal), active.lease, active.abort.signal, "active", 10000);
      if (current.current !== active) return;
      const available = status.roots || [];
      setRoots(old => ({ ...old, [id]: available })); setConnected(true);
      setExpanded(old => old.includes(`t:${id}`) ? old : [...old, `t:${id}`]);
      setPhase("Сеанс активен. Выберите диск или доступный каталог.");
      if (target) {
        if (!fmWithinRoot(target, available)) throw new Error("fm_path_denied");
        await list(active, target); setHistory(["", target]); setHistoryIndex(1);
      }
    } catch (failure) {
      if (id) rememberDrain(id, Date.now() + fmRetryAfter(failure) * 1000);
      if (mounted.current) await fail(failure);
    } finally { setPending(false); }
  };

  const transferFile = async (kind: "upload" | "download", target: string, file?: File) => {
    const active = current.current;
    if (!active || locked.current || transferRef.current) return;
    if (file && file.size > MAX_FM_BYTES) { setError("Размер файла превышает 64 МиБ."); return; }
    transferRef.current = true; setPending(true); setError(""); setNotice("");
    setTransfer({ name: file?.name || target.split("\\").pop() || target, phase: "Подготовка и проверка файла…", closing: false });
    const step = (value: string) => { if (current.current === active) setTransfer(old => old && { ...old, phase: value }); };
    try {
      const name = target.split("\\").pop() || "download";
      const destination = kind === "download" ? await chooseDownloadTarget(name) : undefined;
      if (current.current !== active) return;
      active.abort.signal.throwIfAborted();
      if (destination === null) {
        setTransfer(undefined); transferRef.current = false; setPhase("Скачивание отменено");
        return;
      }
      const signal = AbortSignal.any([active.abort.signal, AbortSignal.timeout(90000)]);
      const api = fmApi(active.device, view.current, signal);
      const operation = await api.create(active.lease, kind, target); active.operation = operation.id;
      if (kind === "upload" && file) {
        const sha = await digest(file, signal); const grant = await api.manifest(operation.id, file.size, sha);
        step("Загрузка в хранилище…"); await fmStorage(grant, signal, file); await api.sourceComplete(operation.id);
        step("Терминал получает, проверяет и записывает файл…"); await wait(api, operation.id, signal, "completed");
        active.operation = undefined;
        if (current.current !== active) return;
        step("Файл записан. Обновляем каталог…");
        try { await list(active, path); setSelectedFile(file.name); }
        catch { await close(); if (mounted.current) setNotice("Файл записан, но список не обновлён. Сеанс завершён; файл повторно загружать не нужно."); }
      } else {
        const source = await wait(api, operation.id, signal, "verifying"); const grant = await api.download(operation.id);
        step("Получение и проверка файла…");
        const response = await fmStorage(grant, signal); const reader = response.body?.getReader();
        if (!reader) throw new Error("fm_integrity_failed");
        const chunks: Uint8Array<ArrayBuffer>[] = []; let size = 0;
        for (;;) {
          const { done, value } = await reader.read(); if (done) break;
          size += value.byteLength;
          if (size > MAX_FM_BYTES || size > (source.size_bytes ?? 0)) { await reader.cancel(); throw new Error("fm_integrity_failed"); }
          chunks.push(new Uint8Array(value));
        }
        const blob = new Blob(chunks); const sha = await digest(blob, signal);
        if (size !== source.size_bytes || sha !== source.sha256) throw new Error("fm_integrity_failed");
        await api.received(operation.id, size, sha);
        if (current.current !== active) return;
        active.operation = undefined;
        step("Сохранение проверенного файла…");
        try { await saveVerifiedDownload(blob, name, destination, signal); }
        catch {
          if (current.current === active) setTransfer(old => old && { ...old, result: "Файл получен и проверен, но сохранить его не удалось. Проверьте доступ к выбранной папке и свободное место; затем повторите скачивание." });
          return;
        }
      }
      if (mounted.current && current.current === active) { setTransfer(undefined); transferRef.current = false; setPhase("Файл проверен и передан"); }
    } catch (failure) { if (current.current === active) await fail(failure); }
    finally { setPending(false); }
  };

  const cancelTransfer = async () => {
    if (closing.current || transfer?.closing || transfer?.result) return;
    setTransfer(old => old && { ...old, closing: true, phase: "Останавливаем операцию…" });
    setPending(true);
    await close("Операция отменена. Если запись уже началась, её результат нужно проверить после восстановления сеанса.");
    if (mounted.current) setTransfer(old => old && { ...old, closing: false, result: "Сеанс завершён. При неопределённом результате записи новая передача будет недоступна до проверки состояния." });
    setPending(false);
  };

  const selectedRoot = (device && roots[device]?.find(root => fmWithinRoot(path, [root]))) || "";
  const treeData = [{ key: "all", title: "Обзор парка", icon: <HomeOutlined />, isLeaf: true }, ...devices.filter(item => `${item.device_id} ${item.description || ""} ${item.terminalAddress || ""}`.toLowerCase().includes(search.toLowerCase())).map(item => {
    const ready = availability[item.device_id];
    const remaining = Math.max(0, Math.ceil(((drains[item.device_id] || 0) - now) / 1000));
    const status = remaining ? `Завершение ${remaining} с` : ready ? (fmReason[ready.state] || ready.state) : "Проверка…";
    const usable = ready?.available && ready.compatible && ready.mqtt_available && !remaining;
    return {
      key: `t:${item.device_id}`, icon: <DesktopOutlined />, isLeaf: false,
      title: <span style={{ display: "block", maxWidth: "100%" }}>
        <span title={status}><strong>{item.device_id}</strong> <span role="img" aria-label={status}><Badge status={usable ? "success" : remaining || !ready ? "processing" : "warning"} /></span></span>
        {item.terminalAddress && <Text type="secondary" title={item.terminalAddress} style={{ display: "block", paddingLeft: 12, fontSize: 11, lineHeight: "16px", overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}>{item.terminalAddress}</Text>}
        {!usable && <Text type="secondary" style={{ display: "block", fontSize: 11 }}>{status}</Text>}
      </span>,
      disabled: remaining > 0,
      children: roots[item.device_id]?.map(root => ({ key: `d:${item.device_id}:${root}`, title: root, icon: <HddOutlined />, isLeaf: true })),
    };
  })];
  const onNode = (key: string) => {
    if (key === "all") void select();
    else if (key.startsWith("t:")) void select(Number(key.slice(2)));
    else if (key.startsWith("d:")) { const separator = key.indexOf(":", 2); void select(Number(key.slice(2, separator)), key.slice(separator + 1)); }
  };
  const navigationDisabled = busy || !!transfer;
  const localRoots = device ? roots[device] || [] : [];
  const crumbs = [{ title: "Обзор парка", onClick: () => { if (!navigationDisabled) void select(); } }, ...(device ? [{ title: String(device), onClick: () => { if (!navigationDisabled) void browse(""); } }] : []), ...(path ? [{ title: path }] : [])];

  return <Space orientation="vertical" size="middle" style={{ width: "100%" }}>
    <Title level={3} style={{ margin: 0 }}>Файловый менеджер</Title>
    <Text type="secondary">Выберите терминал и диск. На время работы файловый менеджер занимает общую сессию с консолью и видео.</Text>
    {error && <Alert type="error" title={error} showIcon closable onClose={() => setError("")} />}
    {notice && <Alert type="info" title={notice} showIcon closable onClose={() => setNotice("")} />}
    <Row gutter={[16, 16]} style={{ width: "100%", margin: 0 }}>
      <Col xs={24} md={{ flex: "232px" }} style={{ minWidth: 0 }}>
        <Card title="Терминалы и диски" size="small" style={{ minHeight: 520 }} loading={loading}>
          <Input.Search aria-label="Поиск терминала" placeholder="Номер или адрес" value={search} onChange={event => setSearch(event.target.value)} disabled={navigationDisabled} style={{ marginBottom: 16 }} />
          {!devices.length ? <Empty description="Нет онлайн-терминалов" /> : <Tree className="fm-tree" blockNode showIcon showLine={{ showLeafIcon: false }} disabled={navigationDisabled}
            treeData={treeData} expandedKeys={expanded} selectedKeys={[device ? selectedRoot ? `d:${device}:${selectedRoot}` : `t:${device}` : "all"]}
            onSelect={keys => { if (keys[0] !== undefined) onNode(String(keys[0])); }}
            onExpand={(keys, info) => { setExpanded(keys); if (info.expanded && String(info.node.key).startsWith("t:")) onNode(String(info.node.key)); }} />}
        </Card>
      </Col>
      <Col xs={24} md={{ flex: "1" }} style={{ minWidth: 0 }}>
        <Card size="small" style={{ minHeight: 520 }} title={<Breadcrumb items={crumbs} />} extra={connected && <Tag color="blue">Монопольный сеанс</Tag>}>
          <Space wrap style={{ marginBottom: 16 }}>
            <Button aria-label="Назад" icon={<ArrowLeftOutlined />} disabled={navigationDisabled || !connected || historyIndex === 0} onClick={() => void browse(history[historyIndex - 1], 0, historyIndex - 1)} />
            <Button aria-label="Вперёд" icon={<ArrowRightOutlined />} disabled={navigationDisabled || !connected || historyIndex >= history.length - 1} onClick={() => void browse(history[historyIndex + 1], 0, historyIndex + 1)} />
            <Button aria-label="Вверх" icon={<ArrowUpOutlined />} disabled={navigationDisabled || !connected || !path} onClick={() => void browse(fmParent(path, localRoots))} />
            <Input aria-label="Путь к каталогу" value={address} placeholder="Выберите диск" disabled={navigationDisabled || !connected} onChange={event => setAddress(event.target.value)} onPressEnter={() => void browse(address)} style={{ width: 280, maxWidth: "100%" }} />
            <Button aria-label="Обновить каталог" icon={<ReloadOutlined />} disabled={navigationDisabled || !connected || !path} onClick={() => void browse(path, offset)} />
            <Upload showUploadList={false} disabled={navigationDisabled || !connected || !path || availability[device || 0]?.write_available === false}
              beforeUpload={file => { void transferFile("upload", fmJoin(path, file.name), file); return false; }}>
              <Button icon={<UploadOutlined />} disabled={navigationDisabled || !connected || !path || availability[device || 0]?.write_available === false}>Загрузить файл</Button>
            </Upload>
            {connected && <Button disabled={navigationDisabled} onClick={() => void select()}>Завершить сеанс</Button>}
          </Space>
          {!connected ? busy ? <Empty description={phase} /> : <Table<DeviceListItem> size="small" rowKey="device_id" pagination={{ pageSize: 20 }} dataSource={devices}
            locale={{ emptyText: "Нет онлайн-терминалов" }} columns={[
              { title: "Терминал", dataIndex: "device_id", render: (id: number) => <Button type="link" icon={<DesktopOutlined />} disabled={navigationDisabled || (drains[id] || 0) > now} onClick={() => void select(id)}>{id}</Button> },
              { title: "Название", dataIndex: "description" },
              { title: "Файловый менеджер", render: (_, item) => availability[item.device_id] ? fmReason[availability[item.device_id].state] || availability[item.device_id].state : "Проверка…" },
            ]} /> : !path ? <Space orientation="vertical">
            {localRoots.length ? localRoots.map(root => <Button key={root} icon={<HddOutlined />} disabled={navigationDisabled} onClick={() => void browse(root)}>{root}</Button>) : <Empty description="Нет доступных дисков или каталогов" />}
          </Space> : <>
            <Table<FmEntry> size="small" style={{ maxWidth: 560 }} tableLayout="fixed" scroll={{ x: 520 }} rowKey="name" dataSource={entries} loading={busy && !transfer} pagination={false}
              rowSelection={{ type: "radio", selectedRowKeys: selectedFile ? [selectedFile] : [], onChange: keys => setSelectedFile(String(keys[0] || "")), getCheckboxProps: () => ({ disabled: navigationDisabled }) }}
              onRow={item => ({ style: { cursor: item.directory && !navigationDisabled ? "pointer" : undefined }, onDoubleClick: () => { if (item.directory && !navigationDisabled) void browse(fmJoin(path, item.name)); }, onKeyDown: event => { if (event.key === "Enter" && item.directory && !navigationDisabled) void browse(fmJoin(path, item.name)); }, tabIndex: navigationDisabled ? -1 : 0 })}
              columns={[
                { title: "Имя", dataIndex: "name", width: 260, ellipsis: true, render: (name: string, item: FmEntry) => <span title={name} style={{ display: "block", overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}>{item.directory && <FolderOutlined style={{ marginRight: 8 }} />}{name}</span> },
                { title: "Размер", dataIndex: "size_bytes", width: 100, align: "right", render: (size: number, item: FmEntry) => item.directory ? "—" : `${size.toLocaleString()} байт` },
                { title: "", key: "actions", width: 128, render: (_, item) => !item.directory && <Button icon={<DownloadOutlined />} disabled={navigationDisabled || item.size_bytes > MAX_FM_BYTES} onClick={() => void transferFile("download", fmJoin(path, item.name))}>Скачать</Button> },
              ]} />
            <Space style={{ marginTop: 12 }}>
              <Button disabled={navigationDisabled || offset === 0} onClick={() => void browse(path, Math.max(0, offset - 64))}>Предыдущая страница</Button>
              <Text>{entries.length ? offset + 1 : 0}–{offset + entries.length}</Text>
              <Button disabled={navigationDisabled || !more} onClick={() => void browse(path, offset + 64)}>Следующая страница</Button>
            </Space>
          </>}
          <div role="status" style={{ marginTop: 16 }}><Text type="secondary">{phase}</Text></div>
        </Card>
      </Col>
    </Row>
    <Modal open={!!transfer} title={transfer?.name} closable={false} keyboard={false} maskClosable={false}
      footer={transfer?.result ? <Button type="primary" onClick={() => { setTransfer(undefined); transferRef.current = false; }}>Понятно</Button> : <Button danger disabled={transfer?.closing} onClick={() => void cancelTransfer()}>Отменить операцию</Button>}>
      <Space orientation="vertical">{!transfer?.result && <Spin />}<Text>{transfer?.phase}</Text>
        {transfer?.result ? <Alert type="warning" title={transfer.result} /> : <Text type="secondary">Навигация недоступна до завершения передачи и проверки результата.</Text>}
      </Space>
    </Modal>
  </Space>;
}
