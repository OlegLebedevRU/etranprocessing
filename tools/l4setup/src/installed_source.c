#include "installed_source.h"
#include "updater_identity.h"
#include "../../l4common/physical_console_token.h"
#include "remote_commit.h"
#include "../../l4common/active_updater.h"
#include <bcrypt.h>
#include "../../l4common/journal_reader_internal.h"
#include "../../l4common/journal_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define HISTORY_LIMIT 256u
static __declspec(thread) const char* diagnostic_stage="idle";
static __declspec(thread) wchar_t diagnostic_operation[40];
const char* setup_installed_source_stage(void){return diagnostic_stage;}
const wchar_t* setup_installed_source_candidate(void){return diagnostic_operation;}
struct SetupInstalledSource {L4Journal* owner;L4JournalReader* reader;SetupInstallBundle* bundle;SetupManifest* manifest;DWORD desktop_session;LUID desktop_auth;L4BootstrapPlan plan;L4AccessActors actors;L4ReleaseFence* images[4];HANDLE processes[4];DWORD pids[4];FILETIME births[4];wchar_t operation[40];char version[64];const char* arch;ULONGLONG receipt,bootstrap;bool local;SetupOperationPlan* remote;SetupRemoteCommit committed;char updater_version[64];wchar_t updater_origin[40];HANDLE installer;L4FileFence installer_fence,inputs_fence;wchar_t installer_path[MAX_PATH];};
static bool fail(DWORD code){SetLastError(code);return false;}
static bool source_system(void){HANDLE thread=NULL,token=NULL;BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD n=0;if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread)){CloseHandle(thread);return fail(ERROR_ACCESS_DENIED);}if(GetLastError()!=ERROR_NO_TOKEN)return false;bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)&&GetTokenInformation(token,TokenUser,user,sizeof(user),&n)&&IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid);if(token)CloseHandle(token);return ok?true:fail(ERROR_ACCESS_DENIED);}
static bool canonical_id(const wchar_t* id){if(wcslen(id)!=36)return false;for(unsigned i=0;i<36;i++){bool dash=i==8||i==13||i==18||i==23;if(dash?id[i]!=L'-':!((id[i]>=L'0'&&id[i]<=L'9')||(id[i]>=L'a'&&id[i]<=L'f')))return false;}return true;}
void setup_installed_source_close(SetupInstalledSource* s){if(!s)return;DWORD saved_error=GetLastError();HANDLE tokens[]={s->actors.proxy,s->actors.broker,s->actors.console,s->actors.supervisor,s->actors.desktop};for(unsigned i=0;i<5;i++)if(tokens[i])CloseHandle(tokens[i]);for(unsigned i=0;i<4;i++){if(s->processes[i])CloseHandle(s->processes[i]);l4_release_unpin(s->images[i]);}if(s->installer && s->installer!=INVALID_HANDLE_VALUE)CloseHandle(s->installer);l4_store_unpin(&s->installer_fence);l4_store_unpin(&s->inputs_fence);if(!s->remote)setup_manifest_free(s->manifest);setup_operation_free(s->remote);setup_bundle_free(s->bundle);l4_journal_reader_close(s->reader);free(s);SetLastError(saved_error);}
static bool profile(const L4BootstrapPlan* plan,unsigned i){SC_HANDLE scm=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!scm)return false;SC_HANDLE svc=OpenServiceW(scm,plan->services[i],SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS);DWORD code=GetLastError();CloseServiceHandle(scm);if(!svc)return fail(code);
 DWORD n=0;QueryServiceConfigW(svc,NULL,0,&n);bool ok=GetLastError()==ERROR_INSUFFICIENT_BUFFER&&n>=sizeof(QUERY_SERVICE_CONFIGW)&&n<=65536;QUERY_SERVICE_CONFIGW* c=ok?malloc(n):NULL;if(ok)ok=c&&QueryServiceConfigW(svc,c,n,&n);
 if(ok)ok=c->dwServiceType==SERVICE_WIN32_OWN_PROCESS&&c->dwStartType==plan->start_types[i]&&c->dwErrorControl==SERVICE_ERROR_NORMAL&&c->lpServiceStartName&&!_wcsicmp(c->lpServiceStartName,L"LocalSystem")&&c->lpBinaryPathName&&!wcscmp(c->lpBinaryPathName,plan->commands[i])&&(!c->lpDependencies||!*c->lpDependencies)&&(!c->lpLoadOrderGroup||!*c->lpLoadOrderGroup);
 SERVICE_STATUS_PROCESS status={0};if(ok)ok=QueryServiceStatusEx(svc,SC_STATUS_PROCESS_INFO,(BYTE*)&status,sizeof(status),&n)&&status.dwCurrentState==SERVICE_RUNNING&&status.dwProcessId;
 code=GetLastError();free(c);CloseServiceHandle(svc);return ok?true:fail(code?code:ERROR_REVISION_MISMATCH);
}
/* Retain actual original primary SYSTEM processes; configured SCM paths alone
 * cannot authenticate a process left running after an ImagePath change. */
static bool process(SetupInstalledSource* s,unsigned i,bool capture){SC_HANDLE manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!manager)return false;SC_HANDLE service=OpenServiceW(manager,s->plan.services[i],SERVICE_QUERY_STATUS);DWORD code=GetLastError();CloseServiceHandle(manager);if(!service)return fail(code);SERVICE_STATUS_PROCESS status={0};DWORD n=0;bool ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&status,sizeof(status),&n)&&status.dwCurrentState==SERVICE_RUNNING&&status.dwProcessId;
 if(ok&&capture){s->processes[i]=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,status.dwProcessId);s->pids[i]=status.dwProcessId;ok=s->processes[i]!=NULL;}if(ok)ok=s->processes[i]&&s->pids[i]==status.dwProcessId&&WaitForSingleObject(s->processes[i],0)==WAIT_TIMEOUT;
 FILETIME born={0},exit,kernel,user;wchar_t image[MAX_PATH],expected[MAX_PATH];DWORD chars=MAX_PATH;
 if(ok)ok=GetProcessTimes(s->processes[i],&born,&exit,&kernel,&user)&&QueryFullProcessImageNameW(s->processes[i],0,image,&chars);if(ok&&capture)s->births[i]=born;if(ok)ok=!memcmp(&born,&s->births[i],sizeof(born));
 const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};wchar_t leaf[48];swprintf_s(leaf,48,L"%ls.exe",components[i]);if(ok)ok=l4_layout_component(&s->plan.layout,components[i],leaf,expected)&&!_wcsicmp(expected,image);
 HANDLE primary=NULL;BYTE identity[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];TOKEN_TYPE type;DWORD session=1;
 if(ok)ok=OpenProcessToken(s->processes[i],TOKEN_QUERY,&primary)&&GetTokenInformation(primary,TokenUser,identity,sizeof(identity),&n)&&IsWellKnownSid(((TOKEN_USER*)identity)->User.Sid,WinLocalSystemSid)&&GetTokenInformation(primary,TokenType,&type,sizeof(type),&n)&&type==TokenPrimary&&GetTokenInformation(primary,TokenSessionId,&session,sizeof(session),&n)&&session==0;
 if(primary)CloseHandle(primary);code=GetLastError();CloseServiceHandle(service);return ok?true:fail(code?code:ERROR_RETRY);
}
typedef struct{ULONGLONG receipt,bootstrap,commit;} Saved;
static bool saved(DWORD kind,ULONGLONG seq,const void* bytes,DWORD size,void* context){(void)bytes;(void)size;Saved* s=context;if(kind==102){if(s->commit)return fail(ERROR_INVALID_DATA);s->commit=seq;}if(kind==85){if(s->receipt)return fail(ERROR_INVALID_DATA);s->receipt=seq;}if(kind==40){if(s->bootstrap)return fail(ERROR_INVALID_DATA);s->bootstrap=seq;}return true;}
static bool receipt(SetupInstalledSource* s,L4CatalogRelease* selected){BYTE* b=NULL;DWORD size=0;if(!l4_journal_reader_find(s->reader,85,s->receipt,&b,&size))return false;
 bool ok=size>=68&&!memcmp(b,"L4FRSH01",8);DWORD count=ok?l4_store_get32(b+56):0,d=ok?l4_store_get32(b+60):0,sig=ok?l4_store_get32(b+64):0;
 if(ok)ok=count>=13&&count<=15&&d&&d<=65535&&sig&&sig<=384&&size==68+count*8+d+sig&&l4_store_get64(b+40)==s->bootstrap&&s->bootstrap<s->receipt&&l4_store_get64(b+48)&&l4_store_get64(b+48)<s->receipt;
 for(unsigned i=0;ok&&i<count;i++){ULONGLONG ref=l4_store_get64(b+68+i*8);ok=ref&&ref<s->bootstrap;for(unsigned k=0;ok&&k<i;k++)ok=ref!=l4_store_get64(b+68+k*8);BYTE* record=NULL;DWORD length=0;if(ok)ok=l4_journal_reader_find(s->reader,20,ref,&record,&length);free(record);}
 if(ok){BYTE* path=NULL;DWORD path_size=0;ok=l4_journal_reader_find(s->reader,80,l4_store_get64(b+48),&path,&path_size)&&path_size>=26&&!memcmp(path,"L4PATH01",8);
        if(ok){DWORD old_type=l4_store_get32(path+8),old_size=l4_store_get32(path+12),new_type=l4_store_get32(path+16),new_size=l4_store_get32(path+20);
            ok=old_size<=65534&&new_size>=2&&new_size<=65534&&!(old_size%2)&&!(new_size%2)&&path_size==24+old_size+new_size&&
                ((!old_size&&!old_type)||((old_type==REG_SZ||old_type==REG_EXPAND_SZ)&&old_size>=2&&wcsnlen_s((wchar_t*)(path+24),old_size/2)==old_size/2-1))&&
                (new_type==REG_SZ||new_type==REG_EXPAND_SZ)&&new_type==(old_type?old_type:REG_EXPAND_SZ)&&wcsnlen_s((wchar_t*)(path+24+old_size),new_size/2)==new_size/2-1;
        }free(path);
    }
    if(ok){memset(selected,0,sizeof(*selected));strcpy_s(selected->version,sizeof(selected->version),s->version);memcpy(selected->manifest_sha256,b+8,32);}free(b);return ok?true:fail(ERROR_INVALID_DATA);
}
static bool admitted_descriptor(SetupInstalledSource* s){BYTE* b=NULL;DWORD size=0;if(!l4_journal_reader_find(s->reader,85,s->receipt,&b,&size))return false;unsigned count=l4_store_get32(b+56);DWORD d=l4_store_get32(b+60),sig=l4_store_get32(b+64),actual_d=0,actual_sig=0;
 const void* descriptor=setup_bundle_descriptor(s->bundle,&actual_d);const BYTE* signature=setup_bundle_signature(s->bundle,&actual_sig);
 bool ok=actual_d==d&&actual_sig==sig&&!memcmp(b+68+count*8,descriptor,d)&&!memcmp(b+68+count*8+d,signature,sig);free(b);return ok?true:fail(ERROR_CRC);
}
static bool request(SetupInstalledSource* s){wchar_t path[MAX_PATH];if(swprintf_s(path,MAX_PATH,L"%ls\\host.request",l4_journal_reader_directory(s->reader))<0)return fail(ERROR_FILENAME_EXCED_RANGE);
 HANDLE file=CreateFileW(path,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);if(file==INVALID_HANDLE_VALUE)return false;BY_HANDLE_FILE_INFORMATION info;LARGE_INTEGER size;BYTE* sd=NULL;DWORD n=0,sd_size=0;BYTE b[120];
 bool ok=GetFileInformationByHandle(file,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&l4_store_security(file,true,&sd,&sd_size)&&GetFileSizeEx(file,&size)&&size.QuadPart==120&&ReadFile(file,b,120,&n,NULL)&&n==120&&!memcmp(b,"L4REQ001",8)&&l4_store_get32(b+8)==2&&l4_store_get32(b+12)&&!memcmp(b+16,setup_root_identity(setup_bundle_root(s->bundle)),32);
 DWORD sid_size=ok?l4_store_get32(b+48):0;if(ok)ok=sid_size>=8&&sid_size<=SECURITY_MAX_SID_SIZE&&IsValidSid(b+52)&&GetLengthSid(b+52)==sid_size;
 DWORD code=GetLastError();free(sd);CloseHandle(file);return ok?true:fail(code?code:ERROR_INVALID_DATA);
}
static const L4ReleaseFile* service_file(SetupInstalledSource* s,unsigned index){const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};const wchar_t* files[]={L"leo4proxy.exe",L"mosquitto.exe",L"l4con.exe",L"l4superv.exe"};unsigned count=0;const L4ReleaseFile* inventory=setup_manifest_files(s->manifest,&count);for(unsigned i=0;i<count;i++)if(!wcscmp(inventory[i].component,components[index])&&!wcscmp(inventory[i].file,files[index]))return &inventory[i];return NULL;}
bool setup_installed_source_verify(SetupInstalledSource* s){if(!s||!source_system()||!s->owner||s->owner->poisoned||!s->owner->lock||s->owner->lock==INVALID_HANDLE_VALUE)return fail(ERROR_ACCESS_DENIED);if(!l4_physical_console_token_matches(s->actors.desktop,s->desktop_session,s->desktop_auth))return false;if(!setup_manifest_verify(s->manifest)||(s->remote&&!setup_operation_verify_images(s->remote))||!setup_bundle_self(s->bundle,s->installer_path))return false;for(unsigned i=0;i<4;i++)if(!profile(&s->plan,i)||!process(s,i,false))return false;return true;}
static bool authenticate_metadata(SetupInstalledSource* s){diagnostic_stage="receipt";L4CatalogRelease selected;wchar_t inputs[MAX_PATH];if(!receipt(s,&selected)||swprintf_s(inputs,MAX_PATH,L"%ls\\inputs",l4_journal_reader_directory(s->reader))<0)return false;
 diagnostic_stage="saved-inputs";if(!l4_store_pin(inputs,l4_journal_reader_directory(s->reader),true,&s->inputs_fence))return false;
 diagnostic_stage="signed-bundle";const char* arches[]={"x86","x64"};bool opened=false;for(unsigned a=0;a<2;a++)if(setup_bundle_open(inputs,s->version,arches[a],&selected,&s->bundle)){s->arch=arches[a];opened=true;break;}if(!opened)return false;
 diagnostic_stage="saved-request";if(!request(s))return false;diagnostic_stage="saved-descriptor";if(!admitted_descriptor(s))return false;diagnostic_stage="signed-manifest";return setup_bundle_manifest(s->bundle,&s->plan.layout,&s->manifest);
}
static bool authenticate_current(SetupInstalledSource* s){diagnostic_stage="inventory";wchar_t parent[MAX_PATH];
 if(!setup_manifest_verify(s->manifest)||swprintf_s(s->installer_path,MAX_PATH,L"%ls\\setup\\%hs\\l4setup.exe",s->plan.layout.binaries,s->updater_version)<0)return false;
 wcscpy_s(parent,MAX_PATH,s->installer_path);*wcsrchr(parent,L'\\')=0;
 diagnostic_stage="setup-host";if(!l4_store_pin(parent,s->plan.layout.binaries,false,&s->installer_fence))return false;
 s->installer=CreateFileW(s->installer_path,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
 if(s->installer==INVALID_HANDLE_VALUE||!setup_bundle_self(s->bundle,s->installer_path))return false;
 HANDLE* tokens[]={&s->actors.proxy,&s->actors.broker,&s->actors.console,&s->actors.supervisor};
 for(unsigned i=0;i<4;i++){diagnostic_stage="service-image-token-process";const L4ReleaseFile* file=service_file(s,i);wchar_t path[MAX_PATH];if(!file||file->size!=s->plan.sizes[i]||memcmp(file->sha256,s->plan.sha256[i],32)||!l4_release_pin(&s->plan.layout,file,&s->images[i],path)||!profile(&s->plan,i)||!l4_access_service_token(s->plan.services[i],tokens[i])||!process(s,i,true))return false;}
 diagnostic_stage="physical-console-token";s->actors.desktop=l4_physical_console_token(&s->desktop_session,&s->desktop_auth);if(!s->actors.desktop)return false;
 return setup_installed_source_verify(s);
}
/* A negative terminal claim does not authorize a source. Authenticate the
 * saved fresh metadata before allowing it to be excluded from discovery. */
static bool fresh_history(SetupInstalledSource* s,const Saved* h,bool* eligible){
 *eligible=false;if(!h||h->commit||!h->receipt||!h->bootstrap)return fail(ERROR_INVALID_DATA);
 s->receipt=h->receipt;s->bootstrap=h->bootstrap;L4Journal* view=l4_journal_reader_codec_view(s->reader);bool committed=false,aborted=false;
 if(!view||!l4_bootstrap_load(view,h->bootstrap,&s->plan))return false;
 const wchar_t* version=wcsrchr(s->plan.layout.release,L'\\');if(!version||!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,version+1,-1,s->version,sizeof(s->version),NULL,NULL)||
  _wcsicmp(s->plan.layout.binaries,s->owner->layout.binaries)||_wcsicmp(s->plan.layout.data,s->owner->layout.data))return fail(ERROR_REVISION_MISMATCH);
 if(!authenticate_metadata(s)||!l4_bootstrap_terminal(view,h->bootstrap,&committed,&aborted)||!l4_bootstrap_local_terminal(view,h->bootstrap,&s->local))return false;
 if((committed&&aborted)||(s->local&&!committed))return fail(ERROR_INVALID_DATA);
 strcpy_s(s->updater_version,64,s->version);wcscpy_s(s->updater_origin,40,s->operation);*eligible=committed&&!aborted;return true;
}
/* Historical recursion remains independent of the current anchor. Only outer
 * source applicability uses its authenticated identity, for both fresh and102. */
static bool updater_matches(const SetupInstalledSource* s,const L4ActiveUpdaterInfo* anchor){
 char origin[37];const SetupRootManifest* root=setup_installed_source_updater_root(s);const BYTE* identity=root?setup_root_identity(root):NULL;
 return anchor&&s->arch&&identity&&WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s->updater_origin,-1,origin,37,NULL,NULL)&&
  !strcmp(origin,anchor->origin)&&!strcmp(s->updater_version,anchor->version)&&!strcmp(s->arch,anchor->arch)&&!memcmp(identity,anchor->root_sha256,32);
}
/* The verified active updater selects the fresh base. Other fresh histories
 * are only structurally audited, never admitted as source authority. This
 * avoids requiring old aborted input packages to remain admissible forever. */
static bool fresh_outer(SetupInstalledSource* s,const Saved* h,const L4ActiveUpdaterInfo* anchor,bool* applicable){
 *applicable=false;if(!h||h->commit||!anchor)return fail(ERROR_INVALID_DATA);
 char origin[37];if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s->operation,-1,origin,37,NULL,NULL))return false;
 if(!strcmp(origin,anchor->origin)){bool eligible=false;if(!fresh_history(s,h,&eligible))return false;if(!eligible)return fail(ERROR_INVALID_DATA);*applicable=true;return true;}
 if(!h->bootstrap)return h->receipt?fail(ERROR_INVALID_DATA):true;
 s->bootstrap=h->bootstrap;s->receipt=h->receipt;L4Journal* view=l4_journal_reader_codec_view(s->reader);bool committed=false,aborted=false,local=false;
 if(!view||!l4_bootstrap_load(view,h->bootstrap,&s->plan)||!l4_bootstrap_terminal(view,h->bootstrap,&committed,&aborted)||!l4_bootstrap_local_terminal(view,h->bootstrap,&local))return false;
 if((committed&&aborted)||(local&&!committed)||((committed||aborted)&&!h->receipt))return fail(ERROR_INVALID_DATA);
 if(h->receipt){L4CatalogRelease ignored;if(!receipt(s,&ignored))return false;}
 return true;
}
static bool historical(SetupInstalledSource* s,wchar_t visited[HISTORY_LIMIT][40],unsigned depth){
 if(depth>=HISTORY_LIMIT)return fail(ERROR_TOO_MANY_NAMES);for(unsigned i=0;i<depth;i++)if(!wcscmp(visited[i],s->operation))return fail(ERROR_CIRCULAR_DEPENDENCY);wcscpy_s(visited[depth],40,s->operation);
 Saved h={0};if(!l4_journal_reader_replay(s->reader,saved,&h))return false;
 if(!h.commit){
  bool eligible=false;if(!fresh_history(s,&h,&eligible))return false;return eligible?true:fail(ERROR_INVALID_DATA);
 }
 if(h.receipt||h.bootstrap)return fail(ERROR_INVALID_DATA);
 if(!setup_remote_commit_admit_target(s->reader,&s->owner->layout,&s->committed,&s->remote))return false;
 const L4RoutePlan* route=setup_operation_route(s->remote);unsigned hops=setup_operation_hops(s->remote);s->arch=l4_route_arch(route);strcpy_s(s->version,64,s->committed.suite_version);
 s->manifest=(SetupManifest*)setup_operation_target(s->remote,hops-1);s->plan.layout=*setup_manifest_layout(s->manifest);
 for(unsigned i=0;i<4;i++){const L4ServiceSwitch* v=setup_operation_switch(s->remote,hops-1,i);wcscpy_s(s->plan.services[i],32,v->service);wcscpy_s(s->plan.commands[i],2048,v->after);s->plan.start_types[i]=v->before.start_type;s->plan.sizes[i]=v->size;memcpy(s->plan.sha256[i],v->sha256,32);}
 SetupInstalledSource* previous=calloc(1,sizeof(*previous));if(!previous)return fail(ERROR_OUTOFMEMORY);previous->owner=s->owner;
 bool ok=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s->committed.predecessor,-1,previous->operation,40)!=0&&l4_journal_reader_open(s->owner,previous->operation,&previous->reader)&&historical(previous,visited,depth+1);
 L4CatalogRelease source={0};const SetupRootManifest* updater=ok?setup_bundle_root(previous->bundle):NULL;const SetupRootAsset* asset=updater?setup_root_installer(updater):NULL;const BYTE* root=updater?setup_root_identity(updater):NULL,*publisher=updater?setup_root_publisher(updater):NULL;
 const SetupRootManifest* predecessor=ok?setup_installed_source_root(previous):NULL;const BYTE* previous_root=predecessor?setup_root_identity(predecessor):NULL;char origin[37];
 if(ok)ok=l4_route_source(route,&source)&&previous_root&&!strcmp(source.version,previous->version)&&!memcmp(source.manifest_sha256,previous_root,32)&&!strcmp(previous->arch,s->arch)&&asset&&root&&publisher&&
  !strcmp(s->committed.updater_version,previous->updater_version)&&!memcmp(s->committed.updater_root,root,32)&&!memcmp(s->committed.publisher,publisher,32)&&!memcmp(&s->committed.updater,asset,sizeof(*asset))&&
  WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,previous->updater_origin,-1,origin,37,NULL,NULL)&&!strcmp(origin,s->committed.updater_origin);
 if(ok){s->bundle=previous->bundle;previous->bundle=NULL;strcpy_s(s->updater_version,64,previous->updater_version);wcscpy_s(s->updater_origin,40,previous->updater_origin);}
 DWORD error=GetLastError();setup_installed_source_close(previous);return ok?true:fail(error?error:ERROR_REVISION_MISMATCH);
}
bool setup_installed_source_open(L4Journal* owner,SetupInstalledSource** result){diagnostic_stage="owner-system";diagnostic_operation[0]=0;if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;if(!owner||owner->poisoned||!owner->lock||owner->lock==INVALID_HANDLE_VALUE||!source_system())return fail(ERROR_ACCESS_DENIED);
 diagnostic_stage="owner-layout";L4Layout expected;const wchar_t* owner_version=wcsrchr(owner->layout.release,L'\\');
 if(!owner_version||!l4_layout_resolve(&expected,owner_version+1)||memcmp(&owner->layout,&expected,sizeof(expected)))return fail(ERROR_ACCESS_DENIED);
 diagnostic_stage="history-root";L4FileFence root={0};if(!l4_store_pin(owner->layout.operations,owner->layout.data,false,&root))return false;wchar_t pattern[MAX_PATH];if(swprintf_s(pattern,MAX_PATH,L"%ls\\*",owner->layout.operations)<0){l4_store_unpin(&root);return fail(ERROR_FILENAME_EXCED_RANGE);}
 WIN32_FIND_DATAW entry;HANDLE find=FindFirstFileW(pattern,&entry);if(find==INVALID_HANDLE_VALUE){DWORD code=GetLastError();l4_store_unpin(&root);return fail(code);}
 L4ActiveUpdater* updater=NULL;if(!l4_active_updater_open(owner,&updater)||!l4_active_updater_verify(updater)){DWORD e=GetLastError();l4_active_updater_close(updater);FindClose(find);l4_store_unpin(&root);return fail(e?e:ERROR_ACCESS_DENIED);}
 const L4ActiveUpdaterInfo* anchor=l4_active_updater_info(updater);if(!anchor){l4_active_updater_close(updater);FindClose(find);l4_store_unpin(&root);return fail(ERROR_INVALID_DATA);}SetupInstalledSource* accepted=NULL;bool ok=true;unsigned count=0;const wchar_t* current=wcsrchr(owner->directory,L'\\');
 do {if(!wcscmp(entry.cFileName,L".")||!wcscmp(entry.cFileName,L".."))continue;if(++count>HISTORY_LIMIT){ok=false;SetLastError(ERROR_TOO_MANY_NAMES);break;}if(!(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))continue;
 if(entry.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){ok=false;SetLastError(ERROR_ACCESS_DENIED);break;}if(!canonical_id(entry.cFileName)){ok=false;SetLastError(ERROR_INVALID_NAME);break;}if(current&&!wcscmp(current+1,entry.cFileName))continue;
 SetupInstalledSource* s=calloc(1,sizeof(*s));if(!s){ok=false;SetLastError(ERROR_NOT_ENOUGH_MEMORY);break;}s->owner=owner;wcscpy_s(s->operation,40,entry.cFileName);wcscpy_s(diagnostic_operation,40,entry.cFileName);
 diagnostic_stage="history-snapshot";if(!l4_journal_reader_open(owner,s->operation,&s->reader)){setup_installed_source_close(s);ok=false;break;}diagnostic_stage="history-records";Saved history={0};if(!l4_journal_reader_replay(s->reader,saved,&history)){setup_installed_source_close(s);ok=false;break;}
 if(!history.commit&&!history.receipt&&!history.bootstrap){setup_installed_source_close(s);continue;}
 wchar_t visited[HISTORY_LIMIT][40];diagnostic_stage="historical-authority";if(!history.commit){bool applicable=false;if(!fresh_outer(s,&history,anchor,&applicable)){setup_installed_source_close(s);ok=false;break;}if(!applicable){setup_installed_source_close(s);continue;}}else if(!historical(s,visited,0)){setup_installed_source_close(s);ok=false;break;}
 diagnostic_stage="updater-origin-applicability";if(!updater_matches(s,anchor)){setup_installed_source_close(s);continue;}
 diagnostic_stage="scm-applicability";bool applicable=true;for(unsigned i=0;i<4;i++)if(!profile(&s->plan,i)){applicable=false;break;}if(!applicable){setup_installed_source_close(s);continue;}
 if(!authenticate_current(s)){setup_installed_source_close(s);ok=false;break;}if(accepted){setup_installed_source_close(s);ok=false;SetLastError(ERROR_DUP_NAME);break;}accepted=s;
 }while(FindNextFileW(find,&entry));DWORD code=GetLastError();if(ok&&code!=ERROR_NO_MORE_FILES){ok=false;}FindClose(find);l4_store_unpin(&root);if(ok&&!l4_active_updater_verify(updater)){ok=false;code=GetLastError();}l4_active_updater_close(updater);
 if(!ok){setup_installed_source_close(accepted);return fail(code?code:ERROR_INVALID_DATA);}if(!accepted)return fail(ERROR_NOT_FOUND);*result=accepted;wcscpy_s(diagnostic_operation,40,accepted->operation);diagnostic_stage="accepted";return true;
}
const L4BootstrapPlan* setup_installed_source_plan(const SetupInstalledSource* s){return s?&s->plan:NULL;}
const SetupManifest* setup_installed_source_manifest(const SetupInstalledSource* s){return s?s->manifest:NULL;}
const SetupRootManifest* setup_installed_source_root(const SetupInstalledSource* s){return s?(s->remote?setup_operation_target_root(s->remote,setup_operation_hops(s->remote)-1):setup_bundle_root(s->bundle)):NULL;}
const SetupRootManifest* setup_installed_source_updater_root(const SetupInstalledSource* s){return s?setup_bundle_root(s->bundle):NULL;}
const wchar_t* setup_installed_source_operation(const SetupInstalledSource* s){return s?s->operation:NULL;}
const char* setup_installed_source_version(const SetupInstalledSource* s){return s?s->version:NULL;}
const char* setup_installed_source_suite_version(const SetupInstalledSource* s){return s?s->version:NULL;}
const char* setup_installed_source_updater_version(const SetupInstalledSource* s){return s?s->updater_version:NULL;}
const char* setup_installed_source_arch(const SetupInstalledSource* s){return s?s->arch:NULL;}
const L4AccessActors* setup_installed_source_actors(const SetupInstalledSource* s){return s?&s->actors:NULL;}

static bool config_baseline_mode(const BYTE* b,DWORD size,unsigned index,const BYTE** candidate,DWORD* length,bool remote){
 const char* names[]={"l4superv.json","mosquitto\\mosquitto.conf","mosquitto\\acl.conf","launchers\\leo4proxy.target","launchers\\mosquitto.target","launchers\\l4con.target","launchers\\l4superv.target","launchers\\l4desk.target","launchers\\l4sql.target","launchers\\l4pin.target","launchers\\l4capture.target","launchers\\ffmpeg.target"};
 if(index>=12 || !b || size<24)return fail(ERROR_INVALID_DATA);
 DWORD version=l4_store_get32(b),existed=l4_store_get32(b+4),path=l4_store_get32(b+8),old=l4_store_get32(b+12),next=l4_store_get32(b+16),sd=l4_store_get32(b+20);
 if((version!=1 && version!=2) || existed>1 || old>65536 || (!existed&&old) || (!remote&&(existed||old)) || !path || path>255 || !next || next>65536 || sd<sizeof(SECURITY_DESCRIPTOR_RELATIVE) || sd>4096 ||
    (ULONGLONG)size!=24ull+path+old+next+sd)return fail(ERROR_INVALID_DATA);
 if(path!=strlen(names[index]) || memcmp(b+24,names[index],path))return fail(ERROR_NOT_FOUND);
 if(version==2 && index!=1)return fail(ERROR_INVALID_DATA);
 /* SD is deliberately opaque: its authority is not exported or reused. */
 *candidate=b+24+path+old;*length=next;return true;
}
static bool config_baseline(const BYTE* b,DWORD size,unsigned index,const BYTE** candidate,DWORD* length){return config_baseline_mode(b,size,index,candidate,length,false);}
bool setup_installed_source_config(SetupInstalledSource* s,unsigned index,BYTE** bytes,DWORD* size){
 if(!bytes || !size || index>=12)return fail(ERROR_INVALID_PARAMETER);*bytes=NULL;*size=0;
 if(!setup_installed_source_verify(s))return false;
 BYTE* receipt_bytes=NULL;DWORD receipt_size=0;if(!s->remote&&!l4_journal_reader_find(s->reader,85,s->receipt,&receipt_bytes,&receipt_size))return false;
 bool ok=s->remote||receipt_size>=68;DWORD count=s->remote?12:(ok?l4_store_get32(receipt_bytes+56):0);ok=ok&&(s->remote||(count>=13&&count<=15&&receipt_size>=68+count*8));
 for(unsigned i=0;ok&&i<count;i++){BYTE* record=NULL;DWORD length=0;
  ok=l4_journal_reader_find(s->reader,20,s->remote?s->committed.configs[i]:l4_store_get64(receipt_bytes+68+i*8),&record,&length);
  if(ok){const BYTE* candidate=NULL;DWORD n=0;if(s->remote?config_baseline_mode(record,length,index,&candidate,&n,true):config_baseline(record,length,index,&candidate,&n)){
    if(*bytes){ok=false;SetLastError(ERROR_DUP_NAME);}else{*bytes=malloc(n);ok=*bytes!=NULL;if(ok){memcpy(*bytes,candidate,n);*size=n;}else SetLastError(ERROR_NOT_ENOUGH_MEMORY);}
   }else if(GetLastError()!=ERROR_NOT_FOUND){ok=false;}
  }DWORD code=GetLastError();free(record);SetLastError(code);
 }
 DWORD code=GetLastError();free(receipt_bytes);if(!ok || !*bytes){free(*bytes);*bytes=NULL;*size=0;return fail(ok?ERROR_NOT_FOUND:code);}return true;
}
bool setup_installed_source_owned_by(const SetupInstalledSource* s,const L4Journal* j){return s&&j&&s->owner==j&&!j->poisoned&&j->lock&&j->lock!=INVALID_HANDLE_VALUE;}

struct SetupUpdaterIdentity {char version[64],arch[8];BYTE root[32],publisher[32];SetupRootAsset asset;wchar_t path[MAX_PATH],origin[40];HANDLE file;L4FileFence parents;};
void setup_updater_identity_free(SetupUpdaterIdentity* p){if(!p)return;DWORD error=GetLastError();if(p->file&&p->file!=INVALID_HANDLE_VALUE)CloseHandle(p->file);l4_store_unpin(&p->parents);free(p);SetLastError(error);}
bool setup_updater_identity_verify(SetupUpdaterIdentity* p){
 if(!p||!source_system()||!p->file||p->file==INVALID_HANDLE_VALUE)return fail(ERROR_ACCESS_DENIED);
 BY_HANDLE_FILE_INFORMATION info;LARGE_INTEGER length,zero={0};BYTE* sd=NULL;DWORD sd_size=0;
 bool ok=GetFileInformationByHandle(p->file,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&
  l4_store_security(p->file,false,&sd,&sd_size)&&GetFileSizeEx(p->file,&length)&&length.QuadPart>0&&(ULONGLONG)length.QuadPart==p->asset.size&&SetFilePointerEx(p->file,zero,NULL,FILE_BEGIN);free(sd);
 BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;BYTE block[32768],actual[32];DWORD got=0;ULONGLONG total=0;
 if(ok)ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)==0&&BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)==0;
 while(ok){ok=ReadFile(p->file,block,sizeof(block),&got,NULL)!=0;if(!ok||!got)break;total+=got;ok=total<=p->asset.size&&BCryptHashData(hash,block,got,0)==0;}
 if(ok)ok=total==p->asset.size&&BCryptFinishHash(hash,actual,32,0)==0&&!memcmp(actual,p->asset.sha256,32);
 if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);
 if(!ok)return fail(ERROR_CRC);return setup_signed_executable(p->file,p->path,p->publisher);
}
bool setup_updater_identity_from_source(SetupInstalledSource* s,SetupUpdaterIdentity** out){
 if(!out)return fail(ERROR_INVALID_PARAMETER);*out=NULL;if(!setup_installed_source_verify(s))return false;
 const SetupRootManifest* root=setup_bundle_root(s->bundle);const SetupRootAsset* asset=setup_root_installer(root);const BYTE* identity=setup_root_identity(root),*publisher=setup_root_publisher(root);
 if(!asset||!identity||!publisher)return fail(ERROR_INVALID_DATA);SetupUpdaterIdentity* p=calloc(1,sizeof(*p));if(!p)return fail(ERROR_OUTOFMEMORY);
 strcpy_s(p->version,sizeof(p->version),s->updater_version);strcpy_s(p->arch,sizeof(p->arch),s->arch);memcpy(p->root,identity,32);memcpy(p->publisher,publisher,32);p->asset=*asset;
 wcscpy_s(p->path,MAX_PATH,s->installer_path);wcscpy_s(p->origin,40,s->updater_origin);wchar_t parent[MAX_PATH];wcscpy_s(parent,MAX_PATH,p->path);wchar_t* slash=wcsrchr(parent,L'\\');
 bool ok=slash!=NULL;if(ok){*slash=0;ok=l4_store_pin(parent,s->plan.layout.binaries,false,&p->parents);}
 if(ok){p->file=CreateFileW(p->path,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);ok=p->file!=INVALID_HANDLE_VALUE&&setup_updater_identity_verify(p)&&setup_installed_source_verify(s);}
 DWORD error=GetLastError();if(!ok){setup_updater_identity_free(p);return fail(error?error:ERROR_INVALID_DATA);}*out=p;return true;
}
const char* setup_updater_identity_version(const SetupUpdaterIdentity* p){return p?p->version:NULL;}
const char* setup_updater_identity_arch(const SetupUpdaterIdentity* p){return p?p->arch:NULL;}
const BYTE* setup_updater_identity_root(const SetupUpdaterIdentity* p){return p?p->root:NULL;}
const BYTE* setup_updater_identity_publisher(const SetupUpdaterIdentity* p){return p?p->publisher:NULL;}
const SetupRootAsset* setup_updater_identity_asset(const SetupUpdaterIdentity* p){return p?&p->asset:NULL;}
const wchar_t* setup_updater_identity_path(const SetupUpdaterIdentity* p){return p?p->path:NULL;}
const wchar_t* setup_updater_identity_origin(const SetupUpdaterIdentity* p){return p?p->origin:NULL;}
