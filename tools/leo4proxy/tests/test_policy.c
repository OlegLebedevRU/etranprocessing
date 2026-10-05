/* Include the module to exercise clock/socket transitions without touching real storage or services. */
#include "../src/policy.c"
#include "../src/rtp_tunnel.h"
#include <assert.h>
ProxyStats g_proxyStats={0};

static void response_tests(void) {
    const char* framed[]={
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}\r\n0\r\n\r\n"
    };
    for(int n=0;n<2;n++) {
        assert(http_framed_complete(framed[n],strlen(framed[n])));
        for(size_t k=0;k<strlen(framed[n]);k++) {char partial[128];memcpy(partial,framed[n],k);partial[k]=0;assert(!http_framed_complete(partial,k));}
    }
    const char* negative="HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n{}";
    assert(!http_framed_complete(negative,strlen(negative)));
    bool media=false,https=false; char reasons[1024];
    const char* good="{\"v\":1,\"sn\":\"773\",\"mqtt_rtp_allowed\":false,\"outgoing_https_allowed\":false,\"stop_facts\":[\"terminal_inactive\",\"future_fact\"]}";
    assert(policy_response_parse(good,strlen(good),"773",&media,&https,reasons,sizeof(reasons)));
    assert(!media && !https && !strcmp(reasons,"terminal_inactive,future_fact"));
    const char* missing="{\"v\":1,\"sn\":\"773\",\"mqtt_rtp_allowed\":true,\"stop_facts\":[],\"future\":{\"outgoing_https_allowed\":false}}";
    assert(policy_response_parse(missing,strlen(missing),"773",&media,&https,reasons,sizeof(reasons)) && media && https);
    const char* nonbool="{\"v\":1,\"sn\":\"773\",\"mqtt_rtp_allowed\":true,\"outgoing_https_allowed\":\"false\",\"stop_facts\":[]}";
    assert(policy_response_parse(nonbool,strlen(nonbool),"773",&media,&https,reasons,sizeof(reasons)) && https);
    assert(!policy_response_parse(good,strlen(good),"774",&media,&https,reasons,sizeof(reasons)) && https);
    const char* bad[]={
        "{\"v\":1,\"sn\":\"773\",\"mqtt_rtp_allowed\":\"false\",\"stop_facts\":[]}",
        "{\"v\":2,\"sn\":\"773\",\"mqtt_rtp_allowed\":false,\"stop_facts\":[]}",
        "{\"v\":1,\"sn\":\"773\",\"mqtt_rtp_allowed\":false,\"stop_facts\":[1]}",
        "{\"v\":1,\"sn\":\"773\",\"mqtt_rtp_allowed\":false,\"stop_facts\":[],\"v\":1}",
        "{\"v\":1,\"sn\":\"773\",\"mqtt_rtp_allowed\":false,\"stop_facts\":[],}",
        "{\"v\":1,\"sn\":\"773\",\"mqtt_rtp_allowed\":false,\"stop_facts\":[]} trailing",
        "{\"v\":01,\"sn\":\"773\",\"mqtt_rtp_allowed\":false,\"stop_facts\":[]}",
        "{\"v\":1,\"sn\":\"773\",\"mqtt_rtp_allowed\":false}"
    };
    for (size_t k=0;k<sizeof(bad)/sizeof(bad[0]);k++)
        assert(!policy_response_parse(bad[k],strlen(bad[k]),"773",&media,&https,reasons,sizeof(reasons)) && https);
    const char* escaped="{\"v\":1,\"sn\":\"77\\u0033\",\"mqtt_rtp_allowed\":true,\"stop_facts\":[]}";
    assert(policy_response_parse(escaped,strlen(escaped),"773",&media,&https,reasons,sizeof(reasons)));
    char embedded[]="{\"v\":1}\0ignored";
    assert(!policy_response_parse(embedded,sizeof(embedded)-1,"773",&media,&https,reasons,sizeof(reasons)));
    PolicyJson j; const char* duplicate="{\"v\":1,\"\\u0076\":1}";
    assert(!policy_json_parse(&j,duplicate,strlen(duplicate)));
    const char* overflow="{\"v\":18446744073709551616}"; unsigned long long number;
    assert(policy_json_parse(&j,overflow,strlen(overflow)) && !uint_field(&j,"v",&number));
}
static void state_tests(void) {
    PolicyRecord r={0}; strcpy_s(r.sn,sizeof(r.sn),"773");
    r.generation=1; r.allowed=true; r.offline_allowed_until=100+POLICY_GRACE_SECONDS;
    assert(policy_record_allowed(&r,100));
    assert(policy_record_allowed(&r,r.offline_allowed_until-1));
    assert(!policy_record_allowed(&r,r.offline_allowed_until));
    r.known=true; r.allowed=false; r.last_success_at=100;
    assert(!policy_record_allowed(&r,100) && !policy_record_allowed(&r,1000000));
    PolicyRecord a=r,b=r; a.allowed=true;
    assert(policy_record_prefer(&a,&b) && !policy_record_prefer(&b,&a));
    b.generation++; assert(policy_record_prefer(&b,&a));
    b=a; b.offline_allowed_until++; assert(policy_record_prefer(&b,&a));
    char stored[512];
    snprintf(stored,sizeof(stored),"{\"storage_version\":1,\"sn\":\"773\",\"generation\":7,\"known\":true,\"allowed\":false,\"last_success_at\":100,\"offline_allowed_until\":%llu,\"facts\":\"terminal_inactive\"}",100+POLICY_GRACE_SECONDS);
    assert(policy_record_parse(stored,strlen(stored),"773",&r) && !r.allowed && r.generation==7);
    assert(!policy_record_parse(stored,strlen(stored),"774",&r));
    const char* corrupt="{\"storage_version\":1,\"sn\":\"773\",\"generation\":1,\"known\":true,\"allowed\":true,\"last_success_at\":100,\"offline_allowed_until\":101}";
    assert(!policy_record_parse(corrupt,strlen(corrupt),"773",&r));
    record=r; record.allowed=true; record.offline_allowed_until=utc_now()+3600;
    anchor_tick=GetTickCount64(); remaining_ms=0; media_allowed=true;
    assert(!policy_media_allowed()); /* monotonic expiry wins over a later wall deadline */
    record.allowed=false; remaining_ms=3600000;
    assert(!policy_media_allowed()); /* known deny never obtains grace */
    https_allowed=false;
    const char* allowed[]={"/api/leo4proxy/policy","/api/leo4proxy/policy?x=1","/api/certificates","/api/certificates/?function=setup","/api/licensebilling/","/licensebilling?x=1"};
    for (size_t k=0;k<sizeof(allowed)/sizeof(allowed[0]);k++) assert(policy_https_path_allowed(allowed[k]));
    const char* denied[]={"/api/leo4proxy/policy/","/api/certificates/child","/api/certificates-evil","/api/payment","/x?route=/api/certificates","/api/certificates//","/API/certificates","/api/%63ertificates"};
    for (size_t k=0;k<sizeof(denied)/sizeof(denied[0]);k++) assert(!policy_https_path_allowed(denied[k]));
    https_allowed=true; assert(policy_https_path_allowed("/api/payment"));
}
static void socket_tests(void) {
    WSADATA wsa; assert(WSAStartup(MAKEWORD(2,2),&wsa)==0);
    SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    struct sockaddr_in addr={0}; addr.sin_family=AF_INET; addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(listener,(struct sockaddr*)&addr,sizeof(addr))==0 && listen(listener,1)==0);
    int len=sizeof(addr); assert(getsockname(listener,(struct sockaddr*)&addr,&len)==0);
    SOCKET client=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); PolicySocket node={0};
    record.allowed=true; record.offline_allowed_until=utc_now()+3600; anchor_tick=GetTickCount64(); remaining_ms=3600000; media_allowed=true;
    assert(policy_media_connect(&node,client,(struct sockaddr*)&addr,sizeof(addr))==0);
    SOCKET peer=accept(listener,NULL,NULL); assert(peer!=INVALID_SOCKET && node.registered);
    record.allowed=false; assert(!policy_media_allowed());
    char byte; assert(recv(peer,&byte,1,0)==0); /* deny shuts an existing external socket */
    policy_socket_unregister(&node); closesocket(client); closesocket(peer);
    client=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); memset(&node,0,sizeof(node));
    assert(policy_media_connect(&node,client,(struct sockaddr*)&addr,sizeof(addr))==SOCKET_ERROR && WSAGetLastError()==WSAEACCES && !node.registered);
    closesocket(client); closesocket(listener); WSACleanup();
}
static void storage_tests(void) {
    /* Redirect all storage to this test's disposable key/directory, never production policy. */
    wchar_t key[128],base[MAX_PATH];
    swprintf_s(key,128,L"SOFTWARE\\Leo4ProxyPolicyTest_%lu",GetCurrentProcessId());
    registry_path=key; storage_roots[0]=HKEY_CURRENT_USER; storage_roots[1]=HKEY_CURRENT_USER;
    assert(GetCurrentDirectoryW(MAX_PATH,base));
    swprintf_s(json_paths[0],MAX_PATH,L"%s\\obj\\policy-test-%lu\\policy.json",base,GetCurrentProcessId());
    json_paths[1][0]=0; json_paths[2][0]=0;
    PolicyRecord original={0},found;
    strcpy_s(original.sn,sizeof(original.sn),"773");
    original.generation=17; original.known=true; original.allowed=false;
    original.last_success_at=utc_now(); original.offline_allowed_until=original.last_success_at+POLICY_GRACE_SECONDS;
    strcpy_s(original.facts,sizeof(original.facts),"terminal_inactive");
    assert(persist(&original));
    restore("773",&found);
    assert(found.generation==17 && !found.allowed && !strcmp(found.facts,"terminal_inactive"));
    assert(RegDeleteKeyExW(HKEY_CURRENT_USER,key,KEY_WOW64_32KEY,0)==ERROR_SUCCESS);
    restore("773",&found); /* surviving JSON preserves deny/deadline */
    assert(found.generation==17 && !found.allowed && found.offline_allowed_until==original.offline_allowed_until);
    assert(persist(&original)); assert(DeleteFileW(json_paths[0]));
    restore("773",&found); /* surviving registry preserves deny/deadline */
    assert(found.generation==17 && !found.allowed);
    assert(RegDeleteKeyExW(HKEY_CURRENT_USER,key,KEY_WOW64_32KEY,0)==ERROR_SUCCESS);
    restore("773",&found); /* neither copy: fresh customer grace */
    assert(!found.known && found.allowed && found.generation==1 &&
           found.offline_allowed_until>=utc_now()+POLICY_GRACE_SECONDS-1);
    assert(persist(&original));
    HANDLE file=CreateFileW(json_paths[0],GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    assert(file!=INVALID_HANDLE_VALUE); DWORD bytes;
    assert(WriteFile(file,"broken",6,&bytes,NULL)); CloseHandle(file);
    restore("773",&found); assert(found.generation==17 && !found.allowed);
    assert(RegDeleteKeyExW(HKEY_CURRENT_USER,key,KEY_WOW64_32KEY,0)==ERROR_SUCCESS);
    restore("773",&found); assert(!found.known && found.allowed);
    assert(DeleteFileW(json_paths[0]));
    /* An inaccessible primary registry root still permits the account fallback. */
    storage_roots[0]=NULL;
    assert(persist(&original));
    assert(RegDeleteKeyExW(HKEY_CURRENT_USER,key,KEY_WOW64_32KEY,0)==ERROR_SUCCESS);
    assert(DeleteFileW(json_paths[0]));
    wchar_t primary[MAX_PATH],blocker[MAX_PATH];
    wcscpy_s(primary,MAX_PATH,json_paths[0]);
    swprintf_s(blocker,MAX_PATH,L"%s\\obj\\policy-blocker-%lu",base,GetCurrentProcessId());
    file=CreateFileW(blocker,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    assert(file!=INVALID_HANDLE_VALUE); CloseHandle(file);
    swprintf_s(json_paths[0],MAX_PATH,L"%s\\policy.json",blocker);
    wcscpy_s(json_paths[1],MAX_PATH,primary);
    assert(persist(&original)); /* first JSON directory is a file: second candidate succeeds */
    restore("773",&found); assert(found.generation==17 && !found.allowed);
    storage_roots[1]=NULL;
    assert(!persist(&original)); /* missing registry is diagnosed, surviving JSON remains usable */
    restore("773",&found); assert(found.generation==17 && !found.allowed);
    assert(RegDeleteKeyExW(HKEY_CURRENT_USER,key,KEY_WOW64_32KEY,0)==ERROR_SUCCESS);
    assert(DeleteFileW(blocker)); assert(DeleteFileW(primary));
    wcscpy_s(json_paths[0],MAX_PATH,primary); json_paths[1][0]=0;
    wchar_t* slash=wcsrchr(json_paths[0],L'\\'); *slash=0; assert(RemoveDirectoryW(json_paths[0]));
    json_paths[0][0]=0;
}
static int unused_udp_port(void) {
    SOCKET s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP); assert(s!=INVALID_SOCKET);
    struct sockaddr_in addr={0}; addr.sin_family=AF_INET; addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(s,(struct sockaddr*)&addr,sizeof(addr))==0);
    int len=sizeof(addr); assert(getsockname(s,(struct sockaddr*)&addr,&len)==0);
    int port=ntohs(addr.sin_port); closesocket(s); return port;
}
static void udp_denial_test(void) {
    WSADATA wsa; assert(WSAStartup(MAKEWORD(2,2),&wsa)==0);
    ProxyConfig config={0}; strcpy_s(config.rtp_tunnel_local_host,sizeof(config.rtp_tunnel_local_host),"127.0.0.1");
    config.rtp_tunnel_enabled=1; config.rtp_tunnel_rtp_port=unused_udp_port();
    do { config.rtp_tunnel_rtcp_port=unused_udp_port(); } while (config.rtp_tunnel_rtcp_port==config.rtp_tunnel_rtp_port);
    /* Deliberately invalid upstream: a denied worker must never try it. */
    strcpy_s(config.rtp_tunnel_remote_host,sizeof(config.rtp_tunnel_remote_host),"invalid.local");
    config.rtp_tunnel_remote_port=1;
    CertDetails cert={0}; strcpy_s(cert.sn,sizeof(cert.sn),"773");
    CredHandle cred; SecInvalidateHandle(&cred); RtpTunnelServer server={0};
    record.allowed=false; assert(!policy_media_allowed());
    assert(rtp_tunnel_start(&server,&config,&cert,cred));
    SOCKET rtp=server.rtpSocket,rtcp=server.rtcpSocket;
    SOCKET sender=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    struct sockaddr_in addr={0}; addr.sin_family=AF_INET; addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    for (int k=0;k<2;k++) {
        addr.sin_port=htons((u_short)(k?config.rtp_tunnel_rtcp_port:config.rtp_tunnel_rtp_port));
        assert(sendto(sender,"test packet",11,0,(struct sockaddr*)&addr,sizeof(addr))==11);
    }
    for (int k=0;k<100 && g_proxyStats.rtp_tunnel_dropped_no_upstream<2;k++) Sleep(10);
    assert(g_proxyStats.rtp_tunnel_dropped_no_upstream==2 && !g_proxyStats.rtp_tunnel_total_connections && !g_proxyStats.rtp_tunnel_active);
    assert(server.isRunning && server.rtpSocket==rtp && server.rtcpSocket==rtcp);
    closesocket(sender); rtp_tunnel_stop(&server); WSACleanup();
}
static void fm_policy_tests(void) {
    record.known=true;record.allowed=true;record.offline_allowed_until=utc_now()+3600;
    anchor_tick=GetTickCount64();remaining_ms=3600000;
    const char* grant="{\"fm_allowed\":true,\"fm_storage_endpoint\":{\"host\":\"storage.example\",\"port\":443}}";
    accept_fm_locked(grant,strlen(grant),true);
    assert(policy_fm_authority_allowed("storage.example:443"));
    assert(!policy_fm_authority_allowed("other.example:443"));
    assert(policy_https_path_allowed("/api/file-manager/v1/agent/hello"));
    record.allowed=false;assert(!policy_fm_authority_allowed("storage.example:443"));
    assert(!policy_https_path_allowed("/api/file-manager/v1/agent/hello"));
    assert(!policy_https_path_allowed("/api/%66ile-manager/v1/agent/hello"));
    assert(!policy_https_path_allowed("/unrelated/../api//file-manager/v1/agent/hello"));
    assert(!policy_https_path_allowed("/%2561pi/file-manager/v1/agent/hello"));
    assert(policy_https_path_allowed("/api/payment"));
    record.allowed=true;fm_authority_deadline=GetTickCount64();assert(!policy_fm_authority_allowed(NULL));
    accept_fm_locked("{}",2,true);assert(!policy_fm_authority_allowed(NULL));
    accept_fm_locked(grant,strlen(grant),false);assert(!policy_fm_authority_allowed(NULL));
    const char* malformed="{\"fm_allowed\":true,\"fm_storage_endpoint\":{\"host\":\"https://storage.example\",\"port\":443}}";
    accept_fm_locked(malformed,strlen(malformed),true);assert(!policy_fm_authority_allowed(NULL));
}
int main(void) {
    CERT_INFO cert_info={0}; CERT_CONTEXT context={0}; context.pCertInfo=&cert_info;
    FILETIME now; GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER ticks; ticks.LowPart=now.dwLowDateTime;ticks.HighPart=now.dwHighDateTime;
    ticks.QuadPart-=600000000ULL;
    cert_info.NotBefore.dwLowDateTime=ticks.LowPart;cert_info.NotBefore.dwHighDateTime=ticks.HighPart;
    ticks.QuadPart+=1200000000ULL;
    cert_info.NotAfter.dwLowDateTime=ticks.LowPart;cert_info.NotAfter.dwHighDateTime=ticks.HighPart;
    certificate=&context;
    response_tests(); state_tests(); socket_tests(); storage_tests(); udp_denial_test();fm_policy_tests();
    record.allowed=true;record.known=true;record.offline_allowed_until=utc_now()+3600;
    anchor_tick=GetTickCount64();remaining_ms=3600000;
    cert_info.NotAfter=cert_info.NotBefore;
    assert(!policy_media_allowed());
    char diagnostics[1024];policy_diagnostics(diagnostics,sizeof(diagnostics));
    assert(strstr(diagnostics,"certificate_expired"));
    certificate=NULL;assert(!policy_media_allowed());
    policy_diagnostics(diagnostics,sizeof(diagnostics));assert(strstr(diagnostics,"certificate_missing"));
    ticks.QuadPart+=1200000000ULL;
    cert_info.NotAfter.dwLowDateTime=ticks.LowPart;cert_info.NotAfter.dwHighDateTime=ticks.HighPart;
    certificate=&context;assert(policy_media_allowed());
    certificate=NULL;
    puts("policy tests passed: response/schema, grace, HTTPS paths, socket cancellation, registry/JSON recovery, UDP drain");
    return 0;
}
