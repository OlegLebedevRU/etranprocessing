#include "remote_commit.h"
#include "../../l4common/journal_internal.h"
#include <bcrypt.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <objbase.h>
#include "../../l4common/recovery_plan.h"
#include "../../l4common/communication_plan.h"
static bool fail(DWORD e){SetLastError(e);return false;}
static bool uuid(const char* s){if(strnlen_s(s,37)!=36)return false;bool nonzero=false;for(unsigned i=0;i<36;i++){if(i==8||i==13||i==18||i==23){if(s[i]!='-')return false;}else{if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;if(s[i]!='0')nonzero=true;}}return nonzero;}
static bool version(const char* s){size_t n=strnlen_s(s,32);if(!n||n==32)return false;unsigned dots=0,digits=0;for(size_t i=0;i<n;i++){if(s[i]=='.'){if(!digits||++dots>2)return false;digits=0;}else if(s[i]>='0'&&s[i]<='9')digits++;else return false;}return dots==2&&digits;}
static bool digest(const BYTE* s){BYTE v=0;for(unsigned i=0;i<32;i++)v|=s[i];return v!=0;}
static bool valid(const SetupRemoteCommit* p){
 if(!p||!uuid(p->operation)||!uuid(p->predecessor)||!uuid(p->updater_origin)||!strcmp(p->operation,p->predecessor)||!version(p->suite_version)||!version(p->updater_version)||
  strnlen_s(p->arch,8)>=8||(strcmp(p->arch,"x86")&&strcmp(p->arch,"x64"))||!digest(p->suite_root)||!digest(p->updater_root)||!digest(p->publisher)||!digest(p->operation_sha256)||
  strnlen_s(p->updater.name,40)>=40||strcmp(p->updater.name,"l4setup.exe")||!p->updater.size||!digest(p->updater.sha256)||!p->operation_sequence||!p->finished_utc)return false;
 for(unsigned i=0;i<12;i++){if(!p->configs[i]||p->configs[i]>=p->operation_sequence)return false;for(unsigned j=0;j<i;j++)if(p->configs[j]==p->configs[i])return false;}
 for(unsigned i=0;i<4;i++)if((p->start_types[i]!=SERVICE_AUTO_START&&p->start_types[i]!=SERVICE_DEMAND_START)||!p->pids[i]||(!p->births[i].dwLowDateTime&&!p->births[i].dwHighDateTime)||!wcsnlen_s(p->commands[i],2048)||wcsnlen_s(p->commands[i],2048)==2048)return false;
 return true;
}
static void put(BYTE** b,const void* p,size_t n){memcpy(*b,p,n);*b+=n;}
static void get(const BYTE** b,void* p,size_t n){memcpy(p,*b,n);*b+=n;}
bool setup_remote_commit_encode(const SetupRemoteCommit* p,BYTE out[SETUP_REMOTE_COMMIT_BYTES]){
 if(!out||!valid(p))return fail(ERROR_INVALID_DATA);memset(out,0,SETUP_REMOTE_COMMIT_BYTES);BYTE* b=out;put(&b,"L4SCMT01",8);
 l4_store_u64(b,p->operation_sequence);b+=8;put(&b,p->operation_sha256,32);
 const char* strings[]={p->operation,p->predecessor,p->updater_origin,p->suite_version,p->updater_version,p->arch};const size_t sizes[]={37,37,37,32,32,8};
 for(unsigned i=0;i<6;i++){memcpy(b,strings[i],strlen(strings[i]));b+=sizes[i];}
 put(&b,p->suite_root,32);put(&b,p->updater_root,32);memcpy(b,p->updater.name,strlen(p->updater.name));b+=40;l4_store_u64(b,p->updater.size);b+=8;put(&b,p->updater.sha256,32);put(&b,p->publisher,32);l4_store_u64(b,p->finished_utc);b+=8;
 for(unsigned i=0;i<12;i++){l4_store_u64(b,p->configs[i]);b+=8;}
 for(unsigned i=0;i<4;i++){l4_store_u32(b,p->start_types[i]);b+=4;l4_store_u32(b,p->pids[i]);b+=4;l4_store_u32(b,p->births[i].dwLowDateTime);l4_store_u32(b+4,p->births[i].dwHighDateTime);b+=8;memcpy(b,p->commands[i],wcslen(p->commands[i])*2);b+=2048*2;}
 return (size_t)(b-out)==SETUP_REMOTE_COMMIT_BYTES?true:fail(ERROR_INVALID_DATA);
}
bool setup_remote_commit_decode(const void* bytes,DWORD size,SetupRemoteCommit* p){
 if(!p)return fail(ERROR_INVALID_PARAMETER);memset(p,0,sizeof(*p));if(!bytes||size!=SETUP_REMOTE_COMMIT_BYTES||memcmp(bytes,"L4SCMT01",8))return fail(ERROR_INVALID_DATA);
 const BYTE* b=(const BYTE*)bytes+8;SetupRemoteCommit v={0};v.operation_sequence=l4_store_get64(b);b+=8;get(&b,v.operation_sha256,32);
 get(&b,v.operation,37);get(&b,v.predecessor,37);get(&b,v.updater_origin,37);get(&b,v.suite_version,32);get(&b,v.updater_version,32);get(&b,v.arch,8);
 get(&b,v.suite_root,32);get(&b,v.updater_root,32);get(&b,v.updater.name,40);v.updater.size=l4_store_get64(b);b+=8;get(&b,v.updater.sha256,32);get(&b,v.publisher,32);v.finished_utc=l4_store_get64(b);b+=8;
 for(unsigned i=0;i<12;i++){v.configs[i]=l4_store_get64(b);b+=8;}for(unsigned i=0;i<4;i++){v.start_types[i]=l4_store_get32(b);b+=4;v.pids[i]=l4_store_get32(b);b+=4;v.births[i].dwLowDateTime=l4_store_get32(b);v.births[i].dwHighDateTime=l4_store_get32(b+4);b+=8;get(&b,v.commands[i],2048*2);}
 BYTE canonical[SETUP_REMOTE_COMMIT_BYTES];if(!setup_remote_commit_encode(&v,canonical)||memcmp(bytes,canonical,size))return fail(ERROR_INVALID_DATA);*p=v;return true;
}
typedef struct {ULONGLONG commit;bool duplicate;} Found;
static bool find(DWORD kind,ULONGLONG seq,const void* bytes,DWORD size,void* ctx){(void)bytes;(void)size;Found* f=ctx;if(kind==SETUP_RECORD_REMOTE_COMMIT){if(f->commit)f->duplicate=true;f->commit=seq;}return true;}
bool setup_remote_commit_snapshot(const L4JournalReader* r,SetupRemoteCommit* out){
 if(!r||!out)return fail(ERROR_INVALID_PARAMETER);memset(out,0,sizeof(*out));Found f={0};BYTE* b=NULL;DWORD n=0;SetupRemoteCommit p;
 if(!l4_journal_reader_replay(r,find,&f)||!f.commit||f.duplicate)return fail(ERROR_INVALID_DATA);
 bool ok=l4_journal_reader_find(r,102,f.commit,&b,&n)&&setup_remote_commit_decode(b,n,&p);free(b);b=NULL;if(!ok)return false;
 const wchar_t* directory=l4_journal_reader_directory(r);const wchar_t* leaf=directory?wcsrchr(directory,L'\\'):NULL;char id[37];if(!leaf||!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,leaf+1,-1,id,37,NULL,NULL)||strcmp(id,p.operation)||p.operation_sequence>=f.commit)return fail(ERROR_REVISION_MISMATCH);
 if(!l4_journal_reader_find(r,64,p.operation_sequence,&b,&n))return false;BYTE hash[32];BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE state=NULL;
 ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)==0&&BCryptCreateHash(alg,&state,NULL,0,NULL,0,0)==0&&BCryptHashData(state,b,n,0)==0&&BCryptFinishHash(state,hash,32,0)==0&&!memcmp(hash,p.operation_sha256,32);free(b);if(state)BCryptDestroyHash(state);if(alg)BCryptCloseAlgorithmProvider(alg,0);if(!ok)return fail(ERROR_CRC);
 for(unsigned i=0;i<12;i++){b=NULL;ok=l4_journal_reader_find(r,20,p.configs[i],&b,&n);free(b);if(!ok)return false;}*out=p;return true;
}
bool setup_remote_commit_save(L4Journal* j,const SetupRemoteCommit* p){(void)j;(void)p;return fail(ERROR_CALL_NOT_IMPLEMENTED);}
typedef struct{ULONGLONG recovery,communication,commit;bool duplicate;} Settled;
static bool settled_visit(DWORD kind,ULONGLONG seq,const void* bytes,DWORD size,void* ctx){(void)bytes;(void)size;Settled* s=ctx;ULONGLONG* ref=kind==66?&s->recovery:kind==70?&s->communication:kind==102?&s->commit:NULL;if(ref){if(*ref)s->duplicate=true;*ref=seq;}return true;}
static bool private_read(const L4JournalReader* reader,const wchar_t* leaf,DWORD maximum,BYTE** bytes,DWORD* size){
 *bytes=NULL;*size=0;const wchar_t* directory=l4_journal_reader_directory(reader);wchar_t path[MAX_PATH];if(!directory||swprintf_s(path,MAX_PATH,L"%ls\\%ls",directory,leaf)<0)return fail(ERROR_BAD_PATHNAME);
 HANDLE h=CreateFileW(path,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);if(h==INVALID_HANDLE_VALUE)return false;
 BY_HANDLE_FILE_INFORMATION info;LARGE_INTEGER n={0};FILE_STREAM_INFO streams[128];BYTE* sd=NULL;DWORD sd_size=0;
 bool ok=GetFileInformationByHandle(h,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&GetFileSizeEx(h,&n)&&n.QuadPart>0&&n.QuadPart<=maximum&&l4_store_security(h,true,&sd,&sd_size)&&
  GetFileInformationByHandleEx(h,FileStreamInfo,streams,sizeof(streams))&&streams[0].NextEntryOffset==0&&streams[0].StreamNameLength==14&&!memcmp(streams[0].StreamName,L"::$DATA",14);free(sd);
 DWORD got=0;if(ok){*size=(DWORD)n.QuadPart;*bytes=malloc(*size);ok=*bytes&&ReadFile(h,*bytes,*size,&got,NULL)&&got==*size;}DWORD error=GetLastError();CloseHandle(h);if(!ok){free(*bytes);*bytes=NULL;*size=0;return fail(error?error:ERROR_INVALID_DATA);}return true;
}
static bool settled(const L4JournalReader* reader,const L4Layout* roots,const SetupRemoteCommit* value,const SetupOperationPlan* plan){
 Settled s={0};if(!l4_journal_reader_replay(reader,settled_visit,&s)||s.duplicate||!s.recovery||!s.communication||s.recovery>=s.commit||s.communication>=s.commit)return fail(ERROR_INVALID_DATA);
 BYTE *r=NULL,*actual=NULL,*result=NULL,*c=NULL;DWORD rn=0,an=0,n=0,cn=0;L4RecoveryPlan recovery={0};L4CommunicationPlan communication={0};BYTE digest[32];L4RecoveryStatus status;DWORD error=0;
 /* Result binds the validated plan content hash, excluding its trailing checksum. */
 bool ok=l4_journal_reader_find(reader,66,s.recovery,&r,&rn)&&private_read(reader,L"supervisor.recovery",L4_RECOVERY_PLAN_LIMIT,&actual,&an)&&rn==an&&!memcmp(r,actual,rn)&&l4_recovery_decode(roots,r,rn,&recovery)&&l4_recovery_hash(r,rn-32,digest);
 free(actual);actual=NULL;if(ok)ok=private_read(reader,L"supervisor.result",L4_RECOVERY_RESULT_SIZE,&result,&n)&&l4_recovery_result_decode(&recovery,digest,result,n,&status,&error)&&status==L4_RECOVERY_COMMITTED&&!error;
 if(ok)ok=l4_journal_reader_find(reader,70,s.communication,&c,&cn)&&private_read(reader,L"communication.recovery",L4_COMMUNICATION_PLAN_LIMIT,&actual,&an)&&cn==an&&!memcmp(c,actual,cn)&&l4_communication_plan_decode(roots,c,cn,&communication);
 wchar_t id[40],operation[37];GUID guid;swprintf_s(id,40,L"{%hs}",value->operation);const L4ServiceSwitch* supervisor=setup_operation_switch(plan,0,3);
 if(ok)ok=SUCCEEDED(CLSIDFromString(id,&guid))&&!memcmp(&guid,&recovery.operation,sizeof(guid))&&!memcmp(&guid,&communication.operation,sizeof(guid))&&recovery.sequence==value->operation_sequence&&communication.sequence==value->operation_sequence&&
  !memcmp(communication.operation_sha256,value->operation_sha256,32)&&recovery.worker_pid==communication.worker_pid&&!CompareFileTime(&recovery.worker_created,&communication.worker_created)&&
  supervisor&&!wcscmp(recovery.before,supervisor->before.image_path)&&!wcscmp(recovery.after,supervisor->after)&&recovery.start_type==supervisor->before.start_type&&
  recovery.old_size==supervisor->before_size&&recovery.new_size==supervisor->size&&!memcmp(recovery.old_sha256,supervisor->before_sha256,32)&&!memcmp(recovery.new_sha256,supervisor->sha256,32)&&
  value->finished_utc>=recovery.armed_utc&&value->finished_utc<recovery.deadline_utc&&
  MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value->operation,-1,operation,37);
 for(unsigned i=0;ok&&i<2;i++){
  BYTE* raw=NULL;DWORD size=0;ok=communication.switch_sequence[i]==setup_operation_switch_reference(plan,0,i)&&l4_journal_reader_find(reader,10,communication.switch_sequence[i],&raw,&size)&&size==communication.switch_size[i]&&!memcmp(raw,communication.switches[i],size);free(raw);raw=NULL;
  if(ok)ok=communication.config_sequence[i]==value->configs[i+1]&&l4_journal_reader_find(reader,20,communication.config_sequence[i],&raw,&size)&&size==communication.config_size[i]&&!memcmp(raw,communication.configs[i],size);free(raw);
 }
 if(ok){BYTE* raw=NULL;DWORD size=0;ok=communication.con_sequence==setup_operation_switch_reference(plan,0,2)&&l4_journal_reader_find(reader,10,communication.con_sequence,&raw,&size)&&size==communication.con_size&&!memcmp(raw,communication.con_switch,size);free(raw);}
 L4CommunicationDecision* decision=NULL;L4CommunicationPhase phase;
 if(ok)ok=l4_communication_decision_open(roots,operation,1000,&decision)&&l4_communication_decision_read(decision,&phase,&error)&&phase==L4_COMM_DEC_COMMITTED&&!error;
 DWORD saved=GetLastError();l4_communication_decision_close(decision);free(r);free(actual);free(result);free(c);return ok?true:fail(saved?saved:ERROR_INVALID_STATE);
}
bool setup_remote_commit_admit_target(const L4JournalReader* reader,const L4Layout* roots,SetupRemoteCommit* out,SetupOperationPlan** plan){
 if(!out||!plan)return fail(ERROR_INVALID_PARAMETER);memset(out,0,sizeof(*out));*plan=NULL;if(!reader||!roots||!l4_journal_reader_is_immutable(reader))return fail(ERROR_ACCESS_DENIED);
 SetupRemoteCommit p;SetupOperationPlan* admitted=NULL;bool ok=setup_remote_commit_snapshot(reader,&p)&&setup_update_load_operation_snapshot(reader,roots,p.operation_sequence,&admitted);
 unsigned hops=ok?setup_operation_hops(admitted):0,count=0;const SetupManifest* target=hops?setup_operation_target(admitted,hops-1):NULL;const SetupRootManifest* root=hops?setup_operation_target_root(admitted,hops-1):NULL;const BYTE* identity=root?setup_root_identity(root):NULL;
 const L4RoutePlan* route=ok?setup_operation_route(admitted):NULL;const L4CatalogRoute* steps=route?l4_route_steps(route):NULL;const ULONGLONG* configs=ok?setup_operation_configs(admitted,&count):NULL;
 if(ok)ok=target&&identity&&steps&&steps->count==hops&&!strcmp(steps->releases[hops-1].version,p.suite_version)&&!strcmp(l4_route_arch(route),p.arch)&&!memcmp(identity,p.suite_root,32)&&count==12&&configs;
 for(unsigned i=0;ok&&i<12;i++)ok=configs[i]==p.configs[i];
 for(unsigned i=0;ok&&i<4;i++){const L4ServiceSwitch* service=setup_operation_switch(admitted,hops-1,i);ok=service&&!wcscmp(service->after,p.commands[i])&&service->before.start_type==p.start_types[i];}
 if(ok)ok=setup_manifest_verify(target)&&settled(reader,roots,&p,admitted);DWORD error=GetLastError();if(!ok){setup_operation_free(admitted);return fail(error?error:ERROR_REVISION_MISMATCH);}*out=p;*plan=admitted;return true;
}
