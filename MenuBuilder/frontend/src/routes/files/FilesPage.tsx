import { useCallback, useEffect, useRef, useState } from "react";
import { Alert, Button, Card, Empty, Input, Select, Space, Table, Tag, Typography, Upload } from "antd";
import { DownloadOutlined, FolderOutlined, ReloadOutlined, UploadOutlined } from "@ant-design/icons";
import { useSession } from "../../session/SessionContext";
import { getDevices, type DeviceListItem } from "../../api/devices";
import { listTerminalsSettings } from "../../api/settings";
import { selectableDevices } from "../../utils/terminalPresentation";
import { fmApi, fmError, fmReason, fmStorage, MAX_FM_BYTES, type FmEntry, type FmOperation, type FmReadiness } from "../../api/fileManager";

const { Text, Title } = Typography;

function digest(file: Blob, signal: AbortSignal): Promise<string> {
  return new Promise((resolve,reject) => {
    const worker = new Worker(new URL("./hash.worker.ts", import.meta.url), { type: "module" });
    const cleanup = () => { worker.terminate(); signal.removeEventListener("abort", abort); };
    const abort = () => { cleanup(); reject(new Error("Cancelled")); };
    if (signal.aborted) { abort(); return; }
    signal.addEventListener("abort",abort,{ once:true });
    worker.onerror = () => { cleanup(); reject(new Error("Checksum failed")); };
    worker.onmessage = ({data}: MessageEvent<{sha256?:string}>) => { cleanup(); if (data.sha256) resolve(data.sha256); else reject(new Error("Checksum failed")); };
    worker.postMessage({file});
  });
}

async function delay(signal: AbortSignal, ms = 700) {
  return new Promise<void>((resolve,reject) => {
    const abort = () => { clearTimeout(timer);reject(new Error("Cancelled")); };
    const timer = setTimeout(() => { signal.removeEventListener("abort",abort);resolve(); },ms);
    if (signal.aborted) { abort();return; }
    signal.addEventListener("abort",abort,{once:true});
  });
}

export default function FilesPage() {
  const {user} = useSession();
  if (!user || ![1,2,3,5].includes(user.role_id ?? 0)) return <Alert type="warning" title="Для файлового менеджера нужны права управления терминалом." />;
  if (typeof user.org_id !== "number" || user.org_id <= 0) return <Alert type="info" title="Выберите организацию для работы с файлами терминала." />;
  return <TenantFiles key={user.org_id} org={user.org_id} />;
}

function TenantFiles({org}: {org: number}) {
  const [devices,setDevices] = useState<DeviceListItem[]>([]);
  const [device,setDevice] = useState<number>();
  const [readiness,setReadiness] = useState<FmReadiness>();
  const [lease,setLease] = useState<string>();
  const [path,setPath] = useState("");
  const [listedPath,setListedPath] = useState("");
  const [offset,setOffset] = useState(0);
  const [more,setMore] = useState(false);
  const [roots,setRoots] = useState<string[]>([]);
  const [entries,setEntries] = useState<FmEntry[]>([]);
  const [busy,setBusy] = useState(false);
  const [phase,setPhase] = useState("");
  const [error,setError] = useState("");
  const view = useRef(crypto.randomUUID());
  const current = useRef<{ device?:number; lease?:string; operation?:string; abort?:AbortController }>({});
  const mounted = useRef(true);
  const generation = useRef(0);

  const end = useCallback(async (message = "") => {
    const active = current.current;
    current.current = {}; generation.current++;
    active.abort?.abort();
    if (mounted.current) { setLease(undefined);setBusy(false);setPhase("");setEntries([]);if(message)setError(message); }
    if (active.device && active.lease) {
      const api = fmApi(active.device,view.current);
      try { if (active.operation) await api.signal(active.lease,"cancel",active.operation); } catch { /* Stop still follows; IoT expiry is the last bound. */ }
      try { await api.signal(active.lease,"stop"); } catch { if(mounted.current)setError("Связь потеряна. Сессия закроется по таймауту; результат записи может быть неизвестен."); }
    }
  },[]);

  useEffect(() => {
    mounted.current = true;const controller = new AbortController();
    async function load() {
      try {
        const [first,terminals] = await Promise.all([getDevices(org,{page:1,size:100}),listTerminalsSettings({org_id:org,all:true})]);
        const items = [...first.items];
        for(let page=2;page<=first.pages;page++) items.push(...(await getDevices(org,{page,size:100})).items);
        if(!controller.signal.aborted) { const available = selectableDevices(items,terminals.items);setDevices(available);setDevice(available[0]?.device_id); }
      } catch { if(!controller.signal.aborted)setError("Не удалось загрузить терминалы."); }
    }
    void load();
    return () => { mounted.current=false;controller.abort();void end(); };
  },[org,end]);

  useEffect(() => {
    setReadiness(undefined);if(!device)return;
    const controller = new AbortController();let running=false;
    const check = async () => {
      if(running)return;running=true;
      try { const next=await fmApi(device,view.current,controller.signal).readiness();if(!controller.signal.aborted)setReadiness({...next,valid_until:next.valid_until ? new Date(Date.now()+Math.max(0,Date.parse(next.valid_until)-Date.parse(next.server_time))).toISOString() : next.valid_until}); }
      catch { if(!controller.signal.aborted)setReadiness(undefined); }
      finally { running=false; }
    };
    void check();const timer=setInterval(() => void check(),10000);
    return () => {controller.abort();clearInterval(timer);};
  },[device]);

  useEffect(() => {
    if(!lease || !device)return;
    let running=false;
    const timer=setInterval(async () => {
      if(running)return;running=true;
      try {
        const api=fmApi(device,view.current);const renewed=await api.signal(lease,"renew");const deadline=Date.now()+6000;
        let applied=false;
        while(Date.now()<deadline && current.current.lease===lease) {
          const status=await api.status(lease);
          if(status.applied_expires_at && Date.parse(status.applied_expires_at)>=Date.parse(renewed.expires_at)) {applied=true;break;}
          await new Promise(resolve=>setTimeout(resolve,500));
        }
        if(!applied && current.current.lease===lease)throw new Error("Renew acknowledgement missing");
      }
      catch(error) { if(current.current.lease===lease)void end(fmError(error)); }
      finally {running=false;}
    },15000);
    return () => clearInterval(timer);
  },[lease,device,end]);

  const wait = async (api: ReturnType<typeof fmApi>, id:string, signal:AbortSignal, expected:string, timeout=45000):Promise<FmOperation> => {
    const deadline=Date.now()+timeout;
    while(Date.now()<deadline) {
      const result=await api.status(id);
      if(result.state===expected)return result;
      if(["failed","cancelled"].includes(result.state))throw new Error(result.error_code);
      await delay(signal);
    }
    throw new Error("Agent acknowledgement timeout");
  };

  const start = async () => {
    if(!device || busy)return;setBusy(true);setError("");setPhase("Ожидается подтверждение агента…");
    const abort=new AbortController();const run=++generation.current;
    current.current={device,abort};const api=fmApi(device,view.current,abort.signal);
    try {
      const session=await api.start();
      if(run!==generation.current) {void fmApi(device,view.current).signal(session.lease_id,"stop");return;}
      current.current.lease=session.lease_id;setLease(session.lease_id);
      const ready=await wait(api,session.lease_id,abort.signal,"active",10000);
      if(run!==generation.current)return;
      setRoots(ready.roots || []);setPath(ready.roots?.[0] || "");setPhase("Сессия активна");
    } catch(error) { if(run===generation.current)await end(fmError(error)); }
    finally {if(run===generation.current)setBusy(false);}
  };

  const operate = async (kind:"list"|"upload"|"download", target:string, file?:File, pageOffset=0) => {
    const active=current.current;if(!active.device || !active.lease || !active.abort || busy)return;
    const run=generation.current;setBusy(true);setError("");
    const signal=AbortSignal.any([active.abort.signal,AbortSignal.timeout(90000)]);
    const api=fmApi(active.device,view.current,signal);
    try {
      if(file && file.size>MAX_FM_BYTES)throw new Error("File too large");
      setPhase(kind==="list"?"Чтение каталога…":"Подготовка и проверка файла…");
      const operation=await api.create(active.lease,kind,target,pageOffset);active.operation=operation.id;
      if(kind==="upload" && file) {
        const sha=await digest(file,signal);const grant=await api.manifest(operation.id,file.size,sha);
        setPhase("Загрузка в хранилище…");await fmStorage(grant,signal,file);await api.sourceComplete(operation.id);
        setPhase("Агент проверяет файл перед записью…");await wait(api,operation.id,signal,"completed");
      } else if(kind==="download") {
        const source=await wait(api,operation.id,signal,"verifying");const grant=await api.download(operation.id);
        const response=await fmStorage(grant,signal);const reader=response.body?.getReader();if(!reader)throw new Error("No download stream");
        const chunks: Uint8Array<ArrayBuffer>[]=[];let size=0;
        for(;;) { const {done,value}=await reader.read();if(done)break;size+=value.byteLength;if(size>MAX_FM_BYTES || size>(source.size_bytes??0)){await reader.cancel();throw new Error("Invalid file length");}chunks.push(new Uint8Array(value)); }
        const blob=new Blob(chunks);const sha=await digest(blob,signal);
        if(size!==source.size_bytes || sha!==source.sha256)throw new Error("Integrity failure");
        await api.received(operation.id,size,sha);
        if(run!==generation.current)return;
        const url=URL.createObjectURL(blob);const link=document.createElement("a");link.href=url;link.download=target.split("\\").pop() || "download";link.click();setTimeout(() => URL.revokeObjectURL(url),10000);
      } else {
        const result=await wait(api,operation.id,signal,"completed");if(run===generation.current){setEntries(result.entries);setPath(target);setListedPath(target);setOffset(pageOffset);setMore(result.has_more===true);}
      }
      active.operation=undefined;if(run===generation.current)setPhase(kind==="list"?"Каталог получен":"Файл проверен и передан");
    } catch(error) {if(run===generation.current)await end(fmError(error));}
    finally {if(run===generation.current)setBusy(false);}
  };

  const fresh = readiness?.available && readiness.valid_until && Date.parse(readiness.valid_until)>Date.now();
  return <Space orientation="vertical" size="middle" style={{width:"100%",maxWidth:1400}}>
    <Title level={3} style={{margin:0}}>Файловый менеджер</Title>
    <Text type="secondary">Во время работы терминал занят файловым менеджером. Консоль, видео и удалённое управление доступны после завершения сеанса.</Text>
    {error && <Alert type="error" title={error} showIcon />}
    <Card size="small"><Space wrap>
      <Select aria-label="Терминал" placeholder="Выберите терминал" style={{minWidth:260}} value={device} disabled={!!lease || busy} showSearch optionFilterProp="label" options={devices.map(item=>({value:item.device_id,label:`${item.description || item.sn} · ${item.device_id}`}))} onChange={value=>{setDevice(value);setEntries([]);setPath("");setError("");}} />
      <Tag color={fresh?"success":"warning"}>{readiness?fmReason[readiness.state] || readiness.state:"Проверка доступности…"}</Tag>
      {readiness?.agent_version && <Text type="secondary">Агент {readiness.agent_version}</Text>}
      {!lease?<Button type="primary" disabled={!fresh || busy} loading={busy} onClick={()=>void start()}>Начать сеанс</Button>:<Button danger onClick={()=>void end()}>Завершить сеанс</Button>}
    </Space></Card>
    <Card title="Файлы терминала" extra={lease && <Tag color="blue">Монопольная сессия</Tag>}>
      {!lease?<Empty description="Выберите доступный совместимый агент и начните сеанс." />:<Space orientation="vertical" style={{width:"100%"}}>
        <Space wrap><Select aria-label="Разрешённый каталог" placeholder="Каталог" value={roots.includes(path)?path:undefined} style={{minWidth:200}} options={roots.map(value=>({value,label:value}))} disabled={busy} onChange={value=>void operate("list",value)} />
          <Input aria-label="Путь к каталогу" value={path} onChange={event=>setPath(event.target.value)} disabled={busy} style={{width:360}} onPressEnter={()=>void operate("list",path)} />
          <Button aria-label="Открыть" icon={<ReloadOutlined />} disabled={busy || !path} onClick={()=>void operate("list",path)}>Открыть</Button>
          <Upload showUploadList={false} disabled={busy || !listedPath || path!==listedPath || readiness?.write_available===false} beforeUpload={file=>{void operate("upload",`${listedPath}\\${file.name}`,file);return false;}}><Button icon={<UploadOutlined />} disabled={busy || !listedPath || path!==listedPath || readiness?.write_available===false}>Загрузить файл</Button></Upload>
          {busy && <Button danger onClick={()=>void end("Операция остановлена. Для повторения начните новый сеанс.")}>Отменить операцию</Button>}
        </Space>
        <Text type="secondary">{phase}. Один файл до 64 МиБ. Существующие файлы не заменяются.</Text>
        <Table<FmEntry> size="small" rowKey="name" dataSource={entries} loading={busy} pagination={{pageSize:50}} columns={[
          {title:"Имя",dataIndex:"name",render:(name:string,item:FmEntry)=>item.directory?<Button type="link" icon={<FolderOutlined />} disabled={busy} onClick={()=>void operate("list",`${listedPath}\\${name}`)}>{name}</Button>:name},
          {title:"Размер",dataIndex:"size_bytes",render:(size:number,item:FmEntry)=>item.directory?"—":`${size.toLocaleString()} байт`},
          {title:"",key:"actions",render:(_,item)=>!item.directory && <Button icon={<DownloadOutlined />} disabled={busy || item.size_bytes>MAX_FM_BYTES} onClick={()=>void operate("download",`${listedPath}\\${item.name}`)}>Скачать</Button>},
        ]} />
        <Space><Button disabled={busy || !listedPath || offset===0} onClick={()=>void operate("list",listedPath,undefined,Math.max(0,offset-64))}>Предыдущая страница</Button><Button disabled={busy || !more} onClick={()=>void operate("list",listedPath,undefined,offset+64)}>Следующая страница</Button></Space>
      </Space>}
    </Card>
  </Space>;
}
