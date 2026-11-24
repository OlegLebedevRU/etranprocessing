import { useCallback, useEffect, useState } from "react";
import { Alert, Button, Checkbox, Collapse, Input, Modal, Select, Space, Table, Tabs, Tag, Typography } from "antd";
import { useSearchParams } from "react-router";
import client from "../api/client";
import { watchPortalView } from "../utils/portalSocket";

interface Row {
  ip: string; login: string; reason: string; counts: number[]; first: number; last: number;
  terminal?: { device: number; org: number; owner: string } | null;
}
interface Snapshot {
  rows: Row[]; at: number; live: boolean; sample_at: number; incomplete: boolean;
  active_rules: string[] | null; dropped_packets?: number;
}
interface Preview { sample_at:number; last_success:number; token: string; active: string[]; recent: string[]; logins: string[]; partial: boolean; fresh: boolean; seen: boolean }
interface Operation { id: string; ip: string; ttl: number; actor: string; reason: string; expiry: number; status: string; created_at: string }
const label: Record<string,string> = {reconnect:"Повторное соединение (по выборке)",credentials:"Учётные данные",client_id:"Идентификатор клиента",tls_timeout:"Таймаут TLS",refused:"Отказ CONNECT", applied:"Применено на хосте",pending:"Ожидает подтверждения",refresh_preview:"Обновите оценку адреса",confirm_impact:"Требуется подтверждение",protected_address:"Защищённый адрес",superseded:"Заменено более новым решением"};
const when = (s: number) => s ? new Date(s*1000).toLocaleString("ru-RU", {timeZoneName:"short"}) : "—";

export default function AdminMqttPage() {
  const [params]=useSearchParams();
  const [query,setQuery]=useState(params.get("sn") ?? "");
  const [anomaly,setAnomaly]=useState("all");
  const [interval,setIntervalIndex]=useState(1);
  const [data,setData]=useState<Snapshot>();
  const [history,setHistory]=useState<Operation[]>([]);
  const [error,setError]=useState("");
  const [ip,setIp]=useState("");
  const [remove,setRemove]=useState(false);
  const [preview,setPreview]=useState<Preview>();
  const [reason,setReason]=useState("");
  const [ttl,setTtl]=useState(900);
  const [ack,setAck]=useState(false);
  const [busy,setBusy]=useState(false);
  const [nonce,setNonce]=useState("");
  const [operation,setOperation]=useState("");
  const [refreshKey,setRefreshKey]=useState(0);
  const refreshHistory=useCallback(async()=>{
    try { setHistory((await client.get<Operation[]>("/admin/mqtt/history")).data); }
    catch {setError("Не удалось получить журнал решений");}
  },[]);
  useEffect(()=>{void refreshHistory();},[refreshHistory]);
  useEffect(()=>{
    let live=true;
    const receive=(value:Snapshot)=>{if(live){setData(value);setError("");}};
    const stop=watchPortalView<Snapshot>({channel:"mqtt-admin"},receive,async()=>{
      try {receive((await client.get<Snapshot>("/admin/mqtt/observations")).data);}
      catch {if(live)setError("Наблюдения недоступны");}
    });
    return ()=>{live=false;stop();};
  },[]);
  useEffect(()=>{
    if(!ip || remove)return;
    const c=new AbortController();
    client.get<Preview>("/admin/mqtt/preview",{params:{ip},signal:c.signal}).then(r=>{
      if(!c.signal.aborted)setPreview(r.data);
    }).catch(()=>{if(!c.signal.aborted)setOperation("Не удалось проверить соседние подключения");});
    return ()=>c.abort();
  },[ip,remove,refreshKey]);
  const open=(addr:string,del:boolean)=>{
    setIp(addr);setRemove(del);setPreview(undefined);setReason("");setAck(false);setOperation("");setTtl(900);
    setNonce(crypto.randomUUID());setRefreshKey(k=>k+1);
  };
  const shared=!!preview && (preview.partial || preview.active.length>0 || preview.recent.length>0);
  const submit=async()=>{
    setBusy(true);
    try {
      const r=await client.post<Operation>("/admin/mqtt/operations",{operation_id:nonce,ip,ttl:remove?0:ttl,reason:reason.trim(),token:remove?"":preview?.token,acknowledged:ack});
      const status=r.data.status;
      setOperation(label[status]??status);
      if(status==="applied" || status==="pending")setIp("");
      else setNonce(crypto.randomUUID());
      void refreshHistory();
    } catch {setOperation("Подтверждение не получено. Проверьте журнал перед повтором.");}
    finally {setBusy(false);}
  };
  const q=query.trim().toLowerCase();
  const ips=new Map<string,Set<string>>(), identities=new Map<string,Set<string>>();
  for(const r of data?.rows??[]){
    if(!r.login || !r.counts[interval])continue;
    if(!ips.has(r.login))ips.set(r.login,new Set());
    if(!identities.has(r.ip))identities.set(r.ip,new Set());
    ips.get(r.login)!.add(r.ip); identities.get(r.ip)!.add(r.login);
  }
  const rows=(data?.rows??[]).filter(r=>anomaly==="all" || (anomaly==="rate" && r.counts[0]>=30) || (anomaly==="ips" && (ips.get(r.login)?.size??0)>1) || (anomaly==="users" && (identities.get(r.ip)?.size??0)>1)).filter(r=>r.counts[interval]>0 && [r.ip,r.login,r.terminal?.device,r.terminal?.owner].some(v=>String(v??"").toLowerCase().includes(q)));
  const cols=[
    {title:"IP",dataIndex:"ip",render:(v:string)=><Button type="link" onClick={()=>setQuery(v)}>{v}</Button>},
    {title:"Терминал / MQTT-логин",render:(_:unknown,r:Row)=><Space direction="vertical"><span>{r.terminal ? r.terminal.device+" · "+r.terminal.owner : "Без привязки"}</span><Button type="link" onClick={()=>setQuery(r.login)}>{r.login||"Не установлен"}</Button></Space>},
    {title:"Причина",dataIndex:"reason",render:(v:string)=>label[v]??v},
    {title:"Попытки",render:(_:unknown,r:Row)=><Tag color={r.counts[0]>=30?"orange":undefined}>{r.counts[interval]}</Tag>,sorter:(a:Row,b:Row)=>a.counts[interval]-b.counts[interval],defaultSortOrder:"descend" as const},
    {title:"Первый / последний эпизод за сутки",render:(_:unknown,r:Row)=><>{when(r.first)}<br/>{when(r.last)}</>},
    {title:"Действие",render:(_:unknown,r:Row)=><Button disabled={!data?.active_rules} onClick={()=>open(r.ip,!!data?.active_rules?.includes(r.ip))}>{data?.active_rules?.includes(r.ip)?"Снять":"Блокировать"}</Button>},
  ];
  return <Space direction="vertical" style={{width:"100%"}}>
    <Typography.Title level={3}>MQTT-подключения</Typography.Title>
    <Collapse items={[{key:"help",label:"Как работает блокировка",children:<p>Docker публикует порт RabbitMQ TCP/8883. По команде MenuBuilder служба на сервере добавляет IPv4- или IPv6-адрес в набор nftables. Фильтр отбрасывает новые подключения до передачи в Docker, проверки TLS и обработки MQTT CONNECT. Агенты могут продолжать попытки. Открытые сессии продолжают работать. За общим NAT запрет действует на переподключение каждого устройства с этого IP. Сервер снимает правило по истечении срока. Кнопка «Снять» позволяет сделать это раньше.</p>}]} />
    {error && <Alert type="error" message={error}/>}
    {operation && !ip && <Alert type="info" message={operation}/>}
    {data && (!data.live || data.incomplete || Date.now()/1000-data.at>20 || !data.active_rules) && <Alert type="warning" message="Данные неполны, устарели или исполнитель недоступен" />}
    <Tabs items={[
      {key:"activity",label:"Активность",children:<><Space wrap><Input allowClear placeholder="IP, SN, номер или организация" value={query} onChange={e=>setQuery(e.target.value)} style={{width:300}}/><Select value={interval} onChange={setIntervalIndex} options={["5 минут","1 час","24 часа"].map((v,i)=>({label:v,value:i}))}/><Select value={anomaly} onChange={setAnomaly} options={[{value:"all",label:"Все события"},{value:"rate",label:"От 30 за 5 минут"},{value:"ips",label:"Логин с разных IP"},{value:"users",label:"Несколько логинов за IP"}]}/><span>Обновлено {when(data?.at??0)}</span></Space><Table loading={!data&&!error} rowKey={r=>JSON.stringify([r.ip,r.login,r.reason])} columns={cols} dataSource={rows} scroll={{x:850}}/></>},
      {key:"rules",label:"Блокировки",children:<><p>Отброшено пакетов правилами MQTT с момента установки счётчиков: {data?.dropped_packets??"неизвестно"}</p><Table rowKey="ip" dataSource={(data?.active_rules??[]).map(addr=>({ip:addr,decision:history.find(h=>h.ip===addr&&h.status==="applied"&&h.ttl>0)}))} columns={[{title:"IP",dataIndex:"ip"},{title:"До",render:(_,r)=>when(r.decision?.expiry??0)},{title:"Автор / причина",render:(_,r)=>r.decision ? r.decision.actor+" · "+r.decision.reason:"—"},{title:"Действие",render:(_,r)=><Button onClick={()=>open(r.ip,true)}>Снять</Button>}]} /></>},
      {key:"history",label:"Журнал",children:<><Button onClick={()=>void refreshHistory()}>Обновить журнал</Button><Table rowKey="id" dataSource={history} scroll={{x:800}} columns={[{title:"Время",render:(_,r)=>new Date(r.created_at).toLocaleString("ru-RU")},{title:"IP",dataIndex:"ip"},{title:"Действие",render:(_,r)=>r.ttl?"Блокировать до "+when(r.expiry):"Снять"},{title:"Автор",dataIndex:"actor"},{title:"Причина",dataIndex:"reason"},{title:"Результат",render:(_,r)=>label[r.status]??r.status}]}/></>},
    ]}/>
    <Modal open={!!ip} title={(remove?"Снять блокировку ":"Блокировать ")+ip} onCancel={()=>{if(!busy)setIp("");}} onOk={()=>void submit()} okText={remove?"Снять блокировку":"Заблокировать"} confirmLoading={busy} okButtonProps={{disabled:reason.trim().length<3||(!remove&&(!preview?.fresh||!preview.seen||(shared&&!ack)))}}>
      <Space direction="vertical" style={{width:"100%"}}>
        {!remove && <><Select value={ttl} onChange={setTtl} options={[{value:900,label:"15 минут"},{value:3600,label:"1 час"},{value:21600,label:"6 часов"}]}/>
        <Alert type="warning" message={`До ${when(Math.floor(Date.now()/1000)+ttl)} новые MQTT-подключения с ${ip} будут заблокированы. Это касается всех устройств с этим внешним IP.`}/>
        {preview ? <><p>Снимок от {when(preview.sample_at)}. Активные логины: {preview.active.join(", ")||"нет активных"}. Последняя выборка с подключением: {when(preview.last_success)}. В периодических снимках за сутки: {preview.recent.join(", ")||"нет наблюдений"}.</p>{(!preview.fresh||preview.partial)&&<Tag color="orange">Оценка неполная или устарела</Tag>}{shared&&<Checkbox checked={ack} onChange={e=>setAck(e.target.checked)}>Подтверждаю влияние на устройства за этим адресом</Checkbox>}</>:<span>Проверка соседних подключений…</span>}
        <Button onClick={()=>{setPreview(undefined);setAck(false);setRefreshKey(k=>k+1);setNonce(crypto.randomUUID());}}>Обновить оценку</Button></>}
        <Input.TextArea aria-label="Причина" maxLength={500} placeholder="Причина решения" value={reason} onChange={e=>setReason(e.target.value)}/>
        {operation&&<Alert type="info" message={operation}/>}
      </Space>
    </Modal>
  </Space>;
}
