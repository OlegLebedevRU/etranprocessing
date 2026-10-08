#include "update_guard.h"
#include <limits.h>
#include <string.h>

static bool owner_valid(const char* owner){
    if(!owner || strlen(owner)!=36)return false;
    bool nonzero=false;
    for(unsigned i=0;i<36;i++){
        char c=owner[i];
        if(i==8 || i==13 || i==18 || i==23){if(c!='-')return false;}
        else if((c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F')){
            if(c!='0')nonzero=true;
        }else return false;
    }
    return nonzero;
}
static bool same_owner(const L4UpdateGuard* guard,const char* owner){
    return owner_valid(owner) && guard->owner[0] && !_stricmp(guard->owner,owner);
}
static void observe(L4UpdateGuard* guard,ULONGLONG now){
    if(now>=guard->deadline)guard->expired=true;
}
bool l4_update_guard_enter(L4UpdateGuard* guard,L4UpdateWork work,ULONGLONG now,L4UpdateTicket* ticket){
    if(!guard || !ticket || ticket->guard || (work!=L4_WORK_COMMUNICATION && work!=L4_WORK_OTHER_TOOLS))return false;
    AcquireSRWLockExclusive(&guard->lock);
    if(guard->owner[0])observe(guard,now);
    bool allowed=!guard->owner[0] || (guard->window==L4_UPDATE_OTHER_TOOLS && work==L4_WORK_COMMUNICATION && !guard->expired);
    if(allowed && guard->active[work]!=UINT_MAX){
        ++guard->active[work];ticket->guard=guard;ticket->work=work;
    }else allowed=false;
    ReleaseSRWLockExclusive(&guard->lock);return allowed;
}
bool l4_update_guard_leave(L4UpdateTicket* ticket){
    if(!ticket || !ticket->guard || (ticket->work!=L4_WORK_COMMUNICATION && ticket->work!=L4_WORK_OTHER_TOOLS))return false;
    L4UpdateGuard* guard=ticket->guard;AcquireSRWLockExclusive(&guard->lock);
    bool ok=guard->active[ticket->work]!=0;
    if(ok){--guard->active[ticket->work];ticket->guard=NULL;}
    ReleaseSRWLockExclusive(&guard->lock);return ok;
}
bool l4_update_guard_begin(L4UpdateGuard* guard,const char* owner,L4UpdateWindow window,ULONGLONG now,ULONGLONG deadline){
    if(!guard || !owner_valid(owner) || (window!=L4_UPDATE_COMMUNICATION && window!=L4_UPDATE_OTHER_TOOLS) || deadline<=now)return false;
    AcquireSRWLockExclusive(&guard->lock);bool ok;
    if(guard->owner[0]){
        ok=same_owner(guard,owner) && guard->window==window && guard->deadline==deadline;
        if(ok){observe(guard,now);ok=!guard->expired;}
    }else{
        strcpy_s(guard->owner,sizeof(guard->owner),owner);guard->window=window;
        guard->deadline=deadline;guard->expired=false;ok=true;
    }
    ReleaseSRWLockExclusive(&guard->lock);return ok;
}
bool l4_update_guard_ready(L4UpdateGuard* guard,const char* owner,ULONGLONG now){
    if(!guard)return false;
    AcquireSRWLockExclusive(&guard->lock);bool ok=false;
    if(same_owner(guard,owner)){
        observe(guard,now);ok=!guard->expired && !guard->active[L4_WORK_OTHER_TOOLS] &&
            (guard->window==L4_UPDATE_OTHER_TOOLS || !guard->active[L4_WORK_COMMUNICATION]);
    }
    ReleaseSRWLockExclusive(&guard->lock);return ok;
}
bool l4_update_guard_finish(L4UpdateGuard* guard,const char* owner){
    if(!guard)return false;
    AcquireSRWLockExclusive(&guard->lock);
    bool ok=same_owner(guard,owner) && !guard->active[0] && !guard->active[1];
    if(ok){guard->owner[0]=0;guard->window=0;guard->deadline=0;guard->expired=false;}
    ReleaseSRWLockExclusive(&guard->lock);return ok;
}
