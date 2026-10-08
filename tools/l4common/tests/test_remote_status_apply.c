/* Actual private journal/snapshot/record10 refs plus modeled worker admission,
 * action/proof producers. This checks observation, not native completion E2E. */
#define wmain original_status_main
#include "test_remote_status.c"
#undef wmain
#include "../remote_status.c"
static void state_bytes(const Scan* s,DWORD window,ULONGLONG generation,BYTE b[112]){
    memset(b,0,112);memcpy(b,"L4UPD01",8);memcpy(b+8,s->status.operation_id,37);l4_store_u32(b+48,window);
    l4_store_u64(b+56,generation);l4_store_u64(b+64,s->status.plan_sequence);l4_store_u64(b+72,window?s->communication_deadline+(window==2?100000000ull:0):0);
    CHECK(l4_store_hash(b,80,NULL,0,b+80));
}
static void action_bytes(const Scan* s,DWORD action,DWORD service,DWORD phase,ULONGLONG intent,BYTE b[112]){
    memset(b,0,112);memcpy(b,"L4SCA001",8);l4_store_u32(b+8,1);l4_store_u32(b+12,action);l4_store_u32(b+16,service);
    l4_store_u32(b+20,s->active.window);l4_store_u64(b+24,s->active.generation);l4_store_u64(b+32,s->active.deadline_utc);
    l4_store_u64(b+40,s->status.plan_sequence);l4_store_u64(b+48,s->switches[0][service]);l4_store_u32(b+56,200+service);
    l4_store_u64(b+64,300+service);l4_store_u64(b+72,intent);l4_store_u32(b+80,phase);
}
static void codec_tests(const Scan* s){
    L4RemoteOutcome p={0},out;p.result=s->status.result;p.result.target=1;p.result.result=L4_REMOTE_OUTCOME_SUCCESS;
    strcpy_s(p.result.operation_id,37,s->status.operation_id);strcpy_s(p.result.requested_version,32,"latest");strcpy_s(p.result.previous_version,32,"1.13.6");strcpy_s(p.result.resolved_version,32,"1.13.7");
    p.result.started_at=s->status.request.accepted_utc;p.result.finished_at=p.result.started_at+100;
    p.plan_sequence=s->status.plan_sequence;p.proof_sequence=p.plan_sequence+1;memcpy(p.plan_sha256,s->operation_hash,32);memset(p.proof_sha256,2,32);
    BYTE b[280];CHECK(l4_remote_outcome_encode(&p,b));CHECK(l4_remote_outcome_decode(b,sizeof(b),&out)&&out.result.result==1);
    const unsigned offsets[]={8,76,77,78,79,176,177,199};for(unsigned i=0;i<_countof(offsets);i++){b[offsets[i]]^=1;CHECK(!l4_remote_outcome_decode(b,sizeof(b),&out));b[offsets[i]]^=1;}
    CHECK(!l4_remote_outcome_decode(b,279,&out));p.result.error=ERROR_ACCESS_DENIED;CHECK(!l4_remote_outcome_encode(&p,b));
    p.result.result=L4_REMOTE_OUTCOME_RESTORED;CHECK(l4_remote_outcome_encode(&p,b)&&l4_remote_outcome_decode(b,280,&out));
    p.result.error=ERROR_CANCELLED;CHECK(l4_remote_outcome_encode(&p,b)&&l4_remote_outcome_decode(b,280,&out));
    p.result.result=L4_REMOTE_OUTCOME_RECOVERY_REQUIRED;CHECK(!l4_remote_outcome_encode(&p,b));p.proof_sequence=0;memset(p.proof_sha256,0,32);
    CHECK(l4_remote_outcome_encode(&p,b)&&l4_remote_outcome_decode(b,280,&out));p.result.error=0;CHECK(!l4_remote_outcome_encode(&p,b));
}
int wmain(void){
    CHECK(original_status_main(0,NULL)==0);
    wchar_t temp[MAX_PATH],root[MAX_PATH],pf[MAX_PATH],pd[MAX_PATH],path[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));
    swprintf_s(root,MAX_PATH,L"%lsL4OutcomeReducer-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());CHECK(CreateDirectoryW(root,NULL));
    swprintf_s(pf,MAX_PATH,L"%ls\\PF",root);swprintf_s(pd,MAX_PATH,L"%ls\\PD",root);L4Layout layout;CHECK(l4_layout_from_roots(&layout,pf,pd,L"1.13.6")&&l4_layout_prepare(&layout));
    L4Journal* j=NULL;L4RemoteRequest request;CHECK(start(&layout,120,&j,&request));if(!j)return 1;CHECK(ack(j,&request,false));
    CHECK(l4_journal_append(j,60,"x",1,NULL));ULONGLONG packages,source,config[12],switches[4];CHECK(l4_journal_append(j,62,"x",1,&packages));CHECK(l4_journal_append(j,63,"x",1,&source));
    for(unsigned i=0;i<12;i++)CHECK(l4_journal_append(j,20,"x",1,&config[i]));
    const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};const wchar_t* services[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};
    L4ServiceSwitch sw[4];for(unsigned i=0;i<4;i++){memset(&sw[i],0,sizeof(sw[i]));CHECK(l4_layout_from_roots(&sw[i].layout,pf,pd,L"1.13.7"));
        wcscpy_s(sw[i].service,32,services[i]);sw[i].before.installed=true;sw[i].before.start_type=SERVICE_AUTO_START;wcscpy_s(sw[i].before.account,256,L"LocalSystem");
        wchar_t file[40],image[MAX_PATH];swprintf_s(file,40,L"%ls.exe",components[i]);CHECK(l4_layout_component(&layout,components[i],file,image));swprintf_s(sw[i].before.image_path,2048,L"\"%ls\" --service",image);
        CHECK(l4_layout_component(&sw[i].layout,components[i],file,image));swprintf_s(sw[i].after,2048,L"\"%ls\" --service",image);sw[i].size=2;sw[i].before_size=1;memset(sw[i].sha256,2,32);memset(sw[i].before_sha256,1,32);
        CHECK(l4_journal_save_switch(j,&sw[i],&switches[i]));}
    BYTE plan[156]={0};l4_store_u32(plan,1);l4_store_u64(plan+4,packages);l4_store_u64(plan+12,source);l4_store_u32(plan+20,1);l4_store_u32(plan+24,12);
    for(unsigned i=0;i<12;i++)l4_store_u64(plan+28+i*8,config[i]);for(unsigned i=0;i<4;i++)l4_store_u64(plan+124+i*8,switches[i]);ULONGLONG planseq;CHECK(l4_journal_append(j,64,plan,sizeof(plan),&planseq));
    GUID id;const wchar_t* operation=wcsrchr(j->directory,L'\\')+1;L4JournalReader* reader=NULL;CHECK(uuid(operation,&id)&&snapshot(&layout,operation,&id,INVALID_HANDLE_VALUE,true,&reader));
    Scan s={0};s.roots=&layout;s.original=layout;s.reader=reader;s.status.request=request;s.status.packages_sequence=packages;s.status.plan_sequence=planseq;
    CHECK(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,operation,-1,s.status.operation_id,37,NULL,NULL));CHECK(l4_store_hash(plan,sizeof(plan),NULL,0,s.operation_hash));CHECK(apply_plan(&s,planseq,plan,sizeof(plan))&&s.typed_plan);
    s.receipt=true;s.communication_generation=1;s.communication_deadline=request.accepted_utc+600000000ull;s.recovery.armed_utc=request.accepted_utc;s.recovery.deadline_utc=request.accepted_utc+1200000000ull;s.recovery.worker_pid=100;s.recovery.worker_created.dwLowDateTime=1;
    codec_tests(&s);BYTE state[112];state_bytes(&s,1,1,state);CHECK(apply_visit(&s,65,planseq+1,state,112));Scan active=s;
    CHECK(!apply_visit(&s,65,planseq+2,state,112));s=active;ULONGLONG seq=planseq+2;
    for(unsigned service=0;service<4;service++){
        if(service==2){state_bytes(&s,2,2,state);CHECK(apply_visit(&s,65,seq++,state,112));}
        for(unsigned action=1;action<=3;action++){BYTE b[112];action_bytes(&s,action,service,0,0,b);CHECK(apply_visit(&s,100,seq,b,112));ULONGLONG intent=seq++;
            l4_store_u64(b+72,intent);CHECK(apply_visit(&s,101,seq++,b,112));}
        unsigned begin=service==0?1:service==2?0:12,end=service==0?3:service==2?12:12;
        for(unsigned i=begin;i<end;i++){if(service==2 && (i==1 || i==2))continue;BYTE b[12];l4_store_u64(b,config[i]);l4_store_u32(b+8,1);CHECK(apply_visit(&s,21,seq++,b,12));CHECK(apply_visit(&s,22,seq++,b,12));}
    }
    Scan full=s;BYTE commit[16959]={0};memcpy(commit,"L4SCMT01",8);l4_store_u64(commit+8,planseq);memcpy(commit+16,s.operation_hash,32);memcpy(commit+48,s.status.operation_id,37);
    strcpy_s((char*)commit+85,37,"17730000-0000-4000-8000-000000000121");strcpy_s((char*)commit+122,37,"17730000-0000-4000-8000-000000000121");
    strcpy_s((char*)commit+159,32,"1.13.7");strcpy_s((char*)commit+191,32,"1.13.6");strcpy_s((char*)commit+223,8,"x86");memset(commit+231,1,64);strcpy_s((char*)commit+295,40,"l4setup.exe");l4_store_u64(commit+335,1);memset(commit+343,1,64);l4_store_u64(commit+407,request.accepted_utc+100);
    for(unsigned i=0;i<12;i++)l4_store_u64(commit+415+i*8,config[i]);for(unsigned i=0;i<4;i++){BYTE* b=commit+511+i*4112;l4_store_u32(b,SERVICE_AUTO_START);l4_store_u32(b+4,s.pids[i]);l4_store_u64(b+8,s.births[i]);memcpy(b+16,sw[i].after,wcslen(sw[i].after)*2);}
    CHECK(apply_visit(&s,102,seq,commit,sizeof(commit)));ULONGLONG proof=seq++;CHECK(!apply_visit(&s,102,seq,commit,sizeof(commit)));
    L4RemoteOutcome outcome={0};outcome.result.target=1;outcome.result.result=L4_REMOTE_OUTCOME_SUCCESS;outcome.result.started_at=request.accepted_utc;outcome.result.finished_at=request.accepted_utc+200;
    strcpy_s(outcome.result.operation_id,37,s.status.operation_id);strcpy_s(outcome.result.requested_version,32,"latest");strcpy_s(outcome.result.previous_version,32,"1.13.6");strcpy_s(outcome.result.resolved_version,32,"1.13.7");
    outcome.plan_sequence=planseq;memcpy(outcome.plan_sha256,s.operation_hash,32);outcome.proof_sequence=proof;CHECK(l4_store_hash(commit,sizeof(commit),NULL,0,outcome.proof_sha256));BYTE out[280];CHECK(l4_remote_outcome_encode(&outcome,out));
    CHECK(apply_visit(&s,103,seq++,out,280)&&s.status.has_outcome&&!s.status.outcome_cleared);state_bytes(&s,0,3,state);CHECK(apply_visit(&s,65,seq++,state,112)&&s.status.outcome_clear_recorded&&!s.status.outcome_cleared);
    Scan bad=full;CHECK(!apply_visit(&bad,103,seq,out,280));bad=full;commit[519]^=1;CHECK(!apply_visit(&bad,102,seq,commit,sizeof(commit)));commit[519]^=1;
    bad=full;outcome.result.result=L4_REMOTE_OUTCOME_RECOVERY_REQUIRED;outcome.result.error=ERROR_TIMEOUT;outcome.proof_sequence=0;memset(outcome.proof_sha256,0,32);CHECK(l4_remote_outcome_encode(&outcome,out));
    CHECK(apply_visit(&bad,103,seq++,out,280));state_bytes(&bad,0,3,state);CHECK(!apply_visit(&bad,65,seq,state,112));
    /* An abandoned forward intent is never a completed new process. */
    bad=active;BYTE action[112];action_bytes(&bad,1,0,0,0,action);ULONGLONG pending=seq++;CHECK(apply_visit(&bad,100,pending,action,112));
    state_bytes(&bad,2,2,state);CHECK(apply_visit(&bad,65,seq++,state,112));action_bytes(&bad,1,0,2,pending,action);
    CHECK(apply_visit(&bad,106,seq++,action,112)&&bad.restoring&&!bad.pending_kind&&!bad.forward[0]);
    /* Abandon closes only the exact pending config/worker/window tuple. */
    bad=active;BYTE configop[12];l4_store_u64(configop,config[1]);l4_store_u32(configop+8,1);pending=seq++;
    CHECK(apply_visit(&bad,21,pending,configop,12));BYTE abandon[112]={0};memcpy(abandon,"L4CFG001",8);l4_store_u32(abandon+8,1);l4_store_u32(abandon+12,1);
    l4_store_u32(abandon+20,1);l4_store_u64(abandon+24,1);l4_store_u64(abandon+32,bad.active.deadline_utc);l4_store_u64(abandon+40,planseq);l4_store_u64(abandon+48,config[1]);
    l4_store_u32(abandon+56,100);l4_store_u64(abandon+64,1);l4_store_u64(abandon+72,pending);l4_store_u32(abandon+80,1);
    Scan wrong=bad;abandon[64]=2;CHECK(!apply_visit(&wrong,107,seq,abandon,112));abandon[64]=1;CHECK(apply_visit(&bad,107,seq++,abandon,112)&&bad.restoring&&!bad.config_pending);
    /* Four105 records are progress, not terminal restoration without108. */
    bad=active;for(unsigned i=0;i<4;i++){action_bytes(&bad,4,i,3,0,action);pending=seq++;CHECK(apply_visit(&bad,104,pending,action,112));l4_store_u64(action+72,pending);CHECK(apply_visit(&bad,105,seq++,action,112));}
    CHECK(!bad.status.has_outcome);BYTE oldproof[384]={0};memcpy(oldproof,"L4ROLD01",8);l4_store_u32(oldproof+8,1);l4_store_u32(oldproof+12,1);l4_store_u64(oldproof+16,1);
    l4_store_u64(oldproof+24,bad.active.deadline_utc);l4_store_u64(oldproof+32,planseq);memcpy(oldproof+40,bad.operation_hash,32);memcpy(oldproof+72,bad.status.operation_id,37);
    strcpy_s((char*)oldproof+109,32,"1.13.6");strcpy_s((char*)oldproof+141,8,"x86");memset(oldproof+149,1,32);l4_store_u64(oldproof+181,request.accepted_utc+100);
    l4_store_u32(oldproof+189,100);l4_store_u64(oldproof+193,1);for(unsigned i=0;i<4;i++){l4_store_u32(oldproof+201+i*4,bad.old_pids[i]);l4_store_u64(oldproof+217+i*8,bad.old_births[i]);l4_store_u64(oldproof+345+i*8,bad.restore_done[i]);}
    for(unsigned i=0;i<12;i++)l4_store_u64(oldproof+249+i*8,config[i]);l4_store_u32(oldproof+377,1);wrong=bad;oldproof[377]=2;CHECK(!apply_visit(&wrong,108,seq,oldproof,384));oldproof[377]=1;
    proof=seq++;CHECK(apply_visit(&bad,108,proof,oldproof,384));outcome.result.result=L4_REMOTE_OUTCOME_RESTORED;outcome.result.error=ERROR_ACCESS_DENIED;outcome.proof_sequence=proof;CHECK(l4_store_hash(oldproof,384,NULL,0,outcome.proof_sha256));
    CHECK(l4_remote_outcome_encode(&outcome,out));CHECK(apply_visit(&bad,103,seq++,out,280));state_bytes(&bad,0,2,state);CHECK(apply_visit(&bad,65,seq++,state,112)&&bad.status.outcome_clear_recorded&&!bad.status.outcome_cleared);
    l4_journal_reader_close(reader);remove_journal(&j);
    swprintf_s(path,MAX_PATH,L"%ls\\deployment.lock",layout.operations);CHECK(DeleteFileW(path));CHECK(RemoveDirectoryW(layout.operations));CHECK(RemoveDirectoryW(layout.cache));CHECK(RemoveDirectoryW(layout.staging));
    swprintf_s(path,MAX_PATH,L"%ls\\update",layout.data);CHECK(RemoveDirectoryW(path));CHECK(RemoveDirectoryW(layout.config));CHECK(RemoveDirectoryW(layout.state));CHECK(RemoveDirectoryW(layout.logs));
    swprintf_s(path,MAX_PATH,L"%ls\\releases",layout.binaries);CHECK(RemoveDirectoryW(path));CHECK(RemoveDirectoryW(layout.launchers));CHECK(RemoveDirectoryW(layout.binaries));CHECK(RemoveDirectoryW(layout.data));CHECK(RemoveDirectoryW(root));
    printf("Remote outcome reducer: %u checks, %u failures; actual snapshot/switch codecs, apply/admission/proofs modeled; no SCM/channel/clear writes\n",checks,failures);return failures?1:0;
}
