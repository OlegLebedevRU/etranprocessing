#include "../remote_host.h"
#include "../journal_internal.h"
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;static bool config_denied;static PSECURITY_DESCRIPTOR service_sd;
static QUERY_SERVICE_CONFIGW fixture_config;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL remote host %u: %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static BOOL WINAPI query_config(SC_HANDLE service,LPQUERY_SERVICE_CONFIGW output,DWORD size,LPDWORD needed){CHECK(service==(SC_HANDLE)(ULONG_PTR)123);*needed=sizeof(fixture_config);
    if(!output || size<sizeof(fixture_config)){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}if(config_denied){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}*output=fixture_config;return TRUE;}
static BOOL WINAPI query_security(SC_HANDLE service,SECURITY_INFORMATION fields,PSECURITY_DESCRIPTOR output,DWORD size,LPDWORD needed){CHECK(service==(SC_HANDLE)(ULONG_PTR)123 && fields==DACL_SECURITY_INFORMATION);
    *needed=GetSecurityDescriptorLength(service_sd);if(!output || size<*needed){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}memcpy(output,service_sd,*needed);return TRUE;}
/* Typed updater nomination is modeled only for identity/codec unit checks.
 * Production open_fixed still requires SYSTEM, immutable committed history and
 * owner-signed root; this fixture creates no pointer, file or service. */
struct L4ActiveUpdater { L4ActiveUpdaterInfo info; wchar_t path[MAX_PATH]; };
static L4ActiveUpdater updater_fixture;
bool l4_active_updater_open_fixed(const L4Layout* layout,L4ActiveUpdater** out){if(out)*out=NULL;if(!layout||!out)return false;
 memset(&updater_fixture,0,sizeof(updater_fixture));strcpy_s(updater_fixture.info.version,32,"1.13.5");strcpy_s(updater_fixture.info.arch,8,"x86");
 swprintf_s(updater_fixture.path,MAX_PATH,L"%ls\\setup\\1.13.5\\l4setup.exe",layout->binaries);*out=&updater_fixture;return true;}
bool l4_active_updater_open(L4Journal* owner,L4ActiveUpdater** out){return owner&&l4_active_updater_open_fixed(&owner->layout,out);}
bool l4_active_updater_verify(L4ActiveUpdater* updater){return updater==&updater_fixture;}
void l4_active_updater_close(L4ActiveUpdater* updater){(void)updater;}
const L4ActiveUpdaterInfo* l4_active_updater_info(const L4ActiveUpdater* updater){return updater?&updater->info:NULL;}
const wchar_t* l4_active_updater_path(const L4ActiveUpdater* updater){return updater?updater->path:NULL;}
/* Fixed local-sidecar gate only: ancestry/errors modeled, never host admission. */
static bool purpose_ancestry=true;static unsigned purpose_index,purpose_calls;static DWORD purpose_error,purpose_attributes=FILE_ATTRIBUTE_NORMAL;
static bool purpose_pin(const wchar_t* directory,const wchar_t* root,bool control,L4FileFence* fence){CHECK(directory && root && control && fence);if(!purpose_ancestry){SetLastError(ERROR_ACCESS_DENIED);return false;}return true;}
static void purpose_unpin(L4FileFence* fence){CHECK(fence!=NULL);}
static DWORD WINAPI purpose_file(const wchar_t* path){CHECK(path && wcsstr(path,L"acceptance."));purpose_calls++;if(purpose_calls==purpose_index){if(purpose_error){SetLastError(purpose_error);return INVALID_FILE_ATTRIBUTES;}return purpose_attributes;}SetLastError(ERROR_FILE_NOT_FOUND);return INVALID_FILE_ATTRIBUTES;}
#define l4_store_pin purpose_pin
#define l4_store_unpin purpose_unpin
#define GetFileAttributesW purpose_file
#define QueryServiceConfigW query_config
#define QueryServiceObjectSecurity query_security
#include "../remote_host.c"
#undef l4_store_pin
#undef l4_store_unpin
#undef GetFileAttributesW
#undef QueryServiceConfigW
#undef QueryServiceObjectSecurity
static const wchar_t* operation=L"17730000-0000-4000-8000-000000000031";
static void sd(const wchar_t* descriptor){if(service_sd)LocalFree(service_sd);service_sd=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor,SDDL_REVISION_1,&service_sd,NULL));}
static void request(BYTE bytes[56]){memset(bytes,0,56);memcpy(bytes,"L4RPC031",8);l4_store_u32(bytes+8,1);l4_store_u32(bytes+12,L4_REMOTE_SUITE);memcpy(bytes+16,"latest",7);l4_store_u64(bytes+48,12345);}
static void ack(BYTE bytes[112]){memset(bytes,0,112);memcpy(bytes,"L4RHST01",8);l4_store_u32(bytes+8,1);l4_store_u32(bytes+12,111);l4_store_u64(bytes+16,222);l4_store_u64(bytes+24,333);bytes[32]=1;
    l4_store_u64(bytes+64,12345);memcpy(bytes+72,"1.13.6",7);memcpy(bytes+104,"x86",4);}
static void parse_tests(const L4Layout* layout){BYTE req[56],receipt[112];Snapshot scan={0};scan.layout=layout;request(req);ack(receipt);
    CHECK(snapshot_record(L4_RECORD_REMOTE_REQUEST,1,req,56,&scan));CHECK(!scan.ack);CHECK(snapshot_record(L4_RECORD_REMOTE_HOST_ACK,2,receipt,112,&scan));
    CHECK(scan.ack && scan.receipt.pid==111 && scan.receipt.birth==222 && !strcmp(scan.receipt.request.version,"latest") && !strcmp(scan.receipt.source_version,"1.13.6"));
    CHECK(!snapshot_record(L4_RECORD_REMOTE_HOST_ACK,2,receipt,112,&scan));
    for(unsigned fault=0;fault<16;fault++){
        memset(&scan,0,sizeof(scan));scan.layout=layout;request(req);ack(receipt);CHECK(snapshot_record(L4_RECORD_REMOTE_REQUEST,1,req,56,&scan));
        ULONGLONG sequence=2;DWORD size=112;
        if(fault==0)sequence=3;else if(fault==1)size=111;else if(fault==2)receipt[0]^=1;else if(fault==3)l4_store_u32(receipt+8,2);
        else if(fault==4)l4_store_u32(receipt+12,0);else if(fault==5)l4_store_u64(receipt+16,0);else if(fault==6)l4_store_u64(receipt+24,0);
        else if(fault==7)memset(receipt+32,0,32);else if(fault==8)l4_store_u64(receipt+64,99);else if(fault==9)receipt[72]='2';
        else if(fault==10)receipt[104]='Z';else if(fault==11)receipt[80]=1;else if(fault==12)receipt[110]=1;
        else if(fault==13)memset(receipt+72,'A',32);else if(fault==14)memset(receipt+104,'A',8);else scan.request=false;
        CHECK(!snapshot_record(L4_RECORD_REMOTE_HOST_ACK,sequence,receipt,size,&scan) && !scan.ack);
    }
    BYTE current[144]={0};ack(current);l4_store_u32(current+8,2);memcpy(current+112,"1.13.5",7);
    memset(&scan,0,sizeof(scan));scan.layout=layout;request(req);CHECK(snapshot_record(L4_RECORD_REMOTE_REQUEST,1,req,56,&scan));
    CHECK(snapshot_record(L4_RECORD_REMOTE_HOST_ACK,2,current,144,&scan)&&scan.receipt.format_version==2&&!strcmp(scan.receipt.updater_version,"1.13.5"));
    for(unsigned fault=0;fault<5;fault++){
      memset(&scan,0,sizeof(scan));scan.layout=layout;CHECK(snapshot_record(L4_RECORD_REMOTE_REQUEST,1,req,56,&scan));
      ack(current);l4_store_u32(current+8,2);memset(current+112,0,32);memcpy(current+112,"1.13.5",7);DWORD size=144;
      if(fault==0)size=112;else if(fault==1)memcpy(current+112,"latest",7);else if(fault==2)current[120]=1;else if(fault==3)memset(current+112,'A',32);else l4_store_u32(current+8,1);
      CHECK(!snapshot_record(L4_RECORD_REMOTE_HOST_ACK,2,current,size,&scan));
    }
    for(unsigned fault=0;fault<9;fault++){
        memset(&scan,0,sizeof(scan));scan.layout=layout;request(req);DWORD size=56;ULONGLONG sequence=1;
        if(fault==0)sequence=2;else if(fault==1)size=55;else if(fault==2)req[0]^=1;else if(fault==3)l4_store_u32(req+8,2);
        else if(fault==4)l4_store_u32(req+12,L4_REMOTE_UPDATER);else if(fault==5)memset(req+16,'A',32);else if(fault==6)req[30]=1;
        else if(fault==7)l4_store_u64(req+48,0);else req[16]='/';
        CHECK(!snapshot_record(L4_RECORD_REMOTE_REQUEST,sequence,req,size,&scan));
    }
}
int wmain(void){L4Layout layout;CHECK(l4_layout_from_roots(&layout,L"C:\\PF\\Leo4\\Tools",L"C:\\PD\\Leo4\\Tools",L"1.13.6"));
    wchar_t service[80],image[MAX_PATH],command[1024];CHECK(l4_remote_host_identity(&layout,operation,"x86",service,image,command));
    CHECK(!wcscmp(service,L"L4UpdateHost_17730000-0000-4000-8000-000000000031"));CHECK(!wcscmp(image,L"C:\\PF\\Leo4\\Tools\\setup\\1.13.5\\l4setup.exe"));
    CHECK(wcsstr(command,L"--remote-controller --source-version 1.13.6 --updater-version 1.13.5 --operation ") && wcsstr(command,L"--arch x86"));
    CHECK(!l4_remote_host_identity(&layout,L"00000000-0000-0000-0000-000000000000","x86",service,image,command));
    CHECK(!l4_remote_host_identity(&layout,L"../escape","x86",service,image,command));CHECK(!l4_remote_host_identity(&layout,operation,"arm64",service,image,command));
    CHECK(!l4_remote_host_identity(NULL,operation,"x86",service,image,command));L4Layout drift=layout;drift.state[0]='D';CHECK(!l4_remote_host_identity(&drift,operation,"x86",service,image,command));
    CHECK(!l4_remote_host_identity(&layout,operation,"x64",service,image,command));parse_tests(&layout);
    L4RemoteHost model={0};model.layout=layout;wcscpy_s(model.operation,37,operation);CHECK(l4_remote_host_identity(&layout,operation,"x86",model.service,model.executable,model.command));
    wchar_t display[96];swprintf_s(display,96,L"L4Update %ls",operation);memset(&fixture_config,0,sizeof(fixture_config));fixture_config.lpBinaryPathName=model.command;fixture_config.lpDisplayName=display;fixture_config.lpServiceStartName=L"LocalSystem";
    fixture_config.dwServiceType=SERVICE_WIN32_OWN_PROCESS;fixture_config.dwStartType=SERVICE_DEMAND_START;fixture_config.dwErrorControl=SERVICE_ERROR_NORMAL;fixture_config.lpDependencies=L"";fixture_config.lpLoadOrderGroup=L"";
    sd(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");CHECK(fingerprint((SC_HANDLE)(ULONG_PTR)123,&model));
    fixture_config.lpBinaryPathName=L"\"C:\\other.exe\"";CHECK(!fingerprint((SC_HANDLE)(ULONG_PTR)123,&model));fixture_config.lpBinaryPathName=model.command;
    fixture_config.dwStartType=SERVICE_AUTO_START;CHECK(!fingerprint((SC_HANDLE)(ULONG_PTR)123,&model));fixture_config.dwStartType=SERVICE_DEMAND_START;
    fixture_config.lpServiceStartName=L"LocalService";CHECK(!fingerprint((SC_HANDLE)(ULONG_PTR)123,&model));fixture_config.lpServiceStartName=L"LocalSystem";
    fixture_config.lpDisplayName=L"Foreign host";CHECK(!fingerprint((SC_HANDLE)(ULONG_PTR)123,&model));fixture_config.lpDisplayName=display;
    config_denied=true;CHECK(!fingerprint((SC_HANDLE)(ULONG_PTR)123,&model) && GetLastError()==ERROR_ACCESS_DENIED);config_denied=false;
    sd(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GR;;;BU)");CHECK(!fingerprint((SC_HANDLE)(ULONG_PTR)123,&model));
    sd(L"D:P(A;;GA;;;SY)");CHECK(!fingerprint((SC_HANDLE)(ULONG_PTR)123,&model));
    sd(L"D:(A;;GA;;;SY)(A;;GA;;;BA)");CHECK(!fingerprint((SC_HANDLE)(ULONG_PTR)123,&model));
    LocalFree(service_sd);service_sd=NULL;
    CHECK(!l4_remote_host_self(NULL,operation,"x86"));L4RemoteHostReceipt receipt;L4RemoteHost* host=NULL;L4Journal* journal=NULL;
    CHECK(!l4_remote_host_launch(&journal,"x86",1000,&host,&receipt) && !host);CHECK(!l4_remote_host_ack(NULL,"x86",&receipt));
    CHECK(!l4_remote_host_observe(NULL,operation,1000,&receipt));
    CHECK(!l4_remote_host_launch_local(&journal,"x86",1000,&host,&receipt) && !host);DWORD child=0;CHECK(!l4_remote_host_wait(NULL,1000,&child) && child==STILL_ACTIVE);L4RemoteHost empty={0};CHECK(!l4_remote_host_wait(&empty,1000,&child) && child==STILL_ACTIVE);
    memset(&receipt,0xff,sizeof(receipt));CHECK(!l4_remote_host_load(NULL,&receipt) && GetLastError()==ERROR_INVALID_STATE && !receipt.pid);
    L4Journal unlocked={0};CHECK(!l4_remote_host_load(&unlocked,&receipt) && GetLastError()==ERROR_INVALID_STATE && !receipt.pid);
    L4Journal purpose={0};purpose.layout=layout;purpose.lock=(HANDLE)(ULONG_PTR)1;wcscpy_s(purpose.directory,MAX_PATH,L"C:\\PD\\Leo4\\Tools\\operations\\17730000-0000-4000-8000-000000000031");
    purpose_calls=0;CHECK(no_local_purpose(&purpose)&&purpose_calls==5);
    for(unsigned i=1;i<=5;i++){purpose_index=i;purpose_calls=0;CHECK(!no_local_purpose(&purpose)&&GetLastError()==ERROR_ACCESS_DENIED&&purpose_calls==i);purpose_attributes=FILE_ATTRIBUTE_REPARSE_POINT;purpose_calls=0;CHECK(!no_local_purpose(&purpose));purpose_attributes=FILE_ATTRIBUTE_NORMAL;purpose_error=ERROR_ACCESS_DENIED;purpose_calls=0;CHECK(!no_local_purpose(&purpose)&&GetLastError()==ERROR_ACCESS_DENIED);purpose_error=ERROR_PATH_NOT_FOUND;purpose_calls=0;CHECK(!no_local_purpose(&purpose)&&GetLastError()==ERROR_PATH_NOT_FOUND);purpose_error=0;}
    purpose_index=0;purpose_calls=0;purpose_ancestry=false;CHECK(!no_local_purpose(&purpose)&&!purpose_calls);purpose_ancestry=true;
    /* Actual ordinary development process must refuse before any SCM creation. */
    CHECK(!primary_system() && GetLastError()==ERROR_ACCESS_DENIED);
    printf("Remote host: %u checks, %u failures; ACK codec/private SCM contract modeled, actual ordinary-token refusal; no live host/service changes\n",checks,failures);return failures?1:0;
}
