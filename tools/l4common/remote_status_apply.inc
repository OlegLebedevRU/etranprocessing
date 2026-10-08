/* Protected history reducer only. No record here grants permission to apply,
 * stop a service, clear the marker or mint whole-suite completion authority. */
static bool apply_zero(const BYTE* b,DWORD n){for(DWORD i=0;i<n;i++)if(b[i])return false;return true;}
static bool apply_string(const BYTE* b,DWORD n){const BYTE* end=memchr(b,0,n);return end && end!=b && apply_zero(end,(DWORD)(b+n-end));}
static bool apply_uuid(const BYTE* b){if(b[36])return false;bool any=false;for(unsigned i=0;i<36;i++){
    if(i==8 || i==13 || i==18 || i==23){if(b[i]!='-')return false;}else if(!((b[i]>='0'&&b[i]<='9') || (b[i]>='a'&&b[i]<='f')))return false;else if(b[i]!='0')any=true;}return any;}
static bool apply_version(const BYTE* b){if(!apply_string(b,32))return false;unsigned dots=0,digits=0;for(unsigned i=0;b[i];i++){
    if(b[i]=='.'){if(!digits || ++dots>2)return false;digits=0;}else if(b[i]>='0'&&b[i]<='9')digits++;else return false;}return dots==2 && digits;}
static bool apply_ref(const Scan* s,DWORD kind,ULONGLONG ref,ULONGLONG before){
    BYTE* b=NULL;DWORD n=0;if(!ref || ref>=before)return fail(ERROR_INVALID_DATA);
    bool ok=l4_journal_reader_find(s->reader,kind,ref,&b,&n);free(b);return ok;
}
static bool apply_plan(Scan* s,ULONGLONG seq,const BYTE* b,DWORD n){
    /* Historical preparation-only progress did not decode64. Retain observation
     * of it, but no apply or outcome record can follow an untyped64. */
    if(n<28 || l4_store_get32(b)!=1)return true;
    DWORD hops=l4_store_get32(b+20),count=l4_store_get32(b+24);
    if(!hops || hops>L4_CATALOG_MAX_RELEASES || count>16 || count!=12 || n!=28+count*8+hops*32 ||
        l4_store_get64(b+4)!=s->status.packages_sequence || !apply_ref(s,63,l4_store_get64(b+12),seq))return fail(ERROR_INVALID_DATA);
    for(unsigned i=0;i<count;i++){s->configs[i]=l4_store_get64(b+28+i*8);
        if(!apply_ref(s,20,s->configs[i],seq))return false;
        for(unsigned k=0;k<i;k++)if(s->configs[k]==s->configs[i])return fail(ERROR_INVALID_DATA);}
    for(unsigned h=0;h<hops;h++)for(unsigned i=0;i<4;i++){
        s->switches[h][i]=l4_store_get64(b+28+count*8+h*32+i*8);
        if(!apply_ref(s,10,s->switches[h][i],seq))return false;
        for(unsigned k=0;k<h*4+i;k++)if(s->switches[k/4][k%4]==s->switches[h][i])return fail(ERROR_INVALID_DATA);
    }
    s->hops=hops;s->config_count=count;s->typed_plan=true;return true;
}
static bool apply_state(Scan* s,const BYTE* b,DWORD n){
    BYTE hash[32];if(n!=112 || memcmp(b,"L4UPD01",8) || l4_store_get32(b+52) ||
        memcmp(b+8,s->status.operation_id,37) || !apply_zero(b+45,3) ||
        l4_store_get64(b+64)!=s->status.plan_sequence || !l4_store_hash(b,80,NULL,0,hash) || memcmp(hash,b+80,32))return fail(ERROR_INVALID_DATA);
    L4UpdateState next={0};memcpy(next.owner,b+8,40);next.window=l4_store_get32(b+48);
    next.generation=l4_store_get64(b+56);next.plan_sequence=l4_store_get64(b+64);next.deadline_utc=l4_store_get64(b+72);
    if(!next.generation || next.window>2 || (next.window?!next.deadline_utc:next.deadline_utc!=0))return fail(ERROR_INVALID_DATA);
    if(!s->active.generation){if(next.window!=1 || next.generation!=s->communication_generation ||
        next.deadline_utc!=s->communication_deadline || next.deadline_utc>=s->recovery.deadline_utc || next.deadline_utc<=s->recovery.armed_utc)return fail(ERROR_REVISION_MISMATCH);}
    else if(next.generation!=s->active.generation+1 || s->active.generation==~0ull ||
        (next.window && (s->active.window!=1 || next.window!=2 || next.deadline_utc>=s->recovery.deadline_utc ||
            next.deadline_utc<=s->recovery.armed_utc)))return fail(ERROR_REVISION_MISMATCH);
    if(!next.window){
        if(!s->status.has_outcome || s->status.outcome.result.result==L4_REMOTE_OUTCOME_RECOVERY_REQUIRED ||
            s->status.outcome_clear_recorded || s->pending_kind || s->config_pending)return fail(ERROR_INVALID_STATE);
        s->clear=next;s->status.outcome_clear_recorded=true;s->status.outcome_clear_generation=next.generation;
    }else if(s->status.has_outcome)return fail(ERROR_INVALID_STATE);
    s->active=next;return true;
}
static bool apply_action(Scan* s,DWORD kind,ULONGLONG seq,const BYTE* b,DWORD n){
    if(n!=112 || memcmp(b,"L4SCA001",8) || l4_store_get32(b+8)!=1 || !apply_zero(b+60,4) ||
        !apply_zero(b+84,28) || s->config_pending)return fail(ERROR_INVALID_DATA);
    DWORD action=l4_store_get32(b+12),service=l4_store_get32(b+16),window=l4_store_get32(b+20),phase=l4_store_get32(b+80);
    ULONGLONG intent=l4_store_get64(b+72);bool forward=kind==100 || kind==101;
    if(action<1 || action>(forward?3u:4u) || service>=4 || window!=s->active.window ||
        l4_store_get64(b+24)!=s->active.generation || l4_store_get64(b+32)!=s->active.deadline_utc ||
        l4_store_get64(b+40)!=s->status.plan_sequence || l4_store_get64(b+48)!=s->switches[0][service] ||
        !l4_store_get32(b+56) || !l4_store_get64(b+64) || (forward?(phase!=0 || window!=(service<2?1u:2u)):(phase<1 || phase>4)))return fail(ERROR_REVISION_MISMATCH);
    if(forward && s->restoring)return fail(ERROR_INVALID_STATE);
    if(kind==100 || kind==104){
        if(s->pending_kind || intent || (forward?action!=s->forward[service]+1:
            (s->restored[service]?action!=s->restored[service]+1:(action!=1 && action!=4))))return fail(ERROR_INVALID_STATE);
        if(forward && window==2 && (s->forward[0]!=3 || s->forward[1]!=3))return fail(ERROR_INVALID_STATE);
        if(kind==104){s->restoring=true;if((action==2 && phase>2) || (action==3 && phase!=1) || (action==4 && phase!=3))return fail(ERROR_INVALID_DATA);}
        memcpy(s->pending,b,112);s->pending_kind=kind;s->pending_sequence=seq;return true;
    }
    if(kind==106){
        if(s->pending_kind!=100 || intent!=s->pending_sequence || memcmp(b+12,s->pending+12,8) || memcmp(b+40,s->pending+40,16))return fail(ERROR_INVALID_STATE);
        s->pending_kind=0;s->restoring=true;return true;
    }
    if(s->pending_kind!=(kind==101?100u:104u) || intent!=s->pending_sequence ||
        memcmp(b+12,s->pending+12,44) || (action!=3 && memcmp(b+56,s->pending+56,16)))return fail(ERROR_INVALID_DATA);
    if(kind==101){s->forward[service]=action;if(action==3){s->pids[service]=l4_store_get32(b+56);s->births[service]=l4_store_get64(b+64);s->forward_done[service]=seq;}}
    else {if((action==1 && phase>2) || (action==2 && phase!=1) || (action>=3 && phase!=3))return fail(ERROR_INVALID_DATA);
        s->restored[service]=action;if(action>=3){s->old_pids[service]=l4_store_get32(b+56);s->old_births[service]=l4_store_get64(b+64);s->restore_done[service]=seq;}}
    s->pending_kind=0;return true;
}
static bool apply_config(Scan* s,DWORD kind,ULONGLONG seq,const BYTE* b,DWORD n){
    if(n!=12 || s->pending_kind)return fail(ERROR_INVALID_DATA);ULONGLONG ref=l4_store_get64(b);DWORD direction=l4_store_get32(b+8);
    unsigned i=0;while(i<s->config_count && s->configs[i]!=ref)i++;
    if(i==s->config_count || direction>1 || (direction && s->restoring) || (!direction && !s->restoring) ||
        (direction && s->active.window!=(i==1 || i==2?1u:2u)))return fail(ERROR_INVALID_DATA);
    if(kind==21){if(s->config_pending)return fail(ERROR_INVALID_STATE);s->config_pending=seq;s->config_reference=ref;s->config_direction=direction;}
    else {if(!s->config_pending || ref!=s->config_reference || direction!=s->config_direction)return fail(ERROR_INVALID_STATE);s->config_pending=0;s->config_candidate[i]=direction!=0;}
    return true;
}
static bool apply_config_abandon(Scan* s,const BYTE* b,DWORD n){
    if(n!=112 || memcmp(b,"L4CFG001",8) || l4_store_get32(b+8)!=1 || l4_store_get32(b+12)<1 || l4_store_get32(b+12)>2 ||
        l4_store_get32(b+16) || l4_store_get32(b+60) || !apply_zero(b+84,28) || !s->config_pending || s->pending_kind ||
        l4_store_get32(b+20)!=s->active.window || l4_store_get64(b+24)!=s->active.generation || l4_store_get64(b+32)!=s->active.deadline_utc ||
        l4_store_get64(b+40)!=s->status.plan_sequence || l4_store_get64(b+48)!=s->config_reference ||
        l4_store_get32(b+56)!=s->recovery.worker_pid || memcmp(b+64,&s->recovery.worker_created,8) ||
        l4_store_get64(b+72)!=s->config_pending || l4_store_get32(b+80)!=s->config_direction)return fail(ERROR_INVALID_DATA);
    s->config_pending=0;s->restoring=true;return true;
}
static bool apply_completion(Scan* s,DWORD kind,ULONGLONG seq,const BYTE* b,DWORD n){
    if(s->proof_sequence || s->pending_kind || s->config_pending)return fail(ERROR_INVALID_STATE);
    if(kind==102){
        if(s->restoring || s->active.window!=2 || n!=16959 || memcmp(b,"L4SCMT01",8) || l4_store_get64(b+8)!=s->status.plan_sequence ||
            memcmp(b+16,s->operation_hash,32) || memcmp(b+48,s->status.operation_id,37) ||
            !apply_uuid(b+85) || !apply_uuid(b+122) || !memcmp(b+48,b+85,37) || !apply_version(b+159) || !apply_version(b+191) ||
            !apply_string(b+223,8) || (strcmp((const char*)b+223,"x86") && strcmp((const char*)b+223,"x64")) ||
            apply_zero(b+231,32) || apply_zero(b+263,32) || !apply_string(b+295,40) || strcmp((const char*)b+295,"l4setup.exe") ||
            !l4_store_get64(b+335) || apply_zero(b+343,32) || apply_zero(b+375,32))return fail(ERROR_INVALID_DATA);
        /* Header231, roots64, updater asset80, publisher32, UTC8, configs96. */
        if(!l4_store_get64(b+407))return fail(ERROR_INVALID_DATA);s->proof_finished=l4_store_get64(b+407);
        for(unsigned i=0;i<12;i++)if(!s->config_candidate[i] || l4_store_get64(b+415+i*8)!=s->configs[i])return fail(ERROR_REVISION_MISMATCH);
        for(unsigned i=0;i<4;i++){
            const BYTE* v=b+511+i*4112;BYTE* raw=NULL;DWORD size=0;L4ServiceSwitch sw;
            const wchar_t* services[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};
            if(s->forward[i]!=3 || !s->forward_done[i] || l4_store_get32(v+4)!=s->pids[i] || l4_store_get64(v+8)!=s->births[i] ||
                !l4_journal_reader_find(s->reader,10,s->switches[s->hops-1][i],&raw,&size))return fail(ERROR_REVISION_MISMATCH);
            bool ok=l4_switch_decode(&s->original,raw,size,&sw);free(raw);if(!ok)return false;
            size_t length=wcslen(sw.after);if(wcscmp(sw.service,services[i]) || wcscmp(sw.before.account,L"LocalSystem") ||
                (sw.before.start_type!=SERVICE_AUTO_START && sw.before.start_type!=SERVICE_DEMAND_START) ||
                l4_store_get32(v)!=sw.before.start_type || memcmp(v+16,sw.after,(length+1)*2) ||
                !apply_zero(v+16+(DWORD)(length+1)*2,(DWORD)(4096-(length+1)*2)))return fail(ERROR_REVISION_MISMATCH);
        }
        memcpy(s->proof_version,b+159,32);memcpy(s->proof_updater,b+191,32);memcpy(s->proof_arch,b+223,8);memcpy(s->proof_root,b+231,32);
    }else {
        if(!s->restoring || n!=384 || memcmp(b,"L4ROLD01",8) || l4_store_get32(b+8)!=1 || l4_store_get32(b+12)!=s->active.window ||
            l4_store_get64(b+16)!=s->active.generation || l4_store_get64(b+24)!=s->active.deadline_utc ||
            l4_store_get64(b+32)!=s->status.plan_sequence || memcmp(b+40,s->operation_hash,32) ||
            memcmp(b+72,s->status.operation_id,37) || !apply_version(b+109) || !apply_string(b+141,8) ||
            (strcmp((const char*)b+141,"x86") && strcmp((const char*)b+141,"x64")) || apply_zero(b+149,32) ||
            !l4_store_get64(b+181) || l4_store_get32(b+189)!=s->recovery.worker_pid || memcmp(b+193,&s->recovery.worker_created,8) ||
            l4_store_get32(b+377)!=s->active.window || !apply_zero(b+381,3))return fail(ERROR_INVALID_DATA);
        for(unsigned i=0;i<12;i++)if(l4_store_get64(b+249+i*8)!=s->configs[i])return fail(ERROR_REVISION_MISMATCH);
        for(unsigned i=0;i<4;i++)if(s->restored[i]<3 || !s->restore_done[i] || l4_store_get32(b+201+i*4)!=s->old_pids[i] ||
            l4_store_get64(b+217+i*8)!=s->old_births[i] || l4_store_get64(b+345+i*8)!=s->restore_done[i])return fail(ERROR_REVISION_MISMATCH);
        memcpy(s->proof_version,b+109,32);memcpy(s->proof_arch,b+141,8);memcpy(s->proof_root,b+149,32);s->proof_finished=l4_store_get64(b+181);
    }
    if(s->proof_finished<s->status.request.accepted_utc || s->proof_finished>=s->active.deadline_utc)return fail(ERROR_INVALID_DATA);
    s->proof_sequence=seq;s->proof_kind=kind;return l4_store_hash(b,n,NULL,0,s->proof_hash);
}
static bool apply_visit(Scan* s,DWORD kind,ULONGLONG seq,const BYTE* b,DWORD n){
    if(!s->typed_plan || !s->receipt || s->hops!=1)return fail(ERROR_NOT_SUPPORTED);
    if(kind==65)return apply_state(s,b,n);
    if(!s->active.window || s->status.has_outcome)return fail(ERROR_INVALID_STATE);
    if(s->proof_sequence && kind!=103)return fail(ERROR_INVALID_STATE);
    bool ok=false;
    if(kind==21 || kind==22)ok=apply_config(s,kind,seq,b,n);
    else if(kind==100 || kind==101 || kind==104 || kind==105 || kind==106)ok=apply_action(s,kind,seq,b,n);
    else if(kind==107)ok=apply_config_abandon(s,b,n);
    else if(kind==102 || kind==108)ok=apply_completion(s,kind,seq,b,n);
    else if(kind==103){L4RemoteOutcome outcome;
        if(!l4_remote_outcome_decode(b,n,&outcome) || outcome.plan_sequence!=s->status.plan_sequence ||
            memcmp(outcome.plan_sha256,s->operation_hash,32))return fail(ERROR_INVALID_DATA);
        if(outcome.result.result==L4_REMOTE_OUTCOME_RECOVERY_REQUIRED && s->proof_sequence)return fail(ERROR_INVALID_STATE);
        if(outcome.result.result!=L4_REMOTE_OUTCOME_RECOVERY_REQUIRED &&
            (!s->proof_sequence || outcome.proof_sequence!=s->proof_sequence || memcmp(outcome.proof_sha256,s->proof_hash,32) ||
             s->proof_kind!=(outcome.result.result==L4_REMOTE_OUTCOME_SUCCESS?102u:108u) ||
             outcome.result.finished_at<s->proof_finished))return fail(ERROR_REVISION_MISMATCH);
        s->status.has_outcome=true;s->status.outcome=outcome;s->terminal=true;ok=true;
        if(outcome.result.result==L4_REMOTE_OUTCOME_RECOVERY_REQUIRED)s->status.recorded_phase=L4_REMOTE_RECORDED_RECOVERY_REQUIRED;
    }else return fail(ERROR_INVALID_DATA);
    return ok;
}
