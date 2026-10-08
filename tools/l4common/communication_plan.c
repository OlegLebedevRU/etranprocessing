#include "communication_plan.h"
#include "journal_internal.h"
#include "switch_decode.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>
static bool fail(DWORD code){SetLastError(code);return false;}
static bool nonzero(const void* bytes,DWORD size){const BYTE* p=bytes;for(DWORD i=0;i<size;i++)if(p[i])return true;return false;}
/* Decode record20 with fixed broker path and bounded self-relative SD. Validate
 * all offsets BEFORE Win32 SID/ACL APIs; copy SD for required DWORD alignment. */
static bool config(const BYTE* b,DWORD size,const char* path){
    if(!b || size<24 || l4_store_get32(b)!=1 || l4_store_get32(b+4)!=1)return false;
    DWORD n=l4_store_get32(b+8),old=l4_store_get32(b+12),next=l4_store_get32(b+16),sd_size=l4_store_get32(b+20);
    if(n!=strlen(path) || old>65536 || next>65536 || sd_size<20 || sd_size>4096 ||
       24ull+n+old+next+sd_size!=size || memcmp(b+24,path,n))return false;
    union{DWORD aligned;BYTE bytes[4096];} sd;memcpy(sd.bytes,b+24+n+old+next,sd_size);
    SECURITY_DESCRIPTOR_RELATIVE h;memcpy(&h,sd.bytes,20);
    if(h.Revision!=1 || !(h.Control&SE_SELF_RELATIVE) || !(h.Control&SE_DACL_PRESENT) || h.Sacl || !h.Owner || !h.Dacl)return false;
    DWORD offsets[]={h.Owner,h.Group};for(unsigned i=0;i<2;i++){DWORD at=offsets[i];if(!at && i==1)continue;
        if((at&3) || at<20 || at>sd_size || sd_size-at<8 || sd.bytes[at]!=SID_REVISION || sd.bytes[at+1]>SID_MAX_SUB_AUTHORITIES ||
           8u+4u*sd.bytes[at+1]>sd_size-at || !IsValidSid((PSID)(sd.bytes+at)))return false;}
    DWORD at=h.Dacl;if((at&3) || at<20 || at>sd_size || sd_size-at<sizeof(ACL))return false;
    ACL acl;memcpy(&acl,sd.bytes+at,sizeof(acl));if(acl.AclSize<sizeof(ACL) || acl.AclSize>sd_size-at || !acl.AceCount)return false;
    DWORD offset=sizeof(ACL);for(unsigned i=0;i<acl.AceCount;i++){
        if(offset>acl.AclSize || acl.AclSize-offset<16)return false;ACCESS_ALLOWED_ACE ace;memcpy(&ace,sd.bytes+at+offset,sizeof(ace));
        if(ace.Header.AceType!=ACCESS_ALLOWED_ACE_TYPE || (ace.Header.AceSize&3) || ace.Header.AceSize<16 || ace.Header.AceSize>acl.AclSize-offset)return false;
        PSID sid=(PSID)(sd.bytes+at+offset+8);BYTE* sb=sid;
        if(sb[0]!=SID_REVISION || sb[1]>SID_MAX_SUB_AUTHORITIES || 16u+4u*sb[1]>ace.Header.AceSize || !IsValidSid(sid))return false;
        if(!IsWellKnownSid(sid,WinLocalSystemSid) && !IsWellKnownSid(sid,WinBuiltinAdministratorsSid) &&
           (ace.Mask&~(FILE_GENERIC_READ|FILE_GENERIC_EXECUTE|GENERIC_READ|GENERIC_EXECUTE)))return false;
        offset+=ace.Header.AceSize;
    }
    PSID owner=(PSID)(sd.bytes+h.Owner);
    return (IsWellKnownSid(owner,WinLocalSystemSid) || IsWellKnownSid(owner,WinBuiltinAdministratorsSid)) &&
        IsValidSecurityDescriptor(sd.bytes) && GetSecurityDescriptorLength(sd.bytes)==sd_size;
}
static bool command(const L4Layout* roots,const wchar_t* text,unsigned service,const wchar_t** suffix){
    const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con"};const wchar_t* exes[]={L"leo4proxy.exe",L"mosquitto.exe",L"l4con.exe"};
    if(!wmemchr(text,0,2048) || text[0]!=L'"')return false;const wchar_t* end=wcschr(text+1,L'"');
    if(!end || end-text-1>=MAX_PATH || (end[1] && end[1]!=L' ' && end[1]!=L'\t'))return false;
    wchar_t image[MAX_PATH],prefix[MAX_PATH],version[32],expected[MAX_PATH];
    wcsncpy_s(image,MAX_PATH,text+1,(size_t)(end-text-1));swprintf_s(prefix,MAX_PATH,L"%ls\\releases\\",roots->binaries);
    size_t n=wcslen(prefix);if(wcsncmp(image,prefix,n))return false;const wchar_t* slash=wcschr(image+n,L'\\');
    if(!slash || slash-image-n>=32)return false;wcsncpy_s(version,32,image+n,(size_t)(slash-image-n));L4Layout layout;
    if(!l4_layout_from_roots(&layout,roots->binaries,roots->data,version) ||
       !l4_layout_component(&layout,components[service],exes[service],expected) || wcscmp(image,expected))return false;
    *suffix=end+1;return true;
}
static bool valid(const L4Layout* roots,const L4CommunicationPlan* p){
    if(!roots || !p || !nonzero(&p->operation,16) || !p->sequence || !p->generation || !p->armed_utc || p->deadline_utc<=p->armed_utc ||
       !p->worker_pid || !p->supervisor_pid || p->worker_pid==p->supervisor_pid || !nonzero(&p->worker_created,8) || !nonzero(&p->supervisor_created,8) ||
       !nonzero(p->operation_sha256,32) || !l4_communication_budget_valid(&p->budget))return false;
    ULONGLONG refs[5]={p->switch_sequence[0],p->switch_sequence[1],p->config_sequence[0],p->config_sequence[1],p->con_sequence};
    for(unsigned i=0;i<5;i++){if(!refs[i] || refs[i]>=p->sequence)return false;for(unsigned k=0;k<i;k++)if(refs[k]==refs[i])return false;}
    const wchar_t* services[]={L"Leo4Proxy",L"mosquitto"};
    for(unsigned i=0;i<2;i++){L4ServiceSwitch s;const wchar_t *old,*next;
        if(!p->switches[i] || !p->switch_size[i] || p->switch_size[i]>16384 || !l4_switch_decode_bytes(roots,p->switches[i],p->switch_size[i],&s) ||
           wcscmp(s.service,services[i]) || wcscmp(s.before.account,L"LocalSystem") ||
           (s.before.start_type!=SERVICE_AUTO_START && s.before.start_type!=SERVICE_DEMAND_START) ||
           !s.before_size || !s.size || !nonzero(s.before_sha256,32) || !nonzero(s.sha256,32) ||
           !command(roots,s.before.image_path,i,&old) || !command(roots,s.after,i,&next) || wcscmp(old,next))return false;
    }
    L4ServiceSwitch con;const wchar_t *old_con,*new_con;
    if(!p->con_pid || !nonzero(&p->con_created,8) || !memchr(p->thumbprint,0,64) || strlen(p->thumbprint)!=40 ||
       !p->con_switch || !p->con_size || p->con_size>16384 || !l4_switch_decode_bytes(roots,p->con_switch,p->con_size,&con) ||
       wcscmp(con.service,L"L4Con") || wcscmp(con.before.account,L"LocalSystem") ||
       (con.before.start_type!=SERVICE_AUTO_START && con.before.start_type!=SERVICE_DEMAND_START) ||
       !con.before_size || !con.size || !nonzero(con.before_sha256,32) || !nonzero(con.sha256,32) ||
       !command(roots,con.before.image_path,2,&old_con) || !command(roots,con.after,2,&new_con) || wcscmp(old_con,new_con))return false;
    for(unsigned i=0;i<40;i++){char c=p->thumbprint[i];if(!((c>='0' && c<='9') || (c>='A' && c<='F') || (c>='a' && c<='f')))return false;}
    for(unsigned i=41;i<64;i++)if(p->thumbprint[i])return false;
    /* Every source signal belongs to the same original immutable release. */
    L4ServiceSwitch proxy;if(!l4_switch_decode_bytes(roots,p->switches[0],p->switch_size[0],&proxy) || wcscmp(proxy.layout.release,con.layout.release))return false;
    const wchar_t* a=wcschr(proxy.before.image_path+1,L'"');const wchar_t* b=wcschr(con.before.image_path+1,L'"');
    size_t na=(size_t)(a-proxy.before.image_path-1)-wcslen(L"\\leo4proxy\\leo4proxy.exe");
    size_t nb=(size_t)(b-con.before.image_path-1)-wcslen(L"\\l4con\\l4con.exe");
    if(na!=nb || wcsncmp(proxy.before.image_path+1,con.before.image_path+1,na))return false;
    return config(p->configs[0],p->config_size[0],"mosquitto\\mosquitto.conf") && config(p->configs[1],p->config_size[1],"mosquitto\\acl.conf");
}
bool l4_communication_plan_encode(const L4Layout* roots,const L4CommunicationPlan* p,BYTE** bytes,DWORD* size){
    if(!bytes || !size)return fail(ERROR_INVALID_PARAMETER);*bytes=NULL;*size=0;if(!valid(roots,p))return fail(ERROR_INVALID_DATA);
    ULONGLONG total=352+p->con_size;for(unsigned i=0;i<2;i++)total+=p->switch_size[i]+p->config_size[i];
    if(total>L4_COMMUNICATION_PLAN_LIMIT)return fail(ERROR_FILE_TOO_LARGE);BYTE* b=calloc(1,(size_t)total);if(!b)return fail(ERROR_NOT_ENOUGH_MEMORY);
    memcpy(b,"L4COM02",8);l4_store_u32(b+8,(DWORD)total);memcpy(b+16,&p->operation,16);
    l4_store_u64(b+32,p->sequence);l4_store_u64(b+40,p->generation);l4_store_u64(b+48,p->armed_utc);l4_store_u64(b+56,p->deadline_utc);
    l4_store_u32(b+64,p->worker_pid);l4_store_u32(b+68,p->supervisor_pid);memcpy(b+72,&p->worker_created,8);memcpy(b+80,&p->supervisor_created,8);
    memcpy(b+88,p->operation_sha256,32);const DWORD values[]={p->budget.verify_ms,p->budget.worker_ms,p->budget.lock_ms,p->budget.proxy_ms,p->budget.broker_prepare_ms,p->budget.mosquitto_ms,p->budget.channel_ms,p->budget.total_ms};
    for(unsigned i=0;i<8;i++)l4_store_u32(b+120+i*4,values[i]);DWORD at=320;
    for(unsigned i=0;i<2;i++){l4_store_u64(b+152+i*8,p->switch_sequence[i]);l4_store_u64(b+168+i*8,p->config_sequence[i]);
        l4_store_u32(b+184+i*4,p->switch_size[i]);l4_store_u32(b+192+i*4,p->config_size[i]);
        memcpy(b+at,p->switches[i],p->switch_size[i]);at+=p->switch_size[i];memcpy(b+at,p->configs[i],p->config_size[i]);at+=p->config_size[i];}
    l4_store_u64(b+200,p->con_sequence);l4_store_u32(b+208,p->con_size);l4_store_u32(b+212,p->con_pid);
    memcpy(b+216,&p->con_created,8);memcpy(b+224,p->thumbprint,64);memcpy(b+at,p->con_switch,p->con_size);at+=p->con_size;
    bool ok=l4_store_hash(b,at,NULL,0,b+at);if(!ok){free(b);return false;}*bytes=b;*size=(DWORD)total;return true;
}
bool l4_communication_plan_decode(const L4Layout* roots,const BYTE* b,DWORD size,L4CommunicationPlan* result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);memset(result,0,sizeof(*result));BYTE digest[32];
    if(!roots || !b || size<352 || size>L4_COMMUNICATION_PLAN_LIMIT || memcmp(b,"L4COM02",8) || l4_store_get32(b+8)!=size ||
       l4_store_get32(b+12) || nonzero(b+288,32) || !l4_store_hash(b,size-32,NULL,0,digest) || memcmp(digest,b+size-32,32))return fail(ERROR_INVALID_DATA);
    L4CommunicationPlan p={0};memcpy(&p.operation,b+16,16);p.sequence=l4_store_get64(b+32);p.generation=l4_store_get64(b+40);p.armed_utc=l4_store_get64(b+48);p.deadline_utc=l4_store_get64(b+56);
    p.worker_pid=l4_store_get32(b+64);p.supervisor_pid=l4_store_get32(b+68);memcpy(&p.worker_created,b+72,8);memcpy(&p.supervisor_created,b+80,8);memcpy(p.operation_sha256,b+88,32);
    DWORD* fields[]={&p.budget.verify_ms,&p.budget.worker_ms,&p.budget.lock_ms,&p.budget.proxy_ms,&p.budget.broker_prepare_ms,&p.budget.mosquitto_ms,&p.budget.channel_ms,&p.budget.total_ms};
    for(unsigned i=0;i<8;i++)*fields[i]=l4_store_get32(b+120+i*4);DWORD at=320;
    for(unsigned i=0;i<2;i++){p.switch_sequence[i]=l4_store_get64(b+152+i*8);p.config_sequence[i]=l4_store_get64(b+168+i*8);p.switch_size[i]=l4_store_get32(b+184+i*4);p.config_size[i]=l4_store_get32(b+192+i*4);
        if(p.switch_size[i]>size-32-at)return fail(ERROR_INVALID_DATA);p.switches[i]=b+at;at+=p.switch_size[i];
        if(p.config_size[i]>size-32-at)return fail(ERROR_INVALID_DATA);p.configs[i]=b+at;at+=p.config_size[i];}
    p.con_sequence=l4_store_get64(b+200);p.con_size=l4_store_get32(b+208);p.con_pid=l4_store_get32(b+212);
    memcpy(&p.con_created,b+216,8);memcpy(p.thumbprint,b+224,64);
    if(p.con_size>size-32-at)return fail(ERROR_INVALID_DATA);p.con_switch=b+at;at+=p.con_size;
    if(at!=size-32 || !valid(roots,&p))return fail(ERROR_INVALID_DATA);*result=p;return true;
}
