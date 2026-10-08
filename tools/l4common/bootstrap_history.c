#include "bootstrap_history.h"
#include "journal_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool phase_record(DWORD kind,ULONGLONG number,const void* data,DWORD size,void* context){
    (void)number;L4BootstrapHistory* p=(L4BootstrapHistory*)context;
    if((kind<L4_RECORD_BOOTSTRAP_CREATE_INTENT || kind>L4_RECORD_BOOTSTRAP_COMMIT_DONE) && kind!=L4_RECORD_LOCAL_DEPLOY_BEGIN && kind!=L4_RECORD_LOCAL_DEPLOY_DONE && kind!=L4_RECORD_SUPERVISOR_CRASH_INTENT && kind!=L4_RECORD_SUPERVISOR_CRASH_DONE)return true;
    bool whole=kind==L4_RECORD_LOCAL_DEPLOY_BEGIN || kind==L4_RECORD_LOCAL_DEPLOY_DONE || kind==L4_RECORD_BOOTSTRAP_ROLLBACK_BEGIN || kind==L4_RECORD_BOOTSTRAP_ROLLBACK_DONE || kind==L4_RECORD_BOOTSTRAP_COMMIT_BEGIN || kind==L4_RECORD_BOOTSTRAP_COMMIT_DONE;
    if(size!=(whole?8u:(kind==L4_RECORD_BOOTSTRAP_PROCESS?24u:12u)))return l4_store_fail(ERROR_INVALID_DATA);
    const BYTE* bytes=(const BYTE*)data;ULONGLONG plan=l4_store_get64(bytes);if(!plan)return l4_store_fail(ERROR_INVALID_DATA);
    unsigned index=whole?0:l4_store_get32(bytes+8);if(!whole && index>=L4_BOOTSTRAP_SERVICES)return l4_store_fail(ERROR_INVALID_DATA);
    if(plan!=p->plan)return true;
    if(p->complete || p->committed || p->local_done)return l4_store_fail(ERROR_INVALID_DATA);
    if(p->committing && kind<=L4_RECORD_BOOTSTRAP_PROCESS && !p->rollback && kind!=L4_RECORD_BOOTSTRAP_ROLLBACK_BEGIN)return l4_store_fail(ERROR_INVALID_DATA);
    if(kind==L4_RECORD_SUPERVISOR_CRASH_INTENT){if(index!=3 || p->rollback || p->committing || !(p->created&8u))return l4_store_fail(ERROR_INVALID_DATA);p->crash_intent=true;}
    else if(kind==L4_RECORD_SUPERVISOR_CRASH_DONE){if(index!=3 || p->rollback || p->committing || !p->crash_intent)return l4_store_fail(ERROR_INVALID_DATA);p->crash_done=true;}
    else if(kind==L4_RECORD_BOOTSTRAP_CREATE_INTENT){if(p->rollback)return l4_store_fail(ERROR_INVALID_DATA);p->created|=1u<<index;}
    else if(kind==L4_RECORD_BOOTSTRAP_CREATE_DONE){if(p->rollback || !(p->created&(1u<<index)))return l4_store_fail(ERROR_INVALID_DATA);p->registered|=1u<<index;}
    else if(kind==L4_RECORD_BOOTSTRAP_START_INTENT){if(p->rollback || p->registered!=15u || (p->ready&((1u<<index)-1u))!=((1u<<index)-1u))return l4_store_fail(ERROR_INVALID_DATA);p->started|=1u<<index;}
    else if(kind==L4_RECORD_BOOTSTRAP_READY){if(p->rollback || !(p->started&(1u<<index)) || !p->pids[index])return l4_store_fail(ERROR_INVALID_DATA);p->ready|=1u<<index;}
    else if(kind==L4_RECORD_BOOTSTRAP_PROCESS){
        DWORD pid=l4_store_get32(bytes+12);ULONGLONG birth=l4_store_get64(bytes+16);
        if(!(p->started&(1u<<index)) || !pid || !birth || (p->rollback && (p->pids[index] || (p->stopping&(1u<<index)))))return l4_store_fail(ERROR_INVALID_DATA);
        p->pids[index]=pid;p->births[index]=birth;
    }
    else if(kind==L4_RECORD_BOOTSTRAP_STOP_INTENT){unsigned upper=15u&~((1u<<(index+1))-1u);if(!p->rollback || (p->stopped&upper)!=upper)return l4_store_fail(ERROR_INVALID_DATA);p->stopping|=1u<<index;}
    else if(kind==L4_RECORD_BOOTSTRAP_STOP_DONE){if(!p->rollback || !(p->stopping&(1u<<index)) || ((p->started&(1u<<index)) && !p->pids[index]))return l4_store_fail(ERROR_INVALID_DATA);p->stopped|=1u<<index;}
    else if(kind==L4_RECORD_BOOTSTRAP_ROLLBACK_BEGIN)p->rollback=true;
    else if(kind==L4_RECORD_BOOTSTRAP_ROLLBACK_DONE){if(!p->rollback)return l4_store_fail(ERROR_INVALID_DATA);p->complete=true;}
    else if(kind==L4_RECORD_LOCAL_DEPLOY_BEGIN){if(p->rollback || p->committing || p->registered!=15u || p->started || p->ready)return l4_store_fail(ERROR_INVALID_DATA);p->committing=true;p->local=true;}
    else if(kind==L4_RECORD_LOCAL_DEPLOY_DONE){if(p->rollback || !p->local || !p->committing || p->type_done!=15u)return l4_store_fail(ERROR_INVALID_DATA);p->local_done=true;}
    else if(kind==L4_RECORD_BOOTSTRAP_COMMIT_BEGIN){if(p->rollback || p->committing || p->registered!=15u || p->ready!=15u)return l4_store_fail(ERROR_INVALID_DATA);p->committing=true;}
    else if(kind==L4_RECORD_BOOTSTRAP_START_TYPE_INTENT){if(p->rollback || !p->committing)return l4_store_fail(ERROR_INVALID_DATA);p->type_intent|=1u<<index;}
    else if(kind==L4_RECORD_BOOTSTRAP_START_TYPE_DONE){if(p->rollback || !(p->type_intent&(1u<<index)))return l4_store_fail(ERROR_INVALID_DATA);p->type_done|=1u<<index;}
    else if(kind==L4_RECORD_BOOTSTRAP_COMMIT_DONE){if(p->rollback || p->local || !p->committing || p->type_done!=15u)return l4_store_fail(ERROR_INVALID_DATA);p->committed=true;}
    else if(!p->rollback)return l4_store_fail(ERROR_INVALID_DATA);
    return true;
}
bool l4_bootstrap_history(L4Journal* j,ULONGLONG sequence,L4BootstrapHistory* p){if(!j||!p||!sequence)return l4_store_fail(ERROR_INVALID_PARAMETER);memset(p,0,sizeof(*p));p->plan=sequence;return l4_journal_replay(j,phase_record,p);}
bool l4_bootstrap_terminal(L4Journal* j,ULONGLONG sequence,bool* committed,bool* aborted){
    if(!committed || !aborted)return l4_store_fail(ERROR_INVALID_PARAMETER);*committed=*aborted=false;
    L4BootstrapPlan plan;L4BootstrapHistory p;if(!l4_bootstrap_load(j,sequence,&plan) || !l4_bootstrap_history(j,sequence,&p))return false;*committed=p.committed || p.local_done;*aborted=p.complete;return true;
}

bool l4_bootstrap_local_terminal(L4Journal* j,ULONGLONG sequence,bool* deployed){
    if(!deployed)return l4_store_fail(ERROR_INVALID_PARAMETER);*deployed=false;
    L4BootstrapPlan plan;L4BootstrapHistory p;if(!l4_bootstrap_load(j,sequence,&plan) || !l4_bootstrap_history(j,sequence,&p))return false;*deployed=p.local_done;return true;
}

bool l4_bootstrap_validate(const L4BootstrapPlan* plan){
    if(!plan)return l4_store_fail(ERROR_INVALID_PARAMETER);
    const wchar_t* paths[]={plan->layout.binaries,plan->layout.data,plan->layout.release,plan->layout.launchers,plan->layout.config,plan->layout.state,plan->layout.logs,plan->layout.operations,plan->layout.cache,plan->layout.staging};
    for(unsigned i=0;i<_countof(paths);i++)if(wcsnlen_s(paths[i],MAX_PATH)>=MAX_PATH)return l4_store_fail(ERROR_INVALID_DATA);
    const wchar_t* version=wcsrchr(plan->layout.release,L'\\');L4Layout canonical;
    if(!version || !l4_layout_from_roots(&canonical,plan->layout.binaries,plan->layout.data,version+1) || memcmp(&canonical,&plan->layout,sizeof(canonical)))return l4_store_fail(ERROR_INVALID_DATA);
    const wchar_t* services[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};
    const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};
    for(unsigned i=0;i<L4_BOOTSTRAP_SERVICES;i++){
        if(wcsnlen_s(plan->services[i],32)>=32 || wcscmp(plan->services[i],services[i]) || wcsnlen_s(plan->commands[i],2048)>=2048 ||
            (plan->start_types[i]!=SERVICE_AUTO_START && plan->start_types[i]!=SERVICE_DEMAND_START) || !plan->sizes[i] || plan->sizes[i]>512ULL*1024*1024)return l4_store_fail(ERROR_INVALID_DATA);
        wchar_t exe[64],path[MAX_PATH],prefix[MAX_PATH+3];swprintf_s(exe,_countof(exe),L"%ls.exe",components[i]);
        if(!l4_layout_component(&plan->layout,components[i],exe,path))return false;
        swprintf_s(prefix,_countof(prefix),L"\"%ls\"",path);size_t n=wcslen(prefix);
        if(wcsncmp(plan->commands[i],prefix,n) || (plan->commands[i][n] && plan->commands[i][n]!=L' ' && plan->commands[i][n]!=L'\t') || wcslen(plan->commands[i]+n)>1601)return l4_store_fail(ERROR_INVALID_DATA);
        for(const wchar_t* p=plan->commands[i]+n;*p;p++)if(*p<32 && *p!=L'\t')return l4_store_fail(ERROR_INVALID_DATA);
    }
    return true;
}
