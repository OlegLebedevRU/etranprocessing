#include "../src/remote_commit.h"
#include "../../l4common/journal_internal.h"
#include <stdio.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL commit:%u %s\n",__LINE__,#x);}}while(0)
static void cleanup(const wchar_t* root){wchar_t pattern[MAX_PATH],child[MAX_PATH];swprintf_s(pattern,MAX_PATH,L"%ls\\*",root);WIN32_FIND_DATAW d;HANDLE h=FindFirstFileW(pattern,&d);if(h!=INVALID_HANDLE_VALUE){do{if(!wcscmp(d.cFileName,L".")||!wcscmp(d.cFileName,L".."))continue;swprintf_s(child,MAX_PATH,L"%ls\\%ls",root,d.cFileName);if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(!(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))cleanup(child);else RemoveDirectoryW(child);}else DeleteFileW(child);}while(FindNextFileW(h,&d));FindClose(h);}RemoveDirectoryW(root);}
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
 printf("remote commit typed closed groundwork: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
