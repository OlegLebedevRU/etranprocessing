#include "fresh_install.h"
#include "install_path.h"
#include "broker_environment.h"
#include "../../l4common/launcher.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/catalog_floor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define FRESH_CONFIGS 15u
static const wchar_t* const tools[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv",L"l4desk",L"l4sql",L"l4pin",L"l4capture",L"ffmpeg"};
struct SetupFreshInstall {
    L4Journal* journal;BYTE header[24];SetupManifest* manifest;L4BootstrapPlan plan;
    SetupAdmissionPolicy admission;L4AccessActors actors;ULONGLONG configs[FRESH_CONFIGS],sequence,receipt;unsigned count;
    volatile LONG spent;bool aborted;SetupInstallPath* path;
};
static bool fail(DWORD code){SetLastError(code);return false;}
static bool system_host(void){
    HANDLE thread=NULL,token=NULL;BYTE user[512];DWORD size=0;
    if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread)){CloseHandle(thread);return fail(ERROR_ACCESS_DENIED);}
    if(GetLastError()!=ERROR_NO_TOKEN)return false;
    bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,user,sizeof(user),&size) && IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid);
    if(token)CloseHandle(token);return ok?true:fail(ERROR_ACCESS_DENIED);
}
static bool token_copy(HANDLE input,HANDLE* output,bool system){
    if(!input)return fail(ERROR_NO_TOKEN);BYTE user[512];DWORD size=0;
    TOKEN_TYPE type;SECURITY_IMPERSONATION_LEVEL level;
    if(!GetTokenInformation(input,TokenType,&type,sizeof(type),&size) || type!=TokenImpersonation ||
        !GetTokenInformation(input,TokenImpersonationLevel,&level,sizeof(level),&size) || level<SecurityImpersonation)return fail(ERROR_BAD_TOKEN_TYPE);
    if(!GetTokenInformation(input,TokenUser,user,sizeof(user),&size) || (system && !IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid)))return fail(ERROR_ACCESS_DENIED);
    return DuplicateHandle(GetCurrentProcess(),input,GetCurrentProcess(),output,0,FALSE,DUPLICATE_SAME_ACCESS)!=0;
}
static bool absent(const wchar_t* path){DWORD a=GetFileAttributesW(path);if(a!=INVALID_FILE_ATTRIBUTES)return fail(ERROR_ALREADY_EXISTS);
    DWORD code=GetLastError();return code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND?true:fail(code);}
static bool services_absent(void){const wchar_t* names[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};
    for(unsigned i=0;i<4;i++){L4ServiceInventory s;if(!l4_service_inventory(names[i],&s))return false;if(s.installed)return fail(ERROR_SERVICE_EXISTS);}return true;}
static bool data_empty(const L4Layout* roots){
    const wchar_t* paths[]={roots->config,roots->state,roots->logs};
    for(unsigned i=0;i<3;i++){
        wchar_t pattern[MAX_PATH];if(swprintf_s(pattern,MAX_PATH,L"%ls\\*",paths[i])<0)return fail(ERROR_FILENAME_EXCED_RANGE);
        WIN32_FIND_DATAW item;HANDLE find=FindFirstFileW(pattern,&item);
        if(find==INVALID_HANDLE_VALUE){DWORD code=GetLastError();if(code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND)continue;return false;}
        bool empty=true;do{if(wcscmp(item.cFileName,L".") && wcscmp(item.cFileName,L"..")){
            /* Independent anti-rollback metadata survives an explicit cold
             * replacement. No actor configuration/state is adopted. */
            if(i==1 && !_wcsicmp(item.cFileName,L"catalog.floor") && !(item.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){
                if(!l4_catalog_floor_verify_existing(roots)){DWORD error=GetLastError();FindClose(find);return fail(error?error:ERROR_INVALID_DATA);}
            }else{empty=false;break;}
        }}while(FindNextFileW(find,&item));
        DWORD code=GetLastError();FindClose(find);if(!empty)return fail(ERROR_ALREADY_EXISTS);if(code!=ERROR_NO_MORE_FILES)return fail(code);
    }
    wchar_t marker[MAX_PATH];return l4_layout_data_path(roots,L"update\\operations\\update.state",marker) && absent(marker);
}
static bool extra_valid(const SetupFreshConfig* c){
    static const wchar_t* const allowed[]={L"mosquitto\\mosquitto.conf",L"leo4proxy\\service-args.txt",L"mosquitto.conf.tmpl"};
    if(!c || !c->relative || !c->bytes || !c->size || c->size>65536)return fail(ERROR_INVALID_PARAMETER);
    for(unsigned i=0;i<_countof(allowed);i++)if(!wcscmp(c->relative,allowed[i]))return true;return fail(ERROR_INVALID_NAME);
}
static bool bound(L4Journal* j,SetupFreshInstall* p){
    return j && p && j==p->journal && j->lock && j->lock!=INVALID_HANDLE_VALUE && !j->poisoned &&
        !memcmp(j->header,p->header,24) && !memcmp(&j->layout,&p->plan.layout,sizeof(j->layout)) && system_host()?true:fail(ERROR_REVISION_MISMATCH);
}
void setup_fresh_free(SetupFreshInstall* p){if(!p)return;HANDLE tokens[]={p->actors.proxy,p->actors.broker,p->actors.console,p->actors.supervisor,p->actors.desktop};
    for(unsigned i=0;i<5;i++)if(tokens[i])CloseHandle(tokens[i]);setup_path_free(p->path);setup_manifest_free(p->manifest);free(p);}
ULONGLONG setup_fresh_bootstrap_sequence(const SetupFreshInstall* p){return p?p->sequence:0;}
ULONGLONG setup_fresh_receipt_sequence(const SetupFreshInstall* p){return p?p->receipt:0;}
static bool save_receipt(L4Journal* j,SetupFreshInstall* p,const SetupRootManifest* root,const void* descriptor,DWORD descriptor_size,const BYTE* signature,DWORD signature_size){
    const BYTE* identity=setup_root_identity(root);if(!identity || descriptor_size>65535 || signature_size>384 || p->count>FRESH_CONFIGS)return fail(ERROR_INVALID_DATA);
    DWORD size=68+p->count*8+descriptor_size+signature_size;BYTE* bytes=calloc(1,size);if(!bytes)return fail(ERROR_NOT_ENOUGH_MEMORY);
    memcpy(bytes,"L4FRSH01",8);memcpy(bytes+8,identity,32);l4_store_u64(bytes+40,p->sequence);l4_store_u64(bytes+48,setup_path_sequence(p->path));
    l4_store_u32(bytes+56,p->count);l4_store_u32(bytes+60,descriptor_size);l4_store_u32(bytes+64,signature_size);
    for(unsigned i=0;i<p->count;i++)l4_store_u64(bytes+68+i*8,p->configs[i]);
    memcpy(bytes+68+p->count*8,descriptor,descriptor_size);memcpy(bytes+68+p->count*8+descriptor_size,signature,signature_size);
    bool ok=l4_journal_append(j,85,bytes,size,&p->receipt);DWORD code=GetLastError();free(bytes);return ok?true:fail(code);
}
static bool template_prepare(L4Journal* j,SetupFreshInstall* p,unsigned index){
    wchar_t source[MAX_PATH],destination[MAX_PATH];if(!setup_manifest_config(p->manifest,index,source,destination))return false;
    unsigned count=0;const L4ReleaseFile* files=setup_manifest_files(p->manifest,&count);const L4ReleaseFile* found=NULL;
    for(unsigned i=0;i<count;i++){wchar_t candidate[MAX_PATH];if(!l4_layout_component(&j->layout,files[i].component,files[i].file,candidate))return false;if(!wcscmp(source,candidate)){found=&files[i];break;}}
    if(!found || found->size>65536)return fail(ERROR_INVALID_DATA);L4ReleaseFence* fence=NULL;wchar_t path[MAX_PATH];
    if(!l4_release_pin(&j->layout,found,&fence,path))return false;
    HANDLE input=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);BYTE* bytes=malloc((size_t)found->size+1);DWORD size=0;
    bool ok=input!=INVALID_HANDLE_VALUE && bytes && ReadFile(input,bytes,(DWORD)found->size,&size,NULL) && size==found->size;
    size_t n=wcslen(j->layout.config);if(ok)ok=!wcsncmp(destination,j->layout.config,n) && destination[n]==L'\\' &&
        l4_config_prepare(j,destination+n+1,bytes,size,&p->configs[p->count]);
    if(ok)++p->count;DWORD code=GetLastError();if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);free(bytes);l4_release_unpin(fence);return ok?true:fail(code?code:ERROR_INVALID_DATA);
}
static bool fresh_prepare(L4Journal* j,const SetupRootManifest* root,const void* descriptor,DWORD descriptor_size,
    const BYTE* signature,DWORD signature_size,const wchar_t* archive,const L4BootstrapProfile profiles[4],const L4AccessActors* actors,
    const SetupFreshConfig* extra,unsigned extra_count,SetupFreshInstall** output,bool public_broker,SetupAdmissionPolicy admission){
    if(!output)return fail(ERROR_INVALID_PARAMETER);*output=NULL;
    if(!j || !archive || !profiles || !actors || !extra_count || extra_count>3 || !extra || !j->lock || j->lock==INVALID_HANDLE_VALUE || j->poisoned || j->sequence)return fail(ERROR_INVALID_PARAMETER);
    SetupFreshInstall* p=calloc(1,sizeof(*p));if(!p)return fail(ERROR_NOT_ENOUGH_MEMORY);p->admission=admission;
    bool ok=setup_root_descriptor_trusted(root,descriptor,descriptor_size,signature,signature_size,&j->layout,&p->manifest) && system_host();
    if(ok)ok=token_copy(actors->proxy,&p->actors.proxy,true) && token_copy(actors->broker,&p->actors.broker,true) && token_copy(actors->console,&p->actors.console,true) &&
        token_copy(actors->supervisor,&p->actors.supervisor,true) && token_copy(actors->desktop,&p->actors.desktop,false) && services_absent() && data_empty(&j->layout);
    for(unsigned i=0;ok && i<3;i++){wchar_t source[MAX_PATH],target[MAX_PATH];ok=setup_manifest_config(p->manifest,i,source,target) && absent(target);}
    bool broker=false;for(unsigned i=0;ok && i<extra_count;i++){wchar_t path[MAX_PATH],leaf[MAX_PATH];ok=extra_valid(&extra[i]);for(unsigned k=0;ok && k<i;k++)if(!wcscmp(extra[i].relative,extra[k].relative))ok=fail(ERROR_DUP_NAME);
        if(ok && !wcscmp(extra[i].relative,L"mosquitto\\mosquitto.conf"))broker=true;
        if(ok)ok=swprintf_s(leaf,MAX_PATH,L"config\\%ls",extra[i].relative)>0 && l4_layout_data_path(&j->layout,leaf,path) && absent(path);}
    if(ok && !broker)ok=fail(ERROR_INVALID_DATA);
    for(unsigned i=0;ok && i<_countof(tools);i++){wchar_t path[MAX_PATH],leaf[MAX_PATH];swprintf_s(leaf,MAX_PATH,L"config\\launchers\\%ls.target",tools[i]);ok=l4_layout_data_path(&j->layout,leaf,path) && absent(path);}
    if(ok)ok=setup_manifest_prepare_policy(p->manifest,archive,p->admission);
    unsigned count=0;const L4ReleaseFile* files=ok?setup_manifest_files(p->manifest,&count):NULL;
    if(ok)ok=files && l4_bootstrap_plan(&j->layout,profiles,4,files,count,&p->plan) && l4_access_prepare(&j->layout,&p->actors) &&
        l4_layout_prepare_leaf(&j->layout,L"config\\launchers",NULL,NULL,false) && l4_layout_prepare_leaf(&j->layout,L"config\\l4capture",NULL,NULL,false);
    for(unsigned i=0;ok && i<3;i++)ok=template_prepare(j,p,i);
    for(unsigned i=0;ok && i<extra_count;i++){ok=public_broker && !wcscmp(extra[i].relative,L"mosquitto\\mosquitto.conf")?
            l4_config_prepare_public_broker(j,extra[i].bytes,extra[i].size,&p->configs[p->count]):l4_config_prepare(j,extra[i].relative,extra[i].bytes,extra[i].size,&p->configs[p->count]);if(ok)++p->count;}
    for(unsigned i=0;ok && i<_countof(tools);i++){ok=l4_launcher_prepare(j,&j->layout,tools[i],files,count,&p->configs[p->count]);if(ok)++p->count;}
    if(ok)ok=setup_manifest_verify_policy(p->manifest,p->admission) && l4_bootstrap_save(j,&p->plan,&p->sequence) && setup_path_prepare(j,&p->path) && save_receipt(j,p,root,descriptor,descriptor_size,signature,signature_size);
    if(!ok){DWORD code=GetLastError();setup_fresh_free(p);return fail(code?code:ERROR_NOT_READY);}
    p->journal=j;memcpy(p->header,j->header,24);*output=p;return true;
}
bool setup_fresh_prepare(L4Journal* j,const SetupRootManifest* root,const void* descriptor,DWORD descriptor_size,
    const BYTE* signature,DWORD signature_size,const wchar_t* archive,const L4BootstrapProfile profiles[4],const L4AccessActors* actors,
    const SetupFreshConfig* extra,unsigned extra_count,SetupFreshInstall** output){
    return fresh_prepare(j,root,descriptor,descriptor_size,signature,signature_size,archive,profiles,actors,extra,extra_count,output,false,SETUP_ADMISSION_STRICT_REMOTE);
}
static bool fresh_apply(L4Journal* j,SetupFreshInstall* p,const L4BootstrapChecks* gates,DWORD commit_timeout,bool local){
    if(!bound(j,p) || p->aborted || InterlockedCompareExchange(&p->spent,1,0))return fail(ERROR_INVALID_PARAMETER);
    if(!local && (!gates || !gates->probe || !gates->barrier || !commit_timeout || commit_timeout>600000 || !gates->barrier_ms || gates->barrier_ms>300000 || gates->service_ms[1]!=300000))return fail(ERROR_INVALID_PARAMETER);
    if(!commit_timeout || commit_timeout>600000)return fail(ERROR_INVALID_PARAMETER);
    for(unsigned i=0;!local && i<4;i++)if(!gates->service_ms[i] || gates->service_ms[i]>300000)return fail(ERROR_INVALID_PARAMETER);
    if(!services_absent() || !setup_manifest_verify_policy(p->manifest,p->admission) || !l4_access_verify(&j->layout,&p->actors))return false;
    for(unsigned i=0;i<p->count;i++)if(!l4_config_verify(j,p->configs[i],false))return false;
    unsigned count=0;const L4ReleaseFile* files=setup_manifest_files(p->manifest,&count);
    for(unsigned i=0;i<_countof(tools);i++)if(!l4_launcher_install(j,&j->layout,tools[i],files,count))return false;
    for(unsigned i=0;i<p->count;i++)if(!l4_config_apply(j,p->configs[i]))return false;
    if(!setup_manifest_verify_policy(p->manifest,p->admission) || !l4_access_verify(&j->layout,&p->actors))return false;
    for(unsigned i=0;i<p->count;i++)if(!l4_config_verify(j,p->configs[i],true))return false;
    if(local)return l4_bootstrap_register(j,p->sequence) && setup_broker_environment_prepare(j,p->sequence) && setup_path_apply(j,p->path) && l4_bootstrap_local_commit(j,p->sequence,commit_timeout);
    return l4_bootstrap_register(j,p->sequence) && setup_broker_environment_prepare(j,p->sequence) && l4_bootstrap_activate(j,p->sequence,gates) && setup_path_apply(j,p->path) && l4_bootstrap_commit(j,p->sequence,gates,commit_timeout);
}
bool setup_fresh_apply(L4Journal* j,SetupFreshInstall* p,const L4BootstrapChecks* gates,DWORD timeout){if(!p || p->admission!=SETUP_ADMISSION_STRICT_REMOTE)return fail(ERROR_ACCESS_DENIED);return fresh_apply(j,p,gates,timeout,false);}
bool setup_fresh_deploy_local(L4Journal* j,SetupFreshInstall* p,DWORD timeout){return fresh_apply(j,p,NULL,timeout,true);}
bool setup_fresh_abort(L4Journal* j,SetupFreshInstall* p,DWORD timeout){
    if(!bound(j,p))return false;
    InterlockedExchange(&p->spent,1);if(!l4_bootstrap_abort(j,p->sequence,timeout) || !setup_path_rollback(j,p->path))return false;
    for(unsigned i=p->count;i>0;i--)if(!l4_config_rollback(j,p->configs[i-1]))return false;
    p->aborted=true;return true;
}
static bool fresh_prepare_broker(L4Journal* j,const SetupRootManifest* root,const void* descriptor,DWORD descriptor_size,
    const BYTE* signature,DWORD signature_size,const wchar_t* archive,const L4BootstrapProfile profiles[4],const L4AccessActors* actors,
    const char* sn,const SetupFreshConfig* extra,unsigned extra_count,SetupFreshInstall** output,SetupAdmissionPolicy admission){
    if(!output)return fail(ERROR_INVALID_PARAMETER);*output=NULL;
    if(!j || extra_count>2 || (extra_count && !extra))return fail(ERROR_INVALID_PARAMETER);
    SetupBrokerConfig broker;if(!(sn && sn[0]?setup_broker_render(&j->layout,sn,&broker):setup_broker_render_standby(&j->layout,&broker)))return false;
    SetupFreshConfig proposals[3]={{L"mosquitto\\mosquitto.conf",broker.bytes,broker.size}};
    for(unsigned i=0;i<extra_count;i++)proposals[i+1]=extra[i];
    return fresh_prepare(j,root,descriptor,descriptor_size,signature,signature_size,archive,profiles,actors,proposals,extra_count+1,output,true,admission);
}
bool setup_fresh_prepare_broker(L4Journal* j,const SetupRootManifest* root,const void* descriptor,DWORD descriptor_size,
    const BYTE* signature,DWORD signature_size,const wchar_t* archive,const L4BootstrapProfile profiles[4],const L4AccessActors* actors,
    const char* sn,const SetupFreshConfig* extra,unsigned extra_count,SetupFreshInstall** output){
    return fresh_prepare_broker(j,root,descriptor,descriptor_size,signature,signature_size,archive,profiles,actors,sn,extra,extra_count,output,SETUP_ADMISSION_STRICT_REMOTE);
}
bool setup_fresh_prepare_broker_local(L4Journal* j,const SetupRootManifest* root,const void* descriptor,DWORD descriptor_size,
    const BYTE* signature,DWORD signature_size,const wchar_t* archive,const L4BootstrapProfile profiles[4],const L4AccessActors* actors,
    const char* sn,const SetupFreshConfig* extra,unsigned extra_count,SetupFreshInstall** output){
    return fresh_prepare_broker(j,root,descriptor,descriptor_size,signature,signature_size,archive,profiles,actors,sn,extra,extra_count,output,SETUP_ADMISSION_LOCAL_OFFLINE);
}
static bool receipt_bytes(L4Journal* j,ULONGLONG receipt,BYTE** bytes,DWORD* size){
    if(!l4_store_find_record(j,85,receipt,bytes,size))return false;bool ok=*size>=68 && !memcmp(*bytes,"L4FRSH01",8);
    if(ok){DWORD n=l4_store_get32(*bytes+56),d=l4_store_get32(*bytes+60),s=l4_store_get32(*bytes+64);ok=n>=13 && n<=FRESH_CONFIGS && d && d<=65535 && s && s<=384 && *size==68+n*8+d+s &&
        l4_store_get64(*bytes+40) && l4_store_get64(*bytes+40)<receipt && l4_store_get64(*bytes+48) && l4_store_get64(*bytes+48)<receipt;}
    if(!ok){free(*bytes);*bytes=NULL;return fail(ERROR_INVALID_DATA);}return true;
}
bool setup_fresh_saved_identity(L4Journal* j,ULONGLONG receipt,L4CatalogRelease* selected){
    if(!selected || !j)return fail(ERROR_INVALID_PARAMETER);memset(selected,0,sizeof(*selected));BYTE* bytes=NULL;DWORD size=0;
    if(!receipt_bytes(j,receipt,&bytes,&size))return false;const wchar_t* v=wcsrchr(j->layout.release,L'\\');
    bool ok=v && WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,v+1,-1,selected->version,sizeof(selected->version),NULL,NULL);if(ok)memcpy(selected->manifest_sha256,bytes+8,32);free(bytes);return ok;
}
static bool fresh_load_abort(L4Journal* j,ULONGLONG receipt,const SetupRootManifest* root,SetupFreshInstall** output,SetupAdmissionPolicy admission){
    if(!output)return fail(ERROR_INVALID_PARAMETER);*output=NULL;if(!j || !j->lock || j->lock==INVALID_HANDLE_VALUE || j->poisoned || !system_host())return fail(ERROR_ACCESS_DENIED);
    BYTE* bytes=NULL;DWORD size=0;if(!receipt_bytes(j,receipt,&bytes,&size))return false;SetupFreshInstall* p=calloc(1,sizeof(*p));if(!p){free(bytes);return fail(ERROR_NOT_ENOUGH_MEMORY);}
    const BYTE* identity=setup_root_identity(root);p->admission=admission;p->count=l4_store_get32(bytes+56);p->sequence=l4_store_get64(bytes+40);p->receipt=receipt;p->spent=1;
    DWORD d=l4_store_get32(bytes+60),s=l4_store_get32(bytes+64);const BYTE* descriptor=bytes+68+p->count*8;
    bool ok=identity && !memcmp(identity,bytes+8,32) && setup_root_descriptor_trusted(root,descriptor,d,descriptor+d,s,&j->layout,&p->manifest) && setup_manifest_verify_policy(p->manifest,p->admission) &&
        l4_bootstrap_load(j,p->sequence,&p->plan) && setup_path_load(j,l4_store_get64(bytes+48),&p->path);
    for(unsigned i=0;ok && i<p->count;i++){p->configs[i]=l4_store_get64(bytes+68+i*8);ok=p->configs[i] && p->configs[i]<p->sequence;
        for(unsigned k=0;ok && k<i;k++)if(p->configs[k]==p->configs[i])ok=false;}
    free(bytes);if(!ok){DWORD code=GetLastError();setup_fresh_free(p);return fail(code?code:ERROR_INVALID_DATA);}p->journal=j;memcpy(p->header,j->header,24);*output=p;return true;
}

bool setup_fresh_load_abort(L4Journal* j,ULONGLONG receipt,const SetupRootManifest* root,SetupFreshInstall** output){return fresh_load_abort(j,receipt,root,output,SETUP_ADMISSION_STRICT_REMOTE);}
bool setup_fresh_load_abort_local(L4Journal* j,ULONGLONG receipt,const SetupRootManifest* root,SetupFreshInstall** output){return fresh_load_abort(j,receipt,root,output,SETUP_ADMISSION_LOCAL_OFFLINE);}
