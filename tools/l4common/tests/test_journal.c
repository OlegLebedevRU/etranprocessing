#include "../journal_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <aclapi.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL line %u: %s (win32=%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static void cleanup(const wchar_t* root){wchar_t pattern[MAX_PATH],child[MAX_PATH];swprintf_s(pattern,MAX_PATH,L"%ls\\*",root);WIN32_FIND_DATAW d;HANDLE h=FindFirstFileW(pattern,&d);
    if(h!=INVALID_HANDLE_VALUE){do{if(!wcscmp(d.cFileName,L".")||!wcscmp(d.cFileName,L".."))continue;swprintf_s(child,MAX_PATH,L"%ls\\%ls",root,d.cFileName);
        if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(!(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))cleanup(child);else RemoveDirectoryW(child);}else DeleteFileW(child);
    }while(FindNextFileW(h,&d));FindClose(h);}RemoveDirectoryW(root);}
static bool write_fixture(const wchar_t* path,const char* bytes,DWORD size){HANDLE h=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);if(h==INVALID_HANDLE_VALUE)return false;DWORD written;bool ok=WriteFile(h,bytes,size,&written,NULL)&&written==size;CloseHandle(h);return ok;}
static bool contents(const wchar_t* path,const char* expected){char bytes[128]={0};DWORD n=0;HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);if(h==INVALID_HANDLE_VALUE)return false;bool ok=ReadFile(h,bytes,sizeof(bytes),&n,NULL) && n==strlen(expected) && !memcmp(bytes,expected,n);CloseHandle(h);return ok;}
static bool visitor(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){(void)kind;(void)bytes;(void)size;unsigned* count=(unsigned*)context;CHECK(sequence==++*count);return true;}
static bool patch(const wchar_t* path,DWORD at,const void* bytes,DWORD size){HANDLE h=CreateFileW(path,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);if(h==INVALID_HANDLE_VALUE)return false;LARGE_INTEGER offset;offset.QuadPart=at;DWORD written;
    bool ok=SetFilePointerEx(h,offset,NULL,FILE_BEGIN) && WriteFile(h,bytes,size,&written,NULL) && written==size && FlushFileBuffers(h);CloseHandle(h);return ok;}
int wmain(void){
    wchar_t temp[MAX_PATH],root[MAX_PATH],programs[MAX_PATH],data[MAX_PATH],config[MAX_PATH],journal_path[MAX_PATH],fresh[MAX_PATH];
    if(!GetTempPathW(MAX_PATH,temp))return 1;swprintf_s(root,MAX_PATH,L"%lsl4journal-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());if(!CreateDirectoryW(root,NULL))return 1;
    swprintf_s(programs,MAX_PATH,L"%ls\\Programs",root);swprintf_s(data,MAX_PATH,L"%ls\\Data",root);L4Layout layout;
    CHECK(l4_layout_from_roots(&layout,programs,data,L"1.13.2"));CHECK(l4_layout_prepare(&layout));
    CHECK(l4_layout_prepare_leaf(&layout,L"config\\mosquitto",NULL,NULL,true));
    const wchar_t* id=L"17730000-0000-4000-8000-000000000001";L4Journal *j=NULL,*other=NULL;
    CHECK(!l4_journal_open(&layout,L"00000000-0000-0000-0000-000000000000",true,&other));
    CHECK(!l4_journal_open(&layout,L"../escape",true,&other));
    CHECK(!l4_journal_open(&layout,id,false,&other));
    CHECK(l4_journal_open(&layout,id,true,&j));if(!j){cleanup(root);return 1;}
    CHECK(!l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000002",true,&other));CHECK(other==NULL);
    ULONGLONG seq=0;CHECK(l4_journal_append(j,1,"accepted",8,&seq));CHECK(seq==1);
    L4ServiceSwitch plan={0};plan.layout=layout;plan.before.installed=true;plan.before.start_type=SERVICE_AUTO_START;
    wcscpy_s(plan.service,32,L"L4Con");wcscpy_s(plan.before.account,256,L"LocalSystem");
    swprintf_s(plan.before.image_path,2048,L"\"%ls\\releases\\1.13.1\\l4con\\l4con.exe\" --service",layout.binaries);
    swprintf_s(plan.after,2048,L"\"%ls\\l4con\\l4con.exe\" --service",layout.release);plan.size=123;plan.before_size=456;memset(plan.sha256,0x13,32);memset(plan.before_sha256,0x12,32);
    CHECK(l4_journal_save_switch(j,&plan,&seq));CHECK(seq==2);
    swprintf_s(journal_path,MAX_PATH,L"%ls\\journal.bin",j->directory);
    LARGE_INTEGER offset;offset.QuadPart=0;CHECK(SetFilePointerEx(j->file,offset,NULL,FILE_END));CHECK(l4_store_write(j->file,"torn",4));CHECK(FlushFileBuffers(j->file));
    l4_journal_close(j);j=NULL;CHECK(l4_journal_open(&layout,id,false,&j));if(!j){cleanup(root);return 1;}
    CHECK(j->sequence==2);unsigned count=0;CHECK(l4_journal_replay(j,visitor,&count));CHECK(count==2);
    L4ServiceSwitch decoded;CHECK(l4_journal_load_switch(j,2,&decoded));CHECK(!memcmp(&plan,&decoded,sizeof(plan)));
    CHECK(!l4_journal_load_switch(j,1,&decoded));
    /* A damaged later committed frame exposes no earlier records to visitors. */
    offset.QuadPart=24+48+8+4+48;CHECK(SetFilePointerEx(j->file,offset,NULL,FILE_BEGIN));
    BYTE bad=2,good=1;CHECK(l4_store_write(j->file,&bad,1));count=0;
    CHECK(!l4_journal_replay(j,visitor,&count));CHECK(count==0);
    CHECK(SetFilePointerEx(j->file,offset,NULL,FILE_BEGIN));CHECK(l4_store_write(j->file,&good,1));
    count=0;CHECK(l4_journal_replay(j,visitor,&count));CHECK(count==2);
    L4ServiceSwitch foreign=plan;wcscpy_s(foreign.layout.data,MAX_PATH,L"C:\\foreign");CHECK(!l4_journal_save_switch(j,&foreign,NULL));
    /* A header plus incomplete payload is pruned just like a short frame header. */
    BYTE partial[51]={0};l4_store_u32(partial,1);l4_store_u32(partial+4,8);l4_store_u64(partial+8,3);
    offset.QuadPart=(LONGLONG)j->end;CHECK(SetFilePointerEx(j->file,offset,NULL,FILE_BEGIN));CHECK(l4_store_write(j->file,partial,sizeof(partial)));
    l4_journal_close(j);j=NULL;CHECK(l4_journal_open(&layout,id,false,&j));if(!j){cleanup(root);return 1;}CHECK(j->sequence==2);
    l4_journal_close(j);j=NULL;CHECK(patch(journal_path,24+48,"x",1));CHECK(!l4_journal_open(&layout,id,false,&j));CHECK(j==NULL);
    CHECK(patch(journal_path,24+48,"a",1));CHECK(patch(journal_path,0,"X",1));CHECK(!l4_journal_open(&layout,id,false,&j));CHECK(patch(journal_path,0,"L",1));
    CHECK(l4_journal_open(&layout,id,false,&j));if(!j){cleanup(root);return 1;}
    CHECK(l4_layout_data_path(&layout,L"config\\mosquitto\\mosquitto.conf",config));CHECK(write_fixture(config,"old",3));
    ULONGLONG cfg=0;CHECK(l4_config_prepare(j,L"mosquitto\\mosquitto.conf","new",3,&cfg));CHECK(contents(config,"old"));
    ULONGLONG no_write=j->sequence;CHECK(l4_config_verify(j,cfg,false));CHECK(!l4_config_verify(j,cfg,true));CHECK(j->sequence==no_write && contents(config,"old"));
    l4_journal_close(j);j=NULL;CHECK(l4_journal_open(&layout,id,false,&j));if(!j){cleanup(root);return 1;}
    CHECK(l4_config_apply(j,cfg));CHECK(contents(config,"new"));
    no_write=j->sequence;CHECK(l4_config_verify(j,cfg,true));CHECK(!l4_config_verify(j,cfg,false));CHECK(j->sequence==no_write && contents(config,"new"));
    /* Simulate process exit after replacement but before the flushed DONE record. */
    offset.QuadPart=(LONGLONG)j->end-64;CHECK(SetFilePointerEx(j->file,offset,NULL,FILE_BEGIN));CHECK(SetEndOfFile(j->file));CHECK(FlushFileBuffers(j->file));
    l4_journal_close(j);j=NULL;CHECK(l4_journal_open(&layout,id,false,&j));if(!j){cleanup(root);return 1;}
    CHECK(l4_config_apply(j,cfg));CHECK(contents(config,"new"));
    CHECK(l4_config_rollback(j,cfg));CHECK(contents(config,"old"));CHECK(l4_config_rollback(j,cfg));
    CHECK(write_fixture(config,"operator",8));CHECK(!l4_config_apply(j,cfg));CHECK(contents(config,"operator"));CHECK(write_fixture(config,"old",3));
    CHECK(write_fixture(config,"operator",8));no_write=j->sequence;CHECK(!l4_config_verify(j,cfg,false));CHECK(j->sequence==no_write && contents(config,"operator"));CHECK(write_fixture(config,"old",3));
    ULONGLONG created=0;CHECK(l4_config_prepare(j,L"mosquitto\\fresh.conf","fresh",5,&created));
    CHECK(l4_layout_data_path(&layout,L"config\\mosquitto\\fresh.conf",fresh));CHECK(GetFileAttributesW(fresh)==INVALID_FILE_ATTRIBUTES);
    CHECK(l4_config_verify(j,created,false));CHECK(!l4_config_verify(j,created,true));CHECK(GetFileAttributesW(fresh)==INVALID_FILE_ATTRIBUTES);
    CHECK(l4_config_apply(j,created));CHECK(contents(fresh,"fresh"));CHECK(l4_config_rollback(j,created));CHECK(GetFileAttributesW(fresh)==INVALID_FILE_ATTRIBUTES);
    /* Typed decoder rejects a checksummed record with an out-of-bounds SD offset. */
    BYTE* malformed=NULL;DWORD malformed_size=0;CHECK(l4_store_find_record(j,L4_RECORD_CONFIG_PLAN,cfg,&malformed,&malformed_size));
    if(malformed){DWORD sd_at=24+l4_store_get32(malformed+8)+l4_store_get32(malformed+12)+l4_store_get32(malformed+16);
        l4_store_u32(malformed+sd_at+4,0xffffff00u);ULONGLONG broken=0;
        CHECK(l4_journal_append(j,L4_RECORD_CONFIG_PLAN,malformed,malformed_size,&broken));CHECK(!l4_config_apply(j,broken));CHECK(contents(config,"old"));free(malformed);
    }
    wchar_t extra[MAX_PATH];swprintf_s(extra,MAX_PATH,L"%ls:extra",config);CHECK(write_fixture(extra,"ads",3));
    CHECK(!l4_config_prepare(j,L"mosquitto\\mosquitto.conf","bad",3,&seq));CHECK(DeleteFileW(extra));
    swprintf_s(extra,MAX_PATH,L"%ls\\config\\mosquitto\\alias.conf",data);CHECK(CreateHardLinkW(extra,config,NULL));
    CHECK(!l4_config_prepare(j,L"mosquitto\\mosquitto.conf","bad",3,&seq));CHECK(DeleteFileW(extra));
    CHECK(!l4_config_prepare(j,L"missing\\file.conf","bad",3,&seq));
    CHECK(!l4_config_prepare(j,L"..\\state\\outside","bad",3,&seq));CHECK(!l4_config_prepare(j,L"mosquitto\\file:ads","bad",3,&seq));
    /* Built-in broker gets planned per-file read access, never a public parent
     * or Users write. Existing/custom proposals cannot silently change policy. */
    CHECK(!l4_config_prepare_public_broker(j,"public",6,&seq));
    CHECK(DeleteFileW(config));ULONGLONG public_plan=0;
    CHECK(l4_config_prepare_public_broker(j,"public",6,&public_plan));
    CHECK(l4_config_verify(j,public_plan,false));CHECK(l4_config_apply(j,public_plan));
    CHECK(l4_config_verify(j,public_plan,true));CHECK(contents(config,"public"));
    PACL public_acl=NULL;PSECURITY_DESCRIPTOR public_sd=NULL;
    CHECK(GetNamedSecurityInfoW(config,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,NULL,NULL,&public_acl,NULL,&public_sd)==ERROR_SUCCESS);
    BYTE users[SECURITY_MAX_SID_SIZE];DWORD user_size=sizeof(users);TRUSTEE_W trustee={0};ACCESS_MASK public_rights=0;
    CHECK(CreateWellKnownSid(WinBuiltinUsersSid,NULL,users,&user_size));BuildTrusteeWithSidW(&trustee,users);
    CHECK(GetEffectiveRightsFromAclW(public_acl,&trustee,&public_rights)==ERROR_SUCCESS);
    CHECK((public_rights&FILE_GENERIC_READ)==FILE_GENERIC_READ);
    CHECK(!(public_rights&(FILE_WRITE_DATA|FILE_APPEND_DATA|DELETE|WRITE_DAC|WRITE_OWNER)));
    if(public_sd)LocalFree(public_sd);
    CHECK(l4_config_rollback(j,public_plan));CHECK(GetFileAttributesW(config)==INVALID_FILE_ATTRIBUTES);
    CHECK(!l4_journal_append(j,0,NULL,0,NULL));CHECK(!l4_journal_append(j,1,NULL,1,NULL));
    HANDLE token=NULL;CHECK(!OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&token) && GetLastError()==ERROR_NO_TOKEN);if(token)CloseHandle(token);
    /* Real write failure poisons this handle; recovery requires a new open. */
    HANDLE read_only=NULL,original=j->file;
    CHECK(DuplicateHandle(GetCurrentProcess(),original,GetCurrentProcess(),&read_only,GENERIC_READ,FALSE,0));
    if(read_only){j->file=read_only;CHECK(!l4_journal_append(j,1,"fail",4,NULL));CHECK(j->poisoned);
        CHECK(!l4_journal_append(j,1,"retry",5,NULL));CloseHandle(read_only);j->file=original;}
    l4_journal_close(j);j=NULL;CHECK(l4_journal_open(&layout,id,false,&j));
    if(j)CHECK(l4_journal_append(j,1,"recovered",9,NULL));
    l4_journal_close(j);cleanup(root);CHECK(GetFileAttributesW(root)==INVALID_FILE_ATTRIBUTES);
    printf("journal/config: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
