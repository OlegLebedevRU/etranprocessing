#include "../src/remote_commit.h"
#include "../../l4common/journal_internal.h"
#include <sddl.h>
#include "../src/remote_commit.c" /* Exercise private settlement without adding a production API. */
#include <stdio.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL commit:%u %s\n",__LINE__,#x);}}while(0)
static void cleanup(const wchar_t* root){wchar_t pattern[MAX_PATH],child[MAX_PATH];swprintf_s(pattern,MAX_PATH,L"%ls\\*",root);WIN32_FIND_DATAW d;HANDLE h=FindFirstFileW(pattern,&d);if(h!=INVALID_HANDLE_VALUE){do{if(!wcscmp(d.cFileName,L".")||!wcscmp(d.cFileName,L".."))continue;swprintf_s(child,MAX_PATH,L"%ls\\%ls",root,d.cFileName);if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(!(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))cleanup(child);else RemoveDirectoryW(child);}else DeleteFileW(child);}while(FindNextFileW(h,&d));FindClose(h);}RemoveDirectoryW(root);}
static bool write_private(const L4Journal* j,const wchar_t* leaf,const void* bytes,DWORD size){
 wchar_t path[MAX_PATH];swprintf_s(path,MAX_PATH,L"%ls\\%ls",j->directory,leaf);PSECURITY_DESCRIPTOR sd=NULL;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,NULL))return false;
 SECURITY_ATTRIBUTES attrs={sizeof(attrs),sd,FALSE};HANDLE f=CreateFileW(path,GENERIC_WRITE,0,&attrs,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);LocalFree(sd);if(f==INVALID_HANDLE_VALUE)return false;
 DWORD n=0;bool ok=WriteFile(f,bytes,size,&n,NULL)&&n==size&&FlushFileBuffers(f);CloseHandle(f);return ok;
}
/* Actual recovery wire codec and protected files. A valid supervisor receipt
 * must reach the refusing communication adapter; this fixture never grants admission. */
static void settlement_receipt_tests(void){
 wchar_t temp[MAX_PATH],root[MAX_PATH],pf[MAX_PATH],pd[MAX_PATH],image[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));swprintf_s(root,MAX_PATH,L"%lsl4receipt-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());CHECK(CreateDirectoryW(root,NULL));swprintf_s(pf,MAX_PATH,L"%ls\\PF",root);swprintf_s(pd,MAX_PATH,L"%ls\\PD",root);
 L4Layout roots,next;CHECK(l4_layout_from_roots(&roots,pf,pd,L"1.13.7"));CHECK(l4_layout_prepare(&roots));CHECK(l4_layout_from_roots(&next,pf,pd,L"1.13.8"));
 const wchar_t* id=L"17730000-0000-4000-8000-000000000001";L4Journal *j=NULL,*owner=NULL;L4JournalReader* reader=NULL;BYTE *bytes=NULL,result[96],full_hash[32];DWORD size=0;PSECURITY_DESCRIPTOR sd=NULL;
 CHECK(l4_journal_open(&roots,id,true,&j));if(!j){cleanup(root);return;}
 L4RecoveryPlan recovery={0};memcpy(&recovery.operation,j->header+8,16);recovery.worker_pid=GetCurrentProcessId();FILETIME ended,kernel,user;CHECK(GetProcessTimes(GetCurrentProcess(),&recovery.worker_created,&ended,&kernel,&user));recovery.supervisor_pid=recovery.worker_pid;recovery.supervisor_created=recovery.worker_created;FILETIME now;GetSystemTimeAsFileTime(&now);recovery.armed_utc=((ULONGLONG)now.dwHighDateTime<<32)|now.dwLowDateTime;recovery.deadline_utc=recovery.armed_utc+600000000ull;
 recovery.sequence=1;recovery.recovery_ms=1000;recovery.start_type=SERVICE_AUTO_START;recovery.old_size=100;recovery.new_size=101;memset(recovery.old_sha256,1,32);memset(recovery.new_sha256,2,32);recovery.old_exists=true;recovery.old_config=(BYTE*)"old";recovery.old_config_size=3;recovery.new_config=(BYTE*)"newer";recovery.new_config_size=5;
 CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FR;;;BU)",SDDL_REVISION_1,&sd,NULL));if(!sd)goto done;recovery.config_sd=sd;recovery.config_sd_size=GetSecurityDescriptorLength(sd);
 CHECK(l4_layout_component(&roots,L"l4superv",L"l4superv.exe",image));swprintf_s(recovery.before,2048,L"\"%ls\" --service",image);CHECK(l4_layout_component(&next,L"l4superv",L"l4superv.exe",image));swprintf_s(recovery.after,2048,L"\"%ls\" --service",image);
 CHECK(l4_recovery_encode(&roots,&recovery,&bytes,&size));if(!bytes)goto done;
 CHECK(l4_journal_append(j,66,bytes,size,NULL));CHECK(l4_journal_append(j,70,"fixture",7,NULL));CHECK(l4_journal_append(j,102,"fixture-not-target-trust",24,NULL));CHECK(write_private(j,L"supervisor.recovery",bytes,size));CHECK(write_private(j,L"communication.recovery","fixture",7));
 /* The real guard writer takes this authenticated trailing content checksum. */
 CHECK(l4_recovery_result_encode(&recovery,bytes+size-32,L4_RECOVERY_COMMITTED,0,result));CHECK(write_private(j,L"supervisor.result",result,sizeof(result)));l4_journal_close(j);j=NULL;
 CHECK(l4_journal_open(&roots,L"17730000-0000-4000-8000-000000000004",true,&owner));if(!owner)goto done;CHECK(l4_journal_reader_open(owner,id,&reader));if(!reader)goto done;
 SetupRemoteCommit commit={0};strcpy_s(commit.operation,37,"17730000-0000-4000-8000-000000000001");
 SetLastError(0);CHECK(!settled(reader,&roots,&commit,NULL)&&GetLastError()==ERROR_CALL_NOT_IMPLEMENTED);
 /* Whole-file hash is a different identity and must be refused. */
 CHECK(l4_recovery_hash(bytes,size,full_hash));CHECK(memcmp(full_hash,bytes+size-32,32));CHECK(l4_recovery_result_encode(&recovery,full_hash,L4_RECOVERY_COMMITTED,0,result));
 wchar_t result_path[MAX_PATH];swprintf_s(result_path,MAX_PATH,L"%ls\\%ls",roots.operations,id);L4Journal target={0};wcscpy_s(target.directory,MAX_PATH,result_path);
 CHECK(write_private(&target,L"supervisor.result",result,sizeof(result)));SetLastError(0);CHECK(!settled(reader,&roots,&commit,NULL)&&GetLastError()==ERROR_INVALID_DATA);
 L4RecoveryPlan foreign=recovery;foreign.operation.Data1++;CHECK(l4_recovery_result_encode(&foreign,bytes+size-32,L4_RECOVERY_COMMITTED,0,result));CHECK(write_private(&target,L"supervisor.result",result,sizeof(result)));SetLastError(0);CHECK(!settled(reader,&roots,&commit,NULL)&&GetLastError()==ERROR_INVALID_DATA);
 CHECK(l4_recovery_result_encode(&recovery,bytes+size-32,L4_RECOVERY_COMMITTED,0,result));result[64]^=1;CHECK(write_private(&target,L"supervisor.result",result,sizeof(result)));SetLastError(0);CHECK(!settled(reader,&roots,&commit,NULL)&&GetLastError()==ERROR_INVALID_DATA);
 CHECK(l4_recovery_result_encode(&recovery,bytes+size-32,L4_RECOVERY_RESTORED,0,result));CHECK(write_private(&target,L"supervisor.result",result,sizeof(result)));SetLastError(0);CHECK(!settled(reader,&roots,&commit,NULL)&&GetLastError()!=ERROR_CALL_NOT_IMPLEMENTED);
 CHECK(l4_recovery_result_encode(&recovery,bytes+size-32,L4_RECOVERY_COMMITTED,0,result));CHECK(write_private(&target,L"supervisor.result",result,sizeof(result)));bytes[40]^=1;CHECK(write_private(&target,L"supervisor.recovery",bytes,size));SetLastError(0);CHECK(!settled(reader,&roots,&commit,NULL)&&GetLastError()!=ERROR_CALL_NOT_IMPLEMENTED);bytes[40]^=1;
done: l4_journal_reader_close(reader);l4_journal_close(owner);l4_journal_close(j);free(bytes);if(sd)LocalFree(sd);cleanup(root);CHECK(GetFileAttributesW(root)==INVALID_FILE_ATTRIBUTES);
}
/* Real protected snapshot binding; raw fixture64 is NOT signed authority. */
static void snapshot_tests(SetupRemoteCommit p){
 wchar_t temp[MAX_PATH],root[MAX_PATH],pf[MAX_PATH],pd[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));swprintf_s(root,MAX_PATH,L"%lsl4commit-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());CHECK(CreateDirectoryW(root,NULL));swprintf_s(pf,MAX_PATH,L"%ls\\PF",root);swprintf_s(pd,MAX_PATH,L"%ls\\PD",root);L4Layout layout;CHECK(l4_layout_from_roots(&layout,pf,pd,L"1.13.7"));CHECK(l4_layout_prepare(&layout));
 const wchar_t* id=L"17730000-0000-4000-8000-000000000001";const wchar_t* ownerid=L"17730000-0000-4000-8000-000000000004";L4Journal *j=NULL,*owner=NULL;L4JournalReader* r=NULL;BYTE b[SETUP_REMOTE_COMMIT_BYTES];ULONGLONG seq;
 CHECK(l4_journal_open(&layout,id,true,&j));if(!j){cleanup(root);return;}for(unsigned i=0;i<12;i++)CHECK(l4_journal_append(j,20,"fixture",7,&p.configs[i]));CHECK(l4_journal_append(j,64,"fixture64",9,&p.operation_sequence));CHECK(l4_store_hash("fixture64",9,NULL,0,p.operation_sha256));CHECK(setup_remote_commit_encode(&p,b));CHECK(l4_journal_append(j,102,b,sizeof(b),&seq));l4_journal_close(j);
 CHECK(l4_journal_open(&layout,ownerid,true,&owner));if(owner){CHECK(l4_journal_reader_open(owner,id,&r));if(r){SetupRemoteCommit out;CHECK(setup_remote_commit_snapshot(r,&out));CHECK(out.operation_sequence==13&&!strcmp(out.updater_version,"1.13.6"));l4_journal_reader_close(r);}l4_journal_close(owner);}
 CHECK(l4_journal_open(&layout,id,false,&j));if(j){CHECK(l4_journal_append(j,102,b,sizeof(b),&seq));l4_journal_close(j);}
 CHECK(l4_journal_open(&layout,ownerid,false,&owner));if(owner){CHECK(l4_journal_reader_open(owner,id,&r));if(r){SetupRemoteCommit out;CHECK(!setup_remote_commit_snapshot(r,&out)&&GetLastError()==ERROR_INVALID_DATA);l4_journal_reader_close(r);}l4_journal_close(owner);}cleanup(root);CHECK(GetFileAttributesW(root)==INVALID_FILE_ATTRIBUTES);
}
int main(void){
 settlement_receipt_tests();
 SetupRemoteCommit p={0},out;BYTE b[SETUP_REMOTE_COMMIT_BYTES],bad[SETUP_REMOTE_COMMIT_BYTES];
 strcpy_s(p.operation,37,"17730000-0000-4000-8000-000000000001");strcpy_s(p.predecessor,37,"17730000-0000-4000-8000-000000000002");strcpy_s(p.updater_origin,37,p.predecessor);
 strcpy_s(p.suite_version,32,"1.13.7");strcpy_s(p.updater_version,32,"1.13.6");strcpy_s(p.arch,8,"x86");memset(p.suite_root,1,32);memset(p.updater_root,2,32);memset(p.publisher,3,32);memset(p.operation_sha256,4,32);
 strcpy_s(p.updater.name,40,"l4setup.exe");p.updater.size=1234;memset(p.updater.sha256,5,32);p.operation_sequence=64;p.finished_utc=123456;
 for(unsigned i=0;i<12;i++)p.configs[i]=i+1;for(unsigned i=0;i<4;i++){p.start_types[i]=SERVICE_AUTO_START;p.pids[i]=100+i;p.births[i].dwLowDateTime=1000+i;swprintf_s(p.commands[i],2048,L"\"C:\\Program Files\\Leo4\\Tools\\releases\\1.13.7\\tool%u.exe\" --service",i);}
 snapshot_tests(p);CHECK(setup_remote_commit_encode(&p,b));CHECK(setup_remote_commit_decode(b,sizeof(b),&out));CHECK(!strcmp(out.suite_version,"1.13.7")&&!strcmp(out.updater_version,"1.13.6"));CHECK(out.configs[11]==12&&out.pids[3]==103&&out.births[3].dwLowDateTime==1003);CHECK(!wcscmp(out.commands[2],p.commands[2]));
 CHECK(!setup_remote_commit_decode(b,sizeof(b)-1,&out));CHECK(out.operation_sequence==0);memcpy(bad,b,sizeof(b));bad[0]^=1;CHECK(!setup_remote_commit_decode(bad,sizeof(bad),&out));
 memcpy(bad,b,sizeof(b));bad[8+8+32+37*3+32*2+4]=1;CHECK(!setup_remote_commit_decode(bad,sizeof(bad),&out));
 p.configs[11]=p.configs[0];CHECK(!setup_remote_commit_encode(&p,bad));p.configs[11]=64;CHECK(!setup_remote_commit_encode(&p,bad));p.configs[11]=12;
 p.pids[2]=0;CHECK(!setup_remote_commit_encode(&p,bad));p.pids[2]=102;p.commands[3][2048-1]=L'x';for(unsigned i=0;i<2048-1;i++)p.commands[3][i]=L'x';CHECK(!setup_remote_commit_encode(&p,bad));
 CHECK(!setup_remote_commit_snapshot(NULL,&out));CHECK(!setup_remote_commit_save(NULL,&p)&&GetLastError()==ERROR_CALL_NOT_IMPLEMENTED);
 printf("remote commit codec/receipt boundary: %u checks, %u failures; no target admission\n",checks,failures);return failures?1:0;
}
