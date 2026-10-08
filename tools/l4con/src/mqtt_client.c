#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#include "mqtt_client.h"
#include "mqtt_protocol.h"
#include "rpc_contract.h"
#include "file_manager.h"
#include "command_runner.h"
#include "tool_inventory.h"
#include "event_ipc.h"
#include "link_probe.h"
#include "update_reporting.h"
#include "update_admission.h"
#include "../../l4common/probe_ipc.h"
#include "../../l4common/update_state.h"
#include "../../leo4proxy/src/policy_json.h"
#include "../../l4pin/src/cert_discovery.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <objbase.h>

#pragma comment(lib, "ws2_32.lib")

typedef struct {
    SOCKET sock;
    CRITICAL_SECTION send_cs;
    CRITICAL_SECTION probe_cs;
    LinkProbe probe;
    unsigned probe_ready;
    uint16_t probe_subscriptions[3];
    uint16_t packet_id_seq;
    char sn[128];
    CommandContext current_cmd;
    HANDLE hWorkerThread;
    bool connected;
    L4UpdateConsumer update;
    L4UpdateReporting* reporting;
    L4UpdateAdmission* admission;
    uint16_t fm_subscription;
    ULONGLONG fm_subscription_deadline;
    struct { char id[40]; char result_uid[40]; char result[2048]; int status; bool acted; } recent[64];
    unsigned recent_next;
} MqttClientState;
static bool ascii_identifier(const char* value, bool hex_only);

static bool send_publish_packet(MqttClientState* state, const char* topic,
                                const char* payload, size_t payload_len,
                                uint8_t qos, uint8_t retain);
static bool send_publish_with_properties(MqttClientState* state, const char* topic,
                                         const char* payload, size_t payload_len,
                                         uint8_t qos, uint8_t retain,
                                         const MqttUserProperty* properties,
                                         size_t property_count);

static void get_iso_timestamp(char* out_ts, size_t size) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(out_ts, size, "%04u-%02u-%02u %02u:%02u:%02u",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static bool update_busy(MqttClientState* state){
    L4UpdateState update;return !l4_update_consumer_read(&state->update,&update) || update.window!=0;
}
static bool publish_user_event_locked(const UserEvent* event, void* context) {
    MqttClientState* state = (MqttClientState*)context;
    if(update_busy(state))return false;
    GUID guid;
    if (FAILED(CoCreateGuid(&guid))) return false;
    unsigned id = guid.Data1 & 0x7fffffffU;
    if (!id) id = 1;
    SYSTEMTIME utc;
    GetSystemTime(&utc);
    char timestamp[32], correlation[40], id_text[16], code_text[16];
    snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02uT%02u:%02u:%02uZ",
        utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
    snprintf(correlation, sizeof(correlation), "%08lX-%04hX-%04hX-%02X%02X-%02X%02X%02X%02X%02X%02X",
        guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2],
        guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    snprintf(id_text, sizeof(id_text), "%u", id);
    snprintf(code_text, sizeof(code_text), "%d", event->code);
    char payload[7000], topic[160];
    EnterCriticalSection(&state->send_cs);
    bool ok = false;
    if (state->connected && state->sock != INVALID_SOCKET && ascii_identifier(state->sn, false)) {
        snprintf(topic, sizeof(topic), "dev/%s/evt", state->sn);
        int length = event_json(event, state->sn, id, timestamp, correlation, payload, sizeof(payload));
        const MqttUserProperty properties[] = {
            { "event_type_code", code_text }, { "dev_event_id", id_text },
            { "dev_timestamp", timestamp }, { "correlationData", correlation }
        };
        if (length > 0 && (size_t)length < sizeof(payload))
            ok = send_publish_with_properties(state, topic, payload, (size_t)length, 1, 0, properties, 4);
    }
    LeaveCriticalSection(&state->send_cs);
    return ok;
}

static bool publish_user_event(const UserEvent* event,void* context){
    MqttClientState* state=(MqttClientState*)context;AcquireSRWLockShared(&state->update.admission);
    bool ok=publish_user_event_locked(event,context);ReleaseSRWLockShared(&state->update.admission);return ok;
}

static bool ascii_identifier(const char* value, bool hex_only) {
    if (!value || !value[0]) return false;
    for (const unsigned char* p = (const unsigned char*)value; *p; p++) {
        if (hex_only ? !isxdigit(*p) : !(isalnum(*p) || *p == '-' || *p == '_')) return false;
    }
    return true;
}

static bool publish_certificate_connected_event(MqttClientState* state, int proxy_port,
                                                bool force, char* observed_correlation, unsigned* observed_id) {
    static char last_snapshot[512] = { 0 };
    char inventory[12000], digest[65];
    char package_version[64] = { 0 };
    if (!tool_inventory_build(inventory, sizeof(inventory), digest)) {
        fprintf(stderr, "[CERT] Tool inventory scan failed; event deferred\n");
        return false;
    }
    tool_inventory_package_version(package_version);
    ProxyIdentity identity;
    cert_info local_cert = { 0 };
    cert_state local_state = cert_discover(NULL, &local_cert);
    if (config_query_identity_from_proxy(proxy_port, &identity) != 0 ||
        (local_state != CERT_VALID && local_state != CERT_EXPIRING) ||
        local_cert.cert_duplicates != 0 ||
        strcmp(identity.sn, state->sn) != 0 ||
        strcmp(identity.sn, local_cert.sn) != 0 ||
        _stricmp(identity.thumbprint, local_cert.thumbprint_hex) != 0 ||
        !ascii_identifier(identity.sn, false) ||
        strlen(identity.thumbprint) != 40 ||
        !ascii_identifier(identity.thumbprint, true) ||
        !ascii_identifier(identity.serial, true) ||
        strlen(identity.not_after) < 19) {
        printf("[CERT] Active proxy identity is not ready or does not match MQTT SN; event deferred\n");
        return false;
    }
    char snapshot[sizeof(last_snapshot)];
    int snapshot_len = snprintf(snapshot, sizeof(snapshot), "%s:%s:%s:%s:%s",
                                identity.thumbprint, identity.serial, identity.not_after,
                                package_version, digest);
    if (snapshot_len <= 0 || (size_t)snapshot_len >= sizeof(snapshot)) return false;
    if (!force && strcmp(snapshot, last_snapshot) == 0) return true;

    GUID guid;
    if (FAILED(CoCreateGuid(&guid))) return false;
    unsigned event_id = guid.Data1 & 0x7fffffffU;
    if (!event_id) event_id = 1;
    SYSTEMTIME utc;
    GetSystemTime(&utc);
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02uT%02u:%02u:%02uZ",
             utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
    char not_after[24];
    memcpy(not_after, identity.not_after, 19);
    not_after[19] = '\0';
    not_after[10] = 'T';
    strcat_s(not_after, sizeof(not_after), "Z");

    char topic[160], payload[16384];
    char package_json[70];
    if (package_version[0]) snprintf(package_json, sizeof(package_json), "\"%s\"", package_version);
    else strcpy_s(package_json, sizeof(package_json), "null");
    char event_id_text[16];
    snprintf(event_id_text, sizeof(event_id_text), "%u", event_id);
    snprintf(topic, sizeof(topic), "dev/%s/evt", state->sn);
    int length = snprintf(payload, sizeof(payload),
        "{\"101\":%u,\"102\":\"%s\",\"200\":75,\"300\":[{\"324\":\"%s\",\"440\":\"l4con\",\"441\":\"%s\",\"442\":\"%s\",\"443\":\"%s\",\"444\":%s,\"445\":%s}],\"correlationData\":\"%08lX-%04hX-%04hX-%02X%02X-%02X%02X%02X%02X%02X%02X\"}",
        event_id, timestamp, state->sn, identity.thumbprint, identity.serial, not_after,
        inventory, package_json,
        guid.Data1, guid.Data2, guid.Data3,
        guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    if (length <= 0 || (size_t)length >= sizeof(payload)) return false;
    char correlation_id[40] = { 0 };
    if (!json_extract_string(payload, "correlationData",
                             correlation_id, sizeof(correlation_id))) return false;
    const MqttUserProperty event_properties[] = {
        { "event_type_code", "75" },
        { "dev_event_id", event_id_text },
        { "dev_timestamp", timestamp },
        { "correlationData", correlation_id },
    };
    if (!send_publish_with_properties(state, topic, payload, (size_t)length,
                                      1, 0, event_properties, 4)) return false;
    if(observed_correlation)strcpy_s(observed_correlation,40,correlation_id);
    if(observed_id)*observed_id=event_id;
    strcpy_s(last_snapshot, sizeof(last_snapshot), snapshot);
    printf("[CERT] Published identity event 75 for SN %s (qos=1, retain=0)\n", state->sn);
    return true;
}

static uint16_t get_next_packet_id(MqttClientState* state) {
    state->packet_id_seq++;
    if (state->packet_id_seq == 0) state->packet_id_seq = 1;
    return state->packet_id_seq;
}

static bool socket_send_all(SOCKET sock, const unsigned char* buf, size_t len) {
    size_t total_sent = 0;
    ULONGLONG deadline=GetTickCount64()+3000;
    while (total_sent < len) {
        ULONGLONG now=GetTickCount64();
        if (now>=deadline) return false;
        DWORD remaining=(DWORD)(deadline-now);
        if (setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,(const char*)&remaining,sizeof(remaining))) return false;
        int sent = send(sock, (const char*)(buf + total_sent), (int)(len - total_sent), 0);
        if (sent <= 0) return false;
        total_sent += (size_t)sent;
    }
    return true;
}

static bool send_publish_with_properties(MqttClientState* state, const char* topic,
                                         const char* payload, size_t payload_len,
                                         uint8_t qos, uint8_t retain,
                                         const MqttUserProperty* properties,
                                         size_t property_count) {
    unsigned char buf[32768];
    EnterCriticalSection(&state->send_cs);
    if (!state->connected || state->sock == INVALID_SOCKET) {LeaveCriticalSection(&state->send_cs);return false;}
    uint16_t id=qos?get_next_packet_id(state):0;
    int length=mqtt_build_publish_with_properties(buf,sizeof(buf),topic,payload,payload_len,id,qos,retain,properties,property_count);
    bool ok=length>0 && socket_send_all(state->sock,buf,(size_t)length);
    LeaveCriticalSection(&state->send_cs);return ok;
}
static bool send_probe(MqttClientState* state,const char* topic,const char* payload,
                       const MqttUserProperty* properties,size_t count) {
    unsigned char buf[32768];
    EnterCriticalSection(&state->send_cs);
    if (!state->connected || state->sock == INVALID_SOCKET) {
        LeaveCriticalSection(&state->send_cs);
        return false;
    }
    uint16_t pkt_id = get_next_packet_id(state);
    int pkt_len = mqtt_build_publish_expiring(buf, sizeof(buf), topic, payload,
                                                      strlen(payload), pkt_id, 1, 0,
                                                      properties, count,10);
    bool ok = false;
    if (pkt_len > 0) {
        ok = socket_send_all(state->sock, buf, (size_t)pkt_len);
    }
    LeaveCriticalSection(&state->send_cs);
    return ok;
}

static bool socket_connect_bounded(SOCKET sock,const struct sockaddr* address,int size,HANDLE stop) {
    u_long nonblocking=1;
    if (ioctlsocket(sock,FIONBIO,&nonblocking)) return false;
    bool connected=connect(sock,address,size)==0;
    int error=connected?0:WSAGetLastError();
    ULONGLONG deadline=GetTickCount64()+5000;
    while (!connected && (error==WSAEWOULDBLOCK || error==WSAEINPROGRESS) && GetTickCount64()<deadline) {
        if (stop && WaitForSingleObject(stop,0)==WAIT_OBJECT_0) break;
        fd_set writes,errors;FD_ZERO(&writes);FD_ZERO(&errors);FD_SET(sock,&writes);FD_SET(sock,&errors);
        struct timeval wait={0,100000};
        int ready=select(0,NULL,&writes,&errors,&wait);
        if (ready<0) break;
        if (ready>0) { int status=0,length=sizeof(status);
            connected=!getsockopt(sock,SOL_SOCKET,SO_ERROR,(char*)&status,&length) && status==0;
            break;
        }
    }
    nonblocking=0;
    if (ioctlsocket(sock,FIONBIO,&nonblocking)) connected=false;
    return connected;
}

static bool socket_recv_all(SOCKET sock, unsigned char* buf, size_t len, ULONGLONG deadline) {
    size_t used = 0;
    while (used < len) {
        ULONGLONG now=GetTickCount64();
        if (now>=deadline) return false;
        DWORD timeout=(DWORD)(deadline-now);
        setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout,sizeof(timeout));
        int received = recv(sock, (char*)buf + used, (int)(len - used), 0);
        if (received <= 0) return false;
        used += (size_t)received;
    }
    return true;
}

static bool read_connack_v5(SOCKET sock, unsigned char* out_reason) {
    ULONGLONG deadline=GetTickCount64()+10000;
    unsigned char fixed = 0, length_bytes[4], body[1024];
    if (!socket_recv_all(sock, &fixed, 1, deadline) || fixed != MQTT_PKT_CONNACK) return false;
    uint32_t remaining = 0;
    int used = 0;
    do {
        if (used == 4 || !socket_recv_all(sock, &length_bytes[used], 1, deadline)) return false;
        used++;
    } while (length_bytes[used - 1] & 0x80);
    int parsed = 0;
    if (mqtt_decode_remaining_length(length_bytes, (size_t)used,
                                     &remaining, &parsed) != 0 ||
        remaining < 3 || remaining > sizeof(body) ||
        !socket_recv_all(sock, body, remaining, deadline)) return false;
    uint32_t properties_len = 0;
    int property_len_bytes = 0;
    if (mqtt_decode_remaining_length(body + 2, remaining - 2,
                                     &properties_len, &property_len_bytes) != 0 ||
        2U + (uint32_t)property_len_bytes + properties_len != remaining) return false;
    *out_reason = body[1];
    return true;
}

static bool send_publish_packet(MqttClientState* state, const char* topic,
                                const char* payload, size_t payload_len,
                                uint8_t qos, uint8_t retain) {
    return send_publish_with_properties(state, topic, payload, payload_len,
                                        qos, retain, NULL, 0);
}

static void output_chunk_callback(const char* topic, const char* json_envelope, size_t json_len, void* user_data) {
    MqttClientState* state = (MqttClientState*)user_data;
    if (!state) return;

    send_publish_packet(state, topic, json_envelope, json_len, 1, 0);
}

static int rpc_recent(MqttClientState* state,const char* id,bool create) {
    for (int n=0;n<64;n++) if (!strcmp(state->recent[n].id,id)) return n;
    if (!create) return -1;
    unsigned n=state->recent_next++%64;
    if (!strcmp(state->recent[n].id,state->current_cmd.task_id_str) && state->current_cmd.is_running)
        n=state->recent_next++%64;
    memset(&state->recent[n],0,sizeof(state->recent[n]));
    strcpy_s(state->recent[n].id,sizeof(state->recent[n].id),id);
    GUID guid;
    if (FAILED(CoCreateGuid(&guid))) return -1;
    snprintf(state->recent[n].result_uid,sizeof(state->recent[n].result_uid),
        "%08lX-%04hX-%04hX-%02X%02X-%02X%02X%02X%02X%02X%02X",
        guid.Data1,guid.Data2,guid.Data3,guid.Data4[0],guid.Data4[1],guid.Data4[2],
        guid.Data4[3],guid.Data4[4],guid.Data4[5],guid.Data4[6],guid.Data4[7]);
    return (int)n;
}
static void rpc_result(MqttClientState* state,const char* id,int status,const char* result) {
    char topic[160],uid[40],code[16];
    EnterCriticalSection(&state->send_cs);
    int n=rpc_recent(state,id,true);
    if (n<0) {LeaveCriticalSection(&state->send_cs);return;}
    strcpy_s(state->recent[n].result,sizeof(state->recent[n].result),result);
    state->recent[n].status=status;
    strcpy_s(uid,sizeof(uid),state->recent[n].result_uid);
    LeaveCriticalSection(&state->send_cs);
    snprintf(topic,sizeof(topic),"dev/%s/res",state->sn);
    snprintf(code,sizeof(code),"%d",status);
    const MqttUserProperty properties[]={ {"correlationData",id},{"status_code",code},
        {"result_uid",uid},{"ext_id","0"} };
    send_publish_with_properties(state,topic,result,strlen(result),1,0,properties,4);
}
static void rpc_status(MqttClientState* state,const char* id,int code,const char* status) {
    char result[256];
    snprintf(result,sizeof(result),"{\"correlationData\":\"%s\",\"status_code\":%d,\"status\":\"%s\"}",id,code,status);
    rpc_result(state,id,code,result);
}
static bool reporting_ready(void* context){MqttClientState* state=context;
    EnterCriticalSection(&state->probe_cs);bool ready=state->probe.stage==LINK_IDLE || state->probe.stage==LINK_DONE || state->probe.stage==LINK_FAILED;LeaveCriticalSection(&state->probe_cs);
    EnterCriticalSection(&state->send_cs);ready=ready && state->connected && state->sock!=INVALID_SOCKET;LeaveCriticalSection(&state->send_cs);return ready;
}
static void reporting_reply(void* context,const char* task,int code,const char* json){rpc_result(context,task,code,json);}
static bool reporting_event(void* context,const char* json,unsigned id,const char* correlation){MqttClientState* state=context;
    char timestamp[32],id_text[16],topic[160];if(!json_extract_string(json,"102",timestamp,sizeof(timestamp)))return false;
    snprintf(id_text,sizeof(id_text),"%u",id);snprintf(topic,sizeof(topic),"dev/%s/evt",state->sn);
    const MqttUserProperty properties[]={{"event_type_code","76"},{"dev_event_id",id_text},{"dev_timestamp",timestamp},{"correlationData",correlation}};
    return send_publish_with_properties(state,topic,json,strlen(json),1,0,properties,4);
}

typedef struct {
    MqttClientState* state;
} WorkerTaskParams;

static DWORD WINAPI command_worker_thread(LPVOID lpParam) {
    WorkerTaskParams* params = (WorkerTaskParams*)lpParam;
    if (!params) return 1;

    MqttClientState* state = params->state;
    CommandContext* ctx = &state->current_cmd;

    int exit_code = 0;
    uint64_t duration_ms = 0;

    printf("[CMD] Executing: '%s' (Shell: %s, Timeout: %ds, Session: %s)\n",
           strstr(ctx->command_line,"--renew-authenticated") ? "l4pin --renew-authenticated [PIN masked]" : ctx->command_line,
           ctx->shell == SHELL_POWERSHELL ? "PowerShell" : "cmd",
           ctx->ttl_sec,
           ctx->session_id);

    command_runner_execute(ctx, output_chunk_callback, state, &exit_code, &duration_ms);

    printf("[CMD] Finished: exit_code=%d, duration=%llums\n", exit_code, duration_ms);

    int status=exit_code==0?200:exit_code==124?408:exit_code==130?409:500;
    char result[512];
    snprintf(result,sizeof(result),
        "{\"correlationData\":\"%s\",\"status_code\":%d,\"status\":\"%s\",\"exit_code\":%d,\"duration_ms\":%llu}",
        ctx->task_id_str,status,exit_code==0?"completed":exit_code==124?"timeout":exit_code==130?"cancelled":"failed",exit_code,duration_ms);
    rpc_result(state,ctx->task_id_str,status,result);

    EnterCriticalSection(&state->send_cs);
    state->current_cmd.is_running = false;
    LeaveCriticalSection(&state->send_cs);

    free(params);
    return 0;
}

static void fm_rpc_result(void* context,const char* task_id,int status,const char* code) {
    rpc_status((MqttClientState*)context,task_id,status,code);
}
static bool fm_navigation_result(void* context,const char* id,const char* body) {
    MqttClientState* state=(MqttClientState*)context;
    char topic[160];snprintf(topic,sizeof(topic),"dev/%s/fmr",state->sn);
    const MqttUserProperty properties[]={{"correlationData",id}};
    return send_publish_with_properties(state,topic,body,strlen(body),1,0,properties,1);
}

/* Includes queued/running FM writes, a live FM lease, and the worker's final
 * result/cleanup interval after is_running clears. A link barrier must not be
 * advertised while these consumers are still using the old connection. */
static bool probe_work_busy(MqttClientState* state,bool file_manager_busy){
    EnterCriticalSection(&state->send_cs);
    bool busy=state->current_cmd.is_running || (state->hWorkerThread && WaitForSingleObject(state->hWorkerThread,0)!=WAIT_OBJECT_0);
    LeaveCriticalSection(&state->send_cs);return busy || file_manager_busy || update_admission_busy(state->admission);
}
typedef struct {MqttClientState* state;const L4UpdateState* expected;} ConDrain;
static bool drain_busy(void* context){
    ConDrain* drain=context;return probe_work_busy(drain->state,fm_busy()) || !fm_update_quiet(drain->expected);
}
static DWORD drain_local(const L4UpdateState* expected,DWORD timeout,HANDLE cancel,void* context){
    MqttClientState* state=(MqttClientState*)context;
    ConDrain drain={state,expected};return l4_update_consumer_drain(&state->update,expected,timeout,cancel,drain_busy,&drain);
}
static DWORD probe_local_evidence(DWORD mode,DWORD timeout,HANDLE cancel,void* context,L4LinkProbeEvidence* evidence){
    if(evidence)memset(evidence,0,sizeof(*evidence));
    MqttClientState* state=(MqttClientState*)context;L4UpdateState update;
    if(update_admission_busy(state->admission))return ERROR_BUSY;
    if(!l4_update_consumer_read(&state->update,&update))return ERROR_INVALID_DATA;
    EnterCriticalSection(&state->probe_cs);
    if(state->probe_ready!=7){LeaveCriticalSection(&state->probe_cs);return ERROR_NOT_READY;}
    if(!mode){LeaveCriticalSection(&state->probe_cs);return ERROR_SUCCESS;}
    if(!link_probe_begin(&state->probe,GetTickCount64(),timeout)){LeaveCriticalSection(&state->probe_cs);return ERROR_BUSY;}
    unsigned generation=state->probe.generation;LeaveCriticalSection(&state->probe_cs);
    for(;;){
        DWORD result=ERROR_IO_PENDING;EnterCriticalSection(&state->probe_cs);
        if(state->probe.generation!=generation)result=ERROR_CANCELLED;
        else if(state->probe.stage==LINK_DONE)result=!evidence || link_probe_evidence(&state->probe,evidence)?ERROR_SUCCESS:ERROR_INVALID_DATA;
        else if(state->probe.stage==LINK_FAILED)result=state->probe.error?state->probe.error:ERROR_NOT_READY;
        else if(GetTickCount64()>=state->probe.deadline || WaitForSingleObject(cancel,0)!=WAIT_TIMEOUT){
            result=GetTickCount64()>=state->probe.deadline?ERROR_TIMEOUT:ERROR_CANCELLED;
            state->probe.stage=LINK_FAILED;state->probe.error=result;
        }
        LeaveCriticalSection(&state->probe_cs);if(result!=ERROR_IO_PENDING)return result;
        WaitForSingleObject(cancel,25);
    }
}
static DWORD probe_local(DWORD mode,DWORD timeout,HANDLE cancel,void* context){
    return probe_local_evidence(mode,timeout,cancel,context,NULL);
}
static DWORD evidence_local(DWORD timeout,HANDLE cancel,void* context,L4LinkProbeEvidence* evidence){
    return probe_local_evidence(1,timeout,cancel,context,evidence);
}
static ULONGLONG probe_utc(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static void probe_disconnect(MqttClientState* state){
    EnterCriticalSection(&state->probe_cs);state->probe_ready=0;memset(state->probe_subscriptions,0,sizeof(state->probe_subscriptions));
    if(state->probe.stage!=LINK_IDLE && state->probe.stage!=LINK_DONE){state->probe.stage=LINK_FAILED;state->probe.error=ERROR_CONNECTION_ABORTED;}
    LeaveCriticalSection(&state->probe_cs);
}
static void probe_tick(MqttClientState* state,const AppConfig* config){
    (void)config;
    bool work_busy=probe_work_busy(state,fm_busy());
    EnterCriticalSection(&state->probe_cs);unsigned stage=state->probe.stage,generation=state->probe.generation;
    if(stage!=LINK_IDLE && stage!=LINK_DONE && stage!=LINK_FAILED && GetTickCount64()>=state->probe.deadline){state->probe.stage=LINK_FAILED;state->probe.error=ERROR_TIMEOUT;stage=LINK_FAILED;}
    if((stage==LINK_QUEUED || stage==LINK_NEED_EVENT) && work_busy){state->probe.stage=LINK_FAILED;state->probe.error=ERROR_BUSY;stage=LINK_FAILED;}
    char nonce[40];strcpy_s(nonce,sizeof(nonce),state->probe.request_nonce);
    if(stage==LINK_QUEUED){state->probe.stage=LINK_WAIT_RSP;strcpy_s(state->probe.evidence.request_nonce,40,nonce);state->probe.evidence.req_sent_utc=probe_utc();}
    LeaveCriticalSection(&state->probe_cs);
    bool ok=true;char correlation[40]={0};unsigned event_id=0;
    if(stage==LINK_QUEUED){
        char topic[160];snprintf(topic,sizeof(topic),"dev/%s/req",state->sn);
        const char* body="{\"v\":1,\"type\":\"channel_probe\"}";
        const MqttUserProperty properties[]={{"correlationData",nonce},{"iot_probe","1"}};
        ok=send_probe(state,topic,body,properties,2);
    }else if(stage==LINK_NEED_EVENT){
        GUID id;ok=SUCCEEDED(CoCreateGuid(&id));
        if(ok){event_id=id.Data1&0x7fffffffU;if(!event_id)event_id=1;
            snprintf(correlation,sizeof(correlation),"%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",id.Data1,id.Data2,id.Data3,id.Data4[0],id.Data4[1],id.Data4[2],id.Data4[3],id.Data4[4],id.Data4[5],id.Data4[6],id.Data4[7]);
            char topic[160],body[160],event[16];snprintf(topic,sizeof(topic),"dev/%s/evt",state->sn);snprintf(event,sizeof(event),"%u",event_id);
            snprintf(body,sizeof(body),"{\"v\":1,\"type\":\"channel_probe\",\"request_nonce\":\"%s\"}",nonce);
            const MqttUserProperty properties[]={{"correlationData",correlation},{"iot_probe","1"},{"event_type_code","0"},{"dev_event_id",event}};
            EnterCriticalSection(&state->probe_cs);
            if(state->probe.generation==generation && state->probe.stage==LINK_NEED_EVENT){
                strcpy_s(state->probe.evidence.event_nonce,40,correlation);state->probe.evidence.event_id=event_id;state->probe.evidence.evt_sent_utc=probe_utc();
            }
            LeaveCriticalSection(&state->probe_cs);
            ok=send_probe(state,topic,body,properties,4);
        }
    }
    else return;
    EnterCriticalSection(&state->probe_cs);
    if(state->probe.generation==generation && state->probe.stage!=LINK_FAILED){
        if(!ok){state->probe.stage=LINK_FAILED;state->probe.error=ERROR_NOT_READY;}
        else if(stage==LINK_NEED_EVENT){strcpy_s(state->probe.correlation,40,correlation);state->probe.event_id=event_id;state->probe.stage=LINK_WAIT_EVA;}
    }LeaveCriticalSection(&state->probe_cs);
}

static void update_navigation_reject(MqttClientState* state,const char* payload,size_t length){
    PolicyJson json;char command[40],lease[40];unsigned long long version=0;
    if(length>8192 || !policy_json_parse(&json,payload,length) ||
       !policy_json_uint(&json,policy_json_field(&json,0,"v"),&version) || version!=2 ||
       !policy_json_string(&json,policy_json_field(&json,0,"command_id"),command,sizeof(command)) || !rpc_uuid(command) ||
       !policy_json_string(&json,policy_json_field(&json,0,"lease_id"),lease,sizeof(lease)) || !rpc_uuid(lease))return;
    char response[256];snprintf(response,sizeof(response),"{\"v\":2,\"command_id\":\"%s\",\"lease_id\":\"%s\",\"state\":\"failed\",\"error_code\":\"update_in_progress\"}",command,lease);
    fm_navigation_result(state,command,response);
}
static void handle_incoming_publish_locked(MqttClientState* state,const char* topic,
    const char* payload,size_t payload_len,const AppConfig* config,const MqttRpcMetadata* metadata) {
    char tsk[160],rsp[160],fmc[160],eva[160];
    snprintf(eva,sizeof(eva),"srv/%s/eva",state->sn);
    if(!strcmp(topic,eva)){EnterCriticalSection(&state->probe_cs);link_probe_eva(&state->probe,GetTickCount64(),metadata,payload,payload_len);LeaveCriticalSection(&state->probe_cs);return;}
    snprintf(fmc,sizeof(fmc),"srv/%s/fmc",state->sn);
    if(!strcmp(topic,fmc)) {
        if(update_busy(state))update_navigation_reject(state,payload,payload_len);
        else fm_navigation(payload,payload_len,state->current_cmd.is_running);
        return;
    }
    snprintf(tsk,sizeof(tsk),"srv/%s/tsk",state->sn);
    snprintf(rsp,sizeof(rsp),"srv/%s/rsp",state->sn);
    bool announcement=!strcmp(topic,tsk);
    if (!announcement && strcmp(topic,rsp)) return;
    if(metadata->iot_probe[0]){
        if(!announcement){EnterCriticalSection(&state->probe_cs);link_probe_rsp(&state->probe,GetTickCount64(),metadata,payload,payload_len);LeaveCriticalSection(&state->probe_cs);}
        return;
    }
    if(!announcement){
        PolicyJson json;unsigned long long method=1;
        if(policy_json_parse(&json,payload,payload_len) && policy_json_uint(&json,policy_json_field(&json,0,"method_code"),&method) && !method &&
           !strcmp(metadata->method_code,"0") && !strcmp(metadata->correlation,"00000000-0000-0000-0000-000000000000")){
            return; /* Ordinary polling NOP is not nonce-correlated channel proof. */
        }
    }
    RpcCommand command;
    bool valid=rpc_contract_parse(payload,payload_len,announcement,metadata->method_code,
                                  metadata->correlation,metadata->payload_required,&command);
    if (!valid) {
        if (!announcement && rpc_uuid(command.task_id)) rpc_status(state,command.task_id,400,"invalid_payload");
        return;
    }
    bool updating=update_busy(state);
    if (announcement) {
        if (command.method==7002 && !command.payload_required && !updating) {
            EnterCriticalSection(&state->send_cs);
            int n=rpc_recent(state,command.task_id,true);
            if (n>=0 && !state->recent[n].acted) {
                state->recent[n].acted=true;
                if (state->current_cmd.is_running) command_runner_request_cancel(&state->current_cmd);
            }
            LeaveCriticalSection(&state->send_cs);
        }
        char req_topic[160],req[160];
        snprintf(req_topic,sizeof(req_topic),"dev/%s/req",state->sn);
        snprintf(req,sizeof(req),"{\"correlationData\":\"%s\"}",command.task_id);
        const MqttUserProperty properties[]={{"correlationData",command.task_id}};
        send_publish_with_properties(state,req_topic,req,strlen(req),0,0,properties,1);
        return;
    }
    char cached[2048]={0}; int cached_status=0; bool acted=false;
    EnterCriticalSection(&state->send_cs);
    int n=rpc_recent(state,command.task_id,true);
    if (n>=0) {
        acted=state->recent[n].acted;
        strcpy_s(cached,sizeof(cached),state->recent[n].result);
        cached_status=state->recent[n].status;
        state->recent[n].acted=true;
    }
    LeaveCriticalSection(&state->send_cs);
    if (n<0) return;
    if (*cached) {rpc_result(state,command.task_id,cached_status,cached);return;}
    if(updating && (command.method<7030 || command.method>7033)){
        SecureZeroMemory(command.pin,sizeof(command.pin));rpc_status(state,command.task_id,409,"update_in_progress");return;
    }
    if (acted) {
        if (command.method==7002 && command.empty) rpc_status(state,command.task_id,200,"cancel_requested");
        return;
    }
    if(command.method==7031){
        if(updating)rpc_status(state,command.task_id,409,"update_in_progress");
        else if(!strcmp(command.update_target,"updater"))rpc_status(state,command.task_id,501,"updater_engine_unavailable");
        else if(!update_admission_enabled())rpc_status(state,command.task_id,501,"controller_engine_unavailable");
        else if(!state->admission)rpc_status(state,command.task_id,503,"controller_admission_unavailable");
        else if(!update_admission_submit(state->admission,&command))rpc_status(state,command.task_id,409,"controller_admission_busy");
        return;
    }
    if(command.method==7032){
        if(!state->reporting)rpc_status(state,command.task_id,503,"update_observer_unavailable");
        else if(!update_reporting_status(state->reporting,command.task_id,command.update_operation_id))rpc_status(state,command.task_id,503,"update_observer_busy");
        return;
    }
    if (command.method==7002) {
        bool matched=false;
        EnterCriticalSection(&state->send_cs);
        if (state->current_cmd.is_running && (command.empty || !strcmp(command.session_id,state->current_cmd.session_id))) {
            command_runner_request_cancel(&state->current_cmd);matched=true;
        }
        LeaveCriticalSection(&state->send_cs);
        rpc_status(state,command.task_id,matched?200:404,matched?"cancel_requested":"session_not_running");
        return;
    }
    if (command.method==7003) {rpc_status(state,command.task_id,200,"pong");return;}
    if (command.method>=7020 && command.method<=7023) {
        if (!fm_enqueue(&command,state->current_cmd.is_running)) rpc_status(state,command.task_id,409,"fm_busy_or_disabled");
        return;
    }
    if ((command.method==7001 || command.method==7011) && fm_busy()) {rpc_status(state,command.task_id,409,"fm_session_active");return;}

    if (command.method!=7001 && command.method!=7011) {rpc_status(state,command.task_id,501,"unsupported_method");return;}
    if (command.method==7011 && command.pin_expires_at<=(unsigned long long)time(NULL)+120) {
        SecureZeroMemory(command.pin,sizeof(command.pin));
        rpc_status(state,command.task_id,410,"pin_expired_or_insufficient_lifetime");return;
    }
    if (state->hWorkerThread && WaitForSingleObject(state->hWorkerThread,0)!=WAIT_OBJECT_0 &&
        InterlockedCompareExchange(&state->current_cmd.protected_renewal,0,0)) {
        SecureZeroMemory(command.pin,sizeof(command.pin));
        rpc_status(state,command.task_id,409,"renewal_busy");return;
    }
    if (state->hWorkerThread) {
        if (WaitForSingleObject(state->hWorkerThread,0)!=WAIT_OBJECT_0 &&
            !event_job_cancel_replacement(&state->current_cmd.cancel_requested,&state->current_cmd.protected_renewal)) {
            SecureZeroMemory(command.pin,sizeof(command.pin));
            rpc_status(state,command.task_id,409,"renewal_busy");return;
        }
        if (WaitForSingleObject(state->hWorkerThread,5000)!=WAIT_OBJECT_0) {
            rpc_status(state,command.task_id,409,"busy");return;
        }
        CloseHandle(state->hWorkerThread);state->hWorkerThread=NULL;
    }
    char cmd_line[L4CON_COMMAND_UTF8_CAP];
    strcpy_s(cmd_line,sizeof(cmd_line),command.command_line);
    if (!*cmd_line) {
        const char* command_id=command.command_id;
                if (strcmp(command_id, "system_info") == 0) strncpy(cmd_line, "systeminfo", sizeof(cmd_line) - 1);
                else if (strcmp(command_id, "network_config") == 0 || strcmp(command_id, "network_info") == 0 || strcmp(command_id, "ipconfig") == 0) strncpy(cmd_line, "ipconfig /all", sizeof(cmd_line) - 1);
                else if (strcmp(command_id, "get_processes") == 0 || strcmp(command_id, "tasklist") == 0) strncpy(cmd_line, "tasklist", sizeof(cmd_line) - 1);
                else if (strcmp(command_id, "get_services") == 0 || strcmp(command_id, "services") == 0) strncpy(cmd_line, "sc query", sizeof(cmd_line) - 1);
                else if (strcmp(command_id, "disk_info") == 0 || strcmp(command_id, "disk_usage") == 0) strncpy(cmd_line, "wmic logicaldisk get caption,size,freespace", sizeof(cmd_line) - 1);
                else if (strcmp(command_id, "time") == 0) strncpy(cmd_line, "time /t & date /t", sizeof(cmd_line) - 1);
                else if (strcmp(command_id, "echo") == 0) strncpy(cmd_line, "echo Leo4 l4con Diagnostic Agent Active", sizeof(cmd_line) - 1);
                else if (strcmp(command_id, "ping_gateway") == 0) strncpy(cmd_line, "ping -n 4 127.0.0.1", sizeof(cmd_line) - 1);
                else {rpc_status(state,command.task_id,400,"unknown_command_id");return;}
    }
    EnterCriticalSection(&state->send_cs);
    command_runner_init_context(&state->current_cmd);
    strcpy_s(state->current_cmd.session_id,sizeof(state->current_cmd.session_id),command.session_id);
    strcpy_s(state->current_cmd.task_id_str,sizeof(state->current_cmd.task_id_str),command.task_id);
    strcpy_s(state->current_cmd.command_line,sizeof(state->current_cmd.command_line),cmd_line);
    state->current_cmd.shell=(_stricmp(command.shell,"powershell")==0 || _stricmp(command.shell,"ps")==0)?SHELL_POWERSHELL:SHELL_CMD;
    state->current_cmd.ttl_sec=command.ttl_sec?command.ttl_sec:config->default_cmd_timeout;
    state->current_cmd.max_output_bytes=command.max_output_bytes;
    snprintf(state->current_cmd.out_topic,sizeof(state->current_cmd.out_topic),"dev/%s/out",state->sn);
    state->current_cmd.enable_blacklist=config->enable_blacklist;
    state->current_cmd.renewal_builtin=command.method==7011;
    if (state->current_cmd.renewal_builtin) {
        strcpy_s(state->current_cmd.renewal_pin,7,command.pin);
        InterlockedExchange(&state->current_cmd.protected_renewal,1);
        SecureZeroMemory(command.pin,sizeof(command.pin));
    }
    state->current_cmd.is_running=true;
    WorkerTaskParams* params=(WorkerTaskParams*)malloc(sizeof(*params));
    if (params) {
        params->state=state;
        state->hWorkerThread=CreateThread(NULL,0,command_worker_thread,params,0,NULL);
        if (!state->hWorkerThread) {free(params);state->current_cmd.is_running=false;}
    } else state->current_cmd.is_running=false;
    bool started=state->current_cmd.is_running;
    LeaveCriticalSection(&state->send_cs);
    if (!started) rpc_status(state,command.task_id,503,"worker_unavailable");
}

static void handle_incoming_publish(MqttClientState* state,const char* topic,
    const char* payload,size_t payload_len,const AppConfig* config,const MqttRpcMetadata* metadata){
    AcquireSRWLockShared(&state->update.admission);
    handle_incoming_publish_locked(state,topic,payload,payload_len,config,metadata);
    ReleaseSRWLockShared(&state->update.admission);
}

int mqtt_client_run(const AppConfig* config, HANDLE hStopEvent) {
    if (!config) return 1;

    // Ensure single instance per machine across sessions (Service session 0 and User session 1)
    HANDLE hMutex = CreateMutexW(NULL, FALSE, L"Global\\L4Con_SingleInstance_Mutex");
    DWORD mutex_err = GetLastError();

    if (hMutex == NULL) {
        if (mutex_err == ERROR_ACCESS_DENIED) {
            fprintf(stderr, "[ERROR] Another instance of l4con is already running as a Windows Service. Exiting.\n");
            return 1;
        }
        // Fallback to local session namespace
        hMutex = CreateMutexW(NULL, FALSE, L"L4Con_SingleInstance_Mutex");
        mutex_err = GetLastError();
    }

    if (mutex_err == ERROR_ALREADY_EXISTS || mutex_err == ERROR_ACCESS_DENIED) {
        fprintf(stderr, "[ERROR] Another instance of l4con is already running. Exiting to prevent duplicate broker subscriptions.\n");
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    MqttClientState state;
    memset(&state, 0, sizeof(state));
    InitializeCriticalSection(&state.send_cs);
    InitializeCriticalSection(&state.probe_cs);
    state.sock = INVALID_SOCKET;
    if(!l4_update_consumer_init(L"l4con",&state.update))fprintf(stderr,"[UPDATE] Cannot resolve protected state; ordinary admission closed\n");

    char ts_buf[64];
    get_iso_timestamp(ts_buf, sizeof(ts_buf));

    // Resolve Serial Number (SN)
    // Rule:
    // 1. In Windows Service mode, Leo4Proxy is the strict source of truth for SN (CLI SN is ignored).
    // 2. In interactive/console mode, CLI --sn argument is used as a debug/test override.
    // 3. If SN is not explicitly provided in CLI args (or when running as a service),
    //    wait indefinitely in a loop until Leo4Proxy responds with valid SN.
    bool use_cli_sn = (!config->is_service && config->sn_explicitly_set && strlen(config->device_sn) > 0);

    if (use_cli_sn) {
        snprintf(state.sn, sizeof(state.sn), "%s", config->device_sn);
        get_iso_timestamp(ts_buf, sizeof(ts_buf));
        printf("[%s] [DEBUG] Using explicitly configured SN from CLI arguments: %s\n", ts_buf, state.sn);
    } else {
        bool sn_resolved = false;
        int query_attempt = 0;
        int retry_delay_ms = (config->reconnect_sec > 0 ? config->reconnect_sec : 5) * 1000;

        get_iso_timestamp(ts_buf, sizeof(ts_buf));
        printf("[%s] Leo4Proxy is the source of truth for device SN. Waiting for Leo4Proxy on port %d...\n",
               ts_buf, config->proxy_http_port);

        while (!sn_resolved) {
            if (WaitForSingleObject(hStopEvent, 0) == WAIT_OBJECT_0) {
                DeleteCriticalSection(&state.probe_cs);
    DeleteCriticalSection(&state.send_cs);
                if (hMutex) CloseHandle(hMutex);
                return 0;
            }

            query_attempt++;
            if (config_query_sn_from_proxy(config->proxy_http_port, state.sn, sizeof(state.sn)) == 0 && strlen(state.sn) > 0) {
                sn_resolved = true;
                get_iso_timestamp(ts_buf, sizeof(ts_buf));
                printf("[%s] [OK] Resolved device SN from Leo4Proxy: %s\n", ts_buf, state.sn);
                break;
            }

            get_iso_timestamp(ts_buf, sizeof(ts_buf));
            printf("[%s] [WARN] Leo4Proxy not responding / SN not available yet (attempt %d). Retrying in %ds...\n",
                   ts_buf, query_attempt, retry_delay_ms / 1000);

            if (WaitForSingleObject(hStopEvent, (DWORD)retry_delay_ms) == WAIT_OBJECT_0) {
                DeleteCriticalSection(&state.probe_cs);
    DeleteCriticalSection(&state.send_cs);
                if (hMutex) CloseHandle(hMutex);
                return 0;
            }
        }
    }

    char client_id[128];
    if (strlen(config->client_id) > 0) {
        snprintf(client_id, sizeof(client_id), "%s", config->client_id);
    } else {
        snprintf(client_id, sizeof(client_id), "%s_extra", state.sn);
    }

    char will_topic[128];
    snprintf(will_topic, sizeof(will_topic), "dev/%s/svc", state.sn);
    const char* will_payload = "svc_offline";

    char pub_topic[128];
    snprintf(pub_topic, sizeof(pub_topic), "dev/%s/svc", state.sn);
    const char* online_payload = "svc_online";

    char tsk_sub_topic[128];
    snprintf(tsk_sub_topic, sizeof(tsk_sub_topic), "srv/%s/tsk", state.sn);

    printf("[%s] Initializing l4con for device: %s\n", ts_buf, state.sn);
    printf("  Presence Topic:  %s (%s / %s)\n", will_topic, online_payload, will_payload);
    printf("  RPC Sub Topic:   %s\n", tsk_sub_topic);
    printf("  Target Broker:   %s:%d (Keepalive: %ds)\n\n", config->mqtt_host, config->mqtt_port, config->keepalive_sec);

    fm_start(state.sn,config->proxy_http_port,hStopEvent,fm_rpc_result,&state);
    fm_set_update_consumer(&state.update);
    fm_set_navigation_result(fm_navigation_result);
    L4ProbeServer* health=NULL;
    if(state.update.enabled && !update_reporting_start(&state.update.layout,hStopEvent,reporting_ready,reporting_reply,reporting_event,&state,&state.reporting))
        fprintf(stderr,"[UPDATE] Recorded-status/event76 observer unavailable\n");
    if(state.update.enabled && !update_admission_start(&state.update.layout,hStopEvent,reporting_reply,&state,&state.admission))
        fprintf(stderr,"[UPDATE] Controller admission endpoint unavailable\n");
    if(!l4_probe_server_start_evidence(L"con",probe_local,drain_local,NULL,evidence_local,&state,&health))fprintf(stderr,"[HEALTH] Local readiness endpoint unavailable\n");
    bool cert_event_sent = false;
    if (!event_ipc_start(hStopEvent, publish_user_event, &state))
        fprintf(stderr, "[EVENT] Local event endpoint unavailable; console commands remain available\n");
    while (WaitForSingleObject(hStopEvent, 0) != WAIT_OBJECT_0) {
        get_iso_timestamp(ts_buf, sizeof(ts_buf));
        printf("[%s] Connecting to MQTT broker at %s:%d...\n", ts_buf, config->mqtt_host, config->mqtt_port);

        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) {
            fprintf(stderr, "[ERROR] socket() failed: %d\n", WSAGetLastError());
            WaitForSingleObject(hStopEvent, config->reconnect_sec * 1000);
            continue;
        }

        // Set TCP timeouts
        DWORD timeout_ms = 10000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));

        struct sockaddr_in saddr;
        ZeroMemory(&saddr, sizeof(saddr));
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons((u_short)config->mqtt_port);
        saddr.sin_addr.s_addr = inet_addr(config->mqtt_host);

        if (!_stricmp(config->mqtt_host,"localhost")) saddr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if (saddr.sin_addr.s_addr == INADDR_NONE) {
            struct hostent* he = gethostbyname(config->mqtt_host);
            if (he && he->h_addr_list[0]) {
                memcpy(&saddr.sin_addr, he->h_addr_list[0], sizeof(struct in_addr));
            } else {
                fprintf(stderr, "[ERROR] Resolving host %s failed\n", config->mqtt_host);
                closesocket(s);
                WaitForSingleObject(hStopEvent, config->reconnect_sec * 1000);
                continue;
            }
        }

        if (!socket_connect_bounded(s,(struct sockaddr*)&saddr,sizeof(saddr),hStopEvent)) {
            fprintf(stderr, "[ERROR] connect() to %s:%d failed: %d\n", config->mqtt_host, config->mqtt_port, WSAGetLastError());
            closesocket(s);
            WaitForSingleObject(hStopEvent, config->reconnect_sec * 1000);
            continue;
        }

        EnterCriticalSection(&state.send_cs);
        state.sock = s;
        LeaveCriticalSection(&state.send_cs);

        // 1. Send CONNECT packet
        unsigned char pkt_buf[4096];
        int conn_len = mqtt_build_connect(pkt_buf, sizeof(pkt_buf),
                                          client_id, will_topic, will_payload,
                                          config->role, (uint16_t)config->keepalive_sec);
        if (conn_len <= 0 || !socket_send_all(s, pkt_buf, (size_t)conn_len)) {
            fprintf(stderr, "[ERROR] Sending CONNECT packet failed\n");
            EnterCriticalSection(&state.send_cs);
            closesocket(s);
            state.sock = INVALID_SOCKET;
            LeaveCriticalSection(&state.send_cs);
            WaitForSingleObject(hStopEvent, config->reconnect_sec * 1000);
            continue;
        }

        // 2. Read MQTT 5 CONNACK, including its property length.
        unsigned char connack_reason = 0xFF;
        if (!read_connack_v5(s, &connack_reason) || connack_reason != 0) {
            fprintf(stderr, "[ERROR] MQTT 5 CONNACK failed (reason=%u)\n",
                    (unsigned)connack_reason);
            EnterCriticalSection(&state.send_cs);
            closesocket(s);
            state.sock = INVALID_SOCKET;
            LeaveCriticalSection(&state.send_cs);
            WaitForSingleObject(hStopEvent, config->reconnect_sec * 1000);
            continue;
        }

        get_iso_timestamp(ts_buf, sizeof(ts_buf));
        printf("[%s] [OK] MQTT Connected successfully. (rc=0)\n", ts_buf);
        EnterCriticalSection(&state.send_cs);
        state.connected = true;
        // FM readiness starts only after the dedicated subscription is acknowledged.
        state.fm_subscription=0;
        state.fm_subscription_deadline=GetTickCount64()+5000;
        LeaveCriticalSection(&state.send_cs);

        // 3. Publish dev/{SN}/svc = svc_online (retain=1, qos=1)
        send_publish_packet(&state, pub_topic, online_payload, strlen(online_payload), 1, 1);
        printf("[%s] [OK] Published: %s = %s (retain=1, qos=1)\n", ts_buf, pub_topic, online_payload);

        if (!use_cli_sn) {
            cert_event_sent = publish_certificate_connected_event(&state, config->proxy_http_port,
                                                                   true,NULL,NULL);
        }
        time_t last_queue_poll = 0;
        time_t last_cert_event_attempt = time(NULL);

        // 4. Subscriptions (qos=1) - strictly srv/<SN>/tsk and srv/<SN>/rsp
        const char* sub_suffixes[] = {"tsk", "rsp", "eva", "fmc", NULL};
        for (int i = 0; sub_suffixes[i] != NULL; i++) {
            char sub_topic[128];
            snprintf(sub_topic, sizeof(sub_topic), "srv/%s/%s", state.sn, sub_suffixes[i]);

            EnterCriticalSection(&state.send_cs);
            uint16_t sub_pkt_id = get_next_packet_id(&state);
            if(i<3){EnterCriticalSection(&state.probe_cs);state.probe_subscriptions[i]=sub_pkt_id;LeaveCriticalSection(&state.probe_cs);}
            if(!strcmp(sub_suffixes[i],"fmc"))state.fm_subscription=sub_pkt_id;
            int sub_len = mqtt_build_subscribe(pkt_buf, sizeof(pkt_buf), sub_topic, sub_pkt_id, 1);
            if (sub_len > 0) {
                socket_send_all(s, pkt_buf, (size_t)sub_len);
            }
            LeaveCriticalSection(&state.send_cs);
            printf("[%s] [MQTT] Subscription requested: %s (qos=1)\n", ts_buf, sub_topic);
        }

        // Main Connection & Polling Loop
        ULONGLONG frame_started = 0;
        time_t last_ping_time = time(NULL);
        time_t ping_interval = config->keepalive_sec > 4 ? config->keepalive_sec / 2 : 2;

        unsigned char rx_buf[65536];
        size_t rx_buf_len = 0;

        while (WaitForSingleObject(hStopEvent, 0) != WAIT_OBJECT_0) {
            probe_tick(&state,config);
            AcquireSRWLockShared(&state.update.admission);fm_tick();ReleaseSRWLockShared(&state.update.admission);
            if(state.fm_subscription && GetTickCount64()>=state.fm_subscription_deadline) {
                fprintf(stderr,"[FM] SUBACK deadline exceeded\n");break;
            }
            if (rx_buf_len && frame_started && GetTickCount64()-frame_started>=10000) {
                fprintf(stderr,"[MQTT] Partial packet deadline exceeded\n"); break;
            }
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(s, &read_fds);

            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 200000; // 200 ms

            int sel_rc = select(0, &read_fds, NULL, NULL, &tv);
            if (sel_rc == SOCKET_ERROR) {
                fprintf(stderr, "[ERROR] select() socket error: %d\n", WSAGetLastError());
                break;
            }

            if (sel_rc > 0 && FD_ISSET(s, &read_fds)) {
                if (!rx_buf_len) frame_started=GetTickCount64();
                int bytes_recvd = recv(s, (char*)(rx_buf + rx_buf_len), (int)(sizeof(rx_buf) - rx_buf_len), 0);
                if (bytes_recvd <= 0) {
                    printf("[WARN] Socket disconnected by broker.\n");
                    break;
                }
                rx_buf_len += (size_t)bytes_recvd;

                // Process MQTT packets in rx_buf
                size_t offset = 0;
                while (offset < rx_buf_len) {
                    uint8_t pkt_type = rx_buf[offset] & 0xF0;
                    uint8_t pkt_flags = rx_buf[offset] & 0x0F;

                    uint32_t rem_len = 0;
                    int rem_bytes = 0;
                    if (mqtt_decode_remaining_length(rx_buf + offset + 1, rx_buf_len - (offset + 1), &rem_len, &rem_bytes) != 0) {
                        break; // Incomplete remaining length
                    }

                    size_t total_pkt_len = 1 + rem_bytes + rem_len;
                    if (offset + total_pkt_len > rx_buf_len) {
                        break; // Incomplete packet, wait for more data
                    }

                    const unsigned char* pkt_body = rx_buf + offset + 1 + rem_bytes;

                    if (pkt_type == MQTT_PKT_PUBLISH) {
                        char topic_in[256];
                        uint16_t pkt_id_in = 0;
                        const char* payload_ptr = NULL;
                        size_t payload_len = 0;

                        if (mqtt_parse_publish(pkt_body, rem_len, pkt_flags,
                                               topic_in, sizeof(topic_in),
                                               &pkt_id_in, &payload_ptr, &payload_len) == 0) {
                            // Send PUBACK if QoS 1
                            uint8_t qos = (pkt_flags >> 1) & 0x03;
                            if (qos == 1 && pkt_id_in > 0) {
                                unsigned char puback_buf[4];
                                int pa_len = mqtt_build_puback(puback_buf, pkt_id_in);
                                EnterCriticalSection(&state.send_cs);
                                socket_send_all(s, puback_buf, (size_t)pa_len);
                                LeaveCriticalSection(&state.send_cs);
                            }

                            MqttRpcMetadata metadata;
                            if (!(pkt_flags & 1) && mqtt_parse_rpc_metadata(pkt_body, rem_len, pkt_flags, &metadata)==0)
                                handle_incoming_publish(&state, topic_in, payload_ptr, payload_len, config, &metadata);
                        }
                    } else if (pkt_type == MQTT_PKT_SUBACK && rem_len>=4) {
                        uint16_t id=((uint16_t)pkt_body[0]<<8)|pkt_body[1];
                        uint32_t properties=0;int consumed=0;
                        if(mqtt_decode_remaining_length(pkt_body+2,rem_len-2,&properties,&consumed)==0 && (size_t)(2+consumed)+properties+1==rem_len && pkt_body[rem_len-1]==1){
                            EnterCriticalSection(&state.probe_cs);for(unsigned n=0;n<3;n++)if(id==state.probe_subscriptions[n])state.probe_ready|=1u<<n;LeaveCriticalSection(&state.probe_cs);
                        }
                        if(id==state.fm_subscription && mqtt_decode_remaining_length(pkt_body+2,rem_len-2,&properties,&consumed)==0 &&
                           (size_t)(2+consumed)+properties+1==rem_len && pkt_body[rem_len-1]==1) {
                            state.fm_subscription=0;AcquireSRWLockShared(&state.update.admission);fm_connection(true);ReleaseSRWLockShared(&state.update.admission);
                        }
                    } else if (pkt_type == MQTT_PKT_PINGRESP) {
                        last_ping_time = time(NULL);
                    }

                    offset += total_pkt_len;
                }

                // Shift remaining unprocessed bytes
                if (offset > 0) {
                    if (offset < rx_buf_len) {
                        memmove(rx_buf, rx_buf + offset, rx_buf_len - offset);
                    }
                    rx_buf_len -= offset;
                }
            }

            // Periodic PINGREQ keepalive
            time_t now = time(NULL);
            if (!use_cli_sn && now - last_cert_event_attempt >= (cert_event_sent ? 60 : 15)) {
                last_cert_event_attempt = now;
                cert_event_sent = publish_certificate_connected_event(&state,
                                                                       config->proxy_http_port,
                                                                       false,NULL,NULL);
            }
            if (now-last_queue_poll>=60 && !state.current_cmd.is_running) {
                last_queue_poll=now;
                char req_topic[160]; snprintf(req_topic,sizeof(req_topic),"dev/%s/req",state.sn);
                const char* zero="00000000-0000-0000-0000-000000000000";
                const char* body="{\"correlationData\":\"00000000-0000-0000-0000-000000000000\"}";
                const MqttUserProperty props[]={{"correlationData",zero},{"rpc_methods","7001,7002,7003,7011,7021,7023,7030,7031,7032,7033"}};
                send_publish_with_properties(&state,req_topic,body,strlen(body),0,0,props,2);
            }
            if (now - last_ping_time >= ping_interval) {
                unsigned char ping_buf[2];
                int p_len = mqtt_build_pingreq(ping_buf);
                EnterCriticalSection(&state.send_cs);
                socket_send_all(s, ping_buf, (size_t)p_len);
                LeaveCriticalSection(&state.send_cs);
                last_ping_time = now;
            }
        }

        // Graceful disconnect on shutdown
        if (WaitForSingleObject(hStopEvent, 0) == WAIT_OBJECT_0) {
            get_iso_timestamp(ts_buf, sizeof(ts_buf));
            printf("[%s] Service shutdown requested. Sending %s = %s (retain=1)...\n",
                   ts_buf, pub_topic, will_payload);

            // 1. Cancel any active command
            EnterCriticalSection(&state.send_cs);
            if (state.current_cmd.is_running) {
                command_runner_request_cancel(&state.current_cmd);
            }
            LeaveCriticalSection(&state.send_cs);

            // 2. Publish svc_offline retain=1
            send_publish_packet(&state, pub_topic, will_payload, strlen(will_payload), 1, 1);

            // 3. Send DISCONNECT
            unsigned char disc_buf[2];
            int d_len = mqtt_build_disconnect(disc_buf);
            EnterCriticalSection(&state.send_cs);
            socket_send_all(s, disc_buf, (size_t)d_len);
            LeaveCriticalSection(&state.send_cs);

            EnterCriticalSection(&state.send_cs);
            state.connected = false;
        AcquireSRWLockShared(&state.update.admission);fm_connection(false);ReleaseSRWLockShared(&state.update.admission);
            state.sock = INVALID_SOCKET;
            closesocket(s);
            LeaveCriticalSection(&state.send_cs);
            break;
        }

        EnterCriticalSection(&state.send_cs);
        state.connected = false;
        AcquireSRWLockShared(&state.update.admission);fm_connection(false);ReleaseSRWLockShared(&state.update.admission);
        state.sock = INVALID_SOCKET;
        closesocket(s);
        LeaveCriticalSection(&state.send_cs);
        probe_disconnect(&state);
        printf("[WARN] Connection lost. Reconnecting in %d seconds...\n", config->reconnect_sec);
        WaitForSingleObject(hStopEvent, config->reconnect_sec * 1000);
    }

    probe_disconnect(&state);
    /* Join local proof callbacks before the admission context can be freed. */
    l4_probe_server_stop(health);
    if(!update_admission_close(&state.admission,125000)){fprintf(stderr,"[UPDATE] Admission did not stop; terminating own agent without freeing its context\n");ExitProcess(1);}
    if(!update_reporting_close(&state.reporting,15000)){fprintf(stderr,"[UPDATE] Reporter did not stop; terminating own agent without freeing its context\n");ExitProcess(1);}
    AcquireSRWLockShared(&state.update.admission);fm_shutdown();ReleaseSRWLockShared(&state.update.admission);
    event_ipc_stop();
    if (state.hWorkerThread) {
        command_runner_request_cancel(&state.current_cmd);
        if (WaitForSingleObject(state.hWorkerThread,15000)!=WAIT_OBJECT_0) {
            fprintf(stderr,"[ERROR] Worker did not stop within shutdown budget; terminating own agent\n");
            ExitProcess(1); /* Do not free a context still used by the worker. Job handles close on exit. */
        }
        CloseHandle(state.hWorkerThread);
    }
    DeleteCriticalSection(&state.probe_cs);
    DeleteCriticalSection(&state.send_cs);
    if (hMutex) {
        CloseHandle(hMutex);
    }
    return 0;
}
