#include "../src/broker_config.h"
#include "../src/install_path.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/tests/update_state_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures,fault,writes;static wchar_t registry_name[128];
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL profiles %u %s error%lu\n",__LINE__,#x,GetLastError());}}while(0)
static LONG test_open(HKEY parent,LPCWSTR name,DWORD options,REGSAM rights,PHKEY output){
    CHECK(parent==HKEY_LOCAL_MACHINE && !wcscmp(name,L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment") && options==0 && (rights&KEY_WOW64_64KEY));
    return RegOpenKeyExW(HKEY_CURRENT_USER,registry_name,0,KEY_QUERY_VALUE|KEY_SET_VALUE,output);
}
static BOOL test_sid(PSID sid,WELL_KNOWN_SID_TYPE type){(void)sid;CHECK(type==WinLocalSystemSid);return fault!=8;}
static LONG test_set(HKEY key,LPCWSTR name,DWORD reserved,DWORD type,const BYTE* value,DWORD size){
    ++writes;if(fault==9)return ERROR_WRITE_FAULT;LONG e=RegSetValueExW(key,name,reserved,type,value,size);return fault==10?ERROR_WRITE_FAULT:e;
}
static LONG test_flush(HKEY key){return fault==11?ERROR_WRITE_FAULT:RegFlushKey(key);}
#define RegOpenKeyExW test_open
#define IsWellKnownSid test_sid
#define RegSetValueExW test_set
#define RegFlushKey test_flush
#include "../src/install_path.c"
#undef RegOpenKeyExW
#undef IsWellKnownSid
#undef RegSetValueExW
#undef RegFlushKey
static void broker_tests(void){L4Layout layout;SetupBrokerConfig config;
    CHECK(l4_layout_from_roots(&layout,L"C:\\Program Files\\L4Tools",L"C:\\ProgramData\\L4Tools",L"1.13.3"));
    CHECK(setup_broker_render(&layout,"Synthetic773",&config));CHECK(config.size==strlen(config.bytes));
    CHECK(strstr(config.bytes,"listener 1883 127.0.0.1\n") && strstr(config.bytes,"address 127.0.0.1:18883\n") && strstr(config.bytes,"remote_clientid Synthetic773\n"));
    CHECK(strstr(config.bytes,"log_dest file C:/ProgramData/L4Tools/logs/mosquitto/mosquitto.log\n"));
    CHECK(!strstr(config.bytes,"acl_file") && !strstr(config.bytes,"password") && !strstr(config.bytes,"local_clientid"));
    const char* out[]={"app","svc","evt","req","res","out","ctl","fmr"};const char* in[]={"tsk","rsp","eva","cmt","ctl","fmc"};unsigned routes=0;
    for(const char* p=config.bytes;(p=strstr(p,"topic "))!=NULL;p+=6)routes++;CHECK(routes==14);
    for(unsigned i=0;i<_countof(out);i++){char line[128];snprintf(line,sizeof(line),"topic dev/Synthetic773/%s out 1\n",out[i]);CHECK(strstr(config.bytes,line));}
    for(unsigned i=0;i<_countof(in);i++){char line[128];snprintf(line,sizeof(line),"topic srv/Synthetic773/%s in 1\n",in[i]);CHECK(strstr(config.bytes,line));}
    CHECK(setup_broker_render_standby(&layout,&config));CHECK(config.size==strlen(config.bytes));
    CHECK(strstr(config.bytes,"listener 1883 127.0.0.1") && strstr(config.bytes,"log_dest file "));
    CHECK(!strstr(config.bytes,"remote_clientid") && !strstr(config.bytes,"connection platerra-upstream") && !strstr(config.bytes,"topic "));
    CHECK(!setup_broker_render_standby(NULL,&config) && !config.size);CHECK(!setup_broker_render_standby(&layout,NULL));
    const char* invalid[]={"","bad\nlistener 1884 0.0.0.0","bad#","bad/","bad ","bad%"};
    for(unsigned i=0;i<_countof(invalid);i++)CHECK(!setup_broker_render(&layout,invalid[i],&config) && !config.size);
    CHECK(!setup_broker_render(&layout,NULL,&config));CHECK(!setup_broker_render(NULL,"abc",&config));CHECK(!setup_broker_render(&layout,"abc",NULL));
    L4Layout drift=layout;drift.logs[0]=L'D';CHECK(!setup_broker_render(&drift,"abc",&config));
    CHECK(l4_layout_from_roots(&drift,L"C:\\PF",L"C:\\Data#injection",L"1.13.3"));CHECK(!setup_broker_render(&drift,"abc",&config));
    char long_sn[129];memset(long_sn,'a',128);long_sn[128]=0;CHECK(!setup_broker_render(&layout,long_sn,&config));
}
typedef struct {unsigned plan,intent,done,undo,undone;} Records;
static bool record_visit(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){
    Records* r=context;CHECK(sequence && bytes);
    if(kind==PATH_PLAN){CHECK(size>=26 && !memcmp(bytes,"L4PATH01",8));r->plan++;}
    if(kind>=PATH_INTENT && kind<=PATH_UNDO_DONE){CHECK(size==8 && l4_store_get64(bytes));if(kind==PATH_INTENT)r->intent++;if(kind==PATH_DONE)r->done++;if(kind==PATH_UNDO_INTENT)r->undo++;if(kind==PATH_UNDO_DONE)r->undone++;}
    return true;
}
int wmain(void){broker_tests();
    for(unsigned scenario=0;scenario<=15;scenario++){
        fault=scenario;writes=0;UpdateFixture fixture;CHECK(update_fixture_init(&fixture));
        swprintf_s(registry_name,_countof(registry_name),L"Software\\L4InstallPathFixture-%lu-%llu-%u",GetCurrentProcessId(),GetTickCount64(),scenario);
        HKEY key=NULL;CHECK(RegCreateKeyExW(HKEY_CURRENT_USER,registry_name,0,NULL,0,KEY_ALL_ACCESS,NULL,&key,NULL)==ERROR_SUCCESS);
        wchar_t initial[2048];wcscpy_s(initial,_countof(initial),L"%SystemRoot%\\system32;C:\\Other");DWORD type=REG_EXPAND_SZ;
        if(scenario==1)type=REG_SZ;
        if(scenario==2)swprintf_s(initial,_countof(initial),L"C:\\Other;\"%ls\\\"",fixture.layout.launchers);
        if(scenario==3)swprintf_s(initial,_countof(initial),L"C:\\Other;%ls-extra",fixture.layout.launchers);
        if(scenario==4)initial[0]=0;
        if(scenario==5)type=REG_BINARY;
        if(scenario!=6)CHECK(RegSetValueExW(key,L"Path",0,type,(BYTE*)initial,(DWORD)((wcslen(initial)+1)*2))==ERROR_SUCCESS);
        if(scenario==7){const wchar_t embedded[]={L'a',0,L'b',0};CHECK(RegSetValueExW(key,L"Path",0,REG_SZ,(BYTE*)embedded,sizeof(embedded))==ERROR_SUCCESS);}
        L4Journal* journal=NULL;CHECK(l4_journal_open(&fixture.layout,L"17730000-0000-4000-8000-000000000009",true,&journal));
        SetupInstallPath* plan=NULL;bool prepared=setup_path_prepare(journal,&plan);CHECK(prepared==(scenario!=5 && scenario!=7 && scenario!=8));
        if(prepared){ULONGLONG saved=setup_path_sequence(plan);CHECK(saved);setup_path_free(plan);plan=NULL;CHECK(setup_path_load(journal,saved,&plan));if(!plan)return 1;
            CHECK(!writes);if(scenario==12){const wchar_t foreign[]=L"C:\\Operator";CHECK(RegSetValueExW(key,L"Path",0,REG_SZ,(BYTE*)foreign,sizeof(foreign))==ERROR_SUCCESS);}
            if(scenario==13)journal->header[0]^=1;if(scenario==15)journal->layout.launchers[0]^=1;
            bool applied=setup_path_apply(journal,plan);CHECK(applied==(scenario<9 || scenario==14));
            if(scenario==13)journal->header[0]^=1;if(scenario==15)journal->layout.launchers[0]^=1;
            if(applied){wchar_t actual[4096];DWORD size=sizeof(actual),actual_type=0;CHECK(RegQueryValueExW(key,L"Path",NULL,&actual_type,(BYTE*)actual,&size)==ERROR_SUCCESS);
                CHECK(actual_type==(scenario==6?REG_EXPAND_SZ:type));CHECK(wcsstr(actual,L"C:\\Other") || scenario==4 || scenario==6);
                CHECK(scenario==2?!wcscmp(actual,initial):wcsstr(actual,fixture.layout.launchers)!=NULL);unsigned previous=writes;CHECK(setup_path_apply(journal,plan) && writes==previous);
            }
            if(scenario==14){const wchar_t foreign[]=L"C:\\Operator";CHECK(RegSetValueExW(key,L"Path",0,REG_SZ,(BYTE*)foreign,sizeof(foreign))==ERROR_SUCCESS);}
            fault=0;setup_path_free(plan);plan=NULL;CHECK(setup_path_load(journal,saved,&plan));if(!plan)return 1;
            bool rollback=setup_path_rollback(journal,plan);CHECK(rollback==(scenario!=12 && scenario!=14));
            if(rollback){CHECK(equal(plan,false));CHECK(setup_path_rollback(journal,plan));}
            else{wchar_t actual[128];DWORD size=sizeof(actual);CHECK(RegQueryValueExW(key,L"Path",NULL,NULL,(BYTE*)actual,&size)==ERROR_SUCCESS && !wcscmp(actual,L"C:\\Operator"));}
            Records records={0};CHECK(l4_journal_replay(journal,record_visit,&records));CHECK(records.plan==1);
            CHECK(records.intent==(scenario==12 || scenario==13 || scenario==15?0u:1u));CHECK(records.done==(applied?1u:0u));
            CHECK(records.undo==(rollback && records.intent?1u:0u) && records.undone==records.undo);
            setup_path_free(plan);plan=NULL;CHECK(setup_path_load(journal,saved,&plan));if(!plan)return 1;if(rollback && records.intent)CHECK(plan->undo && !setup_path_apply(journal,plan));setup_path_free(plan);
        }
        RegCloseKey(key);CHECK(RegDeleteKeyW(HKEY_CURRENT_USER,registry_name)==ERROR_SUCCESS);l4_journal_close(journal);CHECK(update_fixture_dispose(&fixture));
    }
    printf("Install profiles: %u checks, %u failures; real isolated HKCU/ACL/files/journal, production fixed HKLM binding and SYSTEM modeled; no live PATH/services/MQTT\n",checks,failures);return failures?1:0;
}
