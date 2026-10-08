#include "recovery_plan.h"
#include <bcrypt.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>
static bool bad(DWORD code){SetLastError(code);return false;}
static DWORD u32(const BYTE* p){DWORD v;memcpy(&v,p,4);return v;}
static ULONGLONG u64(const BYTE* p){ULONGLONG v;memcpy(&v,p,8);return v;}
static void w32(BYTE* p,DWORD v){memcpy(p,&v,4);}
static void w64(BYTE* p,ULONGLONG v){memcpy(p,&v,8);}
bool l4_recovery_hash(const void* bytes,DWORD size,BYTE result[32]){
    if((size && !bytes) || !result)return bad(ERROR_INVALID_PARAMETER);
    BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;
    bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0 &&
        BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)>=0 && BCryptHashData(hash,(BYTE*)bytes,size,0)>=0 && BCryptFinishHash(hash,result,32,0)>=0;
    if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok?true:bad(ERROR_INVALID_DATA);
}
static bool nonzero(const void* bytes,DWORD size){const BYTE* p=bytes;for(DWORD i=0;i<size;i++)if(p[i])return true;return false;}
static bool sid_range(const BYTE* sd,DWORD size,DWORD offset){
    if(!offset || offset>size || size-offset<8)return false;const SID* sid=(const SID*)(sd+offset);
    return sid->Revision==SID_REVISION && sid->SubAuthorityCount<=SID_MAX_SUB_AUTHORITIES && size-offset>=8u+4u*sid->SubAuthorityCount;
}
static bool policy_aligned(const BYTE* sd,DWORD size){
    if(!sd || size<20 || size>4096)return false;
    SECURITY_DESCRIPTOR_RELATIVE header;memcpy(&header,sd,20);
    if(header.Revision!=SECURITY_DESCRIPTOR_REVISION || !(header.Control&SE_SELF_RELATIVE) || !(header.Control&SE_DACL_PRESENT) || header.Sacl || ((header.Owner|header.Group|header.Dacl)&3) ||
       !sid_range(sd,size,header.Owner) || !sid_range(sd,size,header.Group) || !header.Dacl || header.Dacl>size || size-header.Dacl<sizeof(ACL))return false;
    ACL acl;memcpy(&acl,sd+header.Dacl,sizeof(acl));
    if(acl.AclRevision!=ACL_REVISION || !acl.AceCount || acl.AclSize<sizeof(acl) || acl.AclSize>size-header.Dacl)return false;
    DWORD at=header.Dacl+sizeof(acl),end=header.Dacl+acl.AclSize;
    for(unsigned i=0;i<acl.AceCount;i++){
        if(at>end || end-at<sizeof(ACCESS_ALLOWED_ACE))return false;
        ACCESS_ALLOWED_ACE ace;memcpy(&ace,sd+at,sizeof(ace));
        if(ace.Header.AceType!=ACCESS_ALLOWED_ACE_TYPE || ace.Header.AceSize<sizeof(ace) || (ace.Header.AceSize&3) || ace.Header.AceSize>end-at ||
           !sid_range(sd,at+ace.Header.AceSize,at+8))return false;
        PSID sid=(PSID)(sd+at+8);
        if(!IsWellKnownSid(sid,WinLocalSystemSid) && !IsWellKnownSid(sid,WinBuiltinAdministratorsSid) &&
           (ace.Mask&~(FILE_GENERIC_READ|FILE_GENERIC_EXECUTE|GENERIC_READ|GENERIC_EXECUTE)))return false;
        at+=ace.Header.AceSize;
    }
    PSID owner=(PSID)(sd+header.Owner);
    return (IsWellKnownSid(owner,WinLocalSystemSid) || IsWellKnownSid(owner,WinBuiltinAdministratorsSid)) &&
        IsValidSecurityDescriptor((PSECURITY_DESCRIPTOR)sd) && GetSecurityDescriptorLength((PSECURITY_DESCRIPTOR)sd)==size;
}
static bool policy(const BYTE* sd,DWORD size){
    if(!sd || size>4096)return false;union{DWORD alignment;BYTE bytes[4096];} copy;
    memcpy(copy.bytes,sd,size);return policy_aligned(copy.bytes,size);
}
static bool command(const L4Layout* roots,const wchar_t* text,const wchar_t** suffix,wchar_t image[MAX_PATH]){
    if(!wmemchr(text,0,2048) || text[0]!=L'"')return false;const wchar_t* end=wcschr(text+1,L'"');
    if(!end || (end[1] && end[1]!=L' ' && end[1]!=L'\t') || end-text-1>=MAX_PATH)return false;
    wcsncpy_s(image,MAX_PATH,text+1,(size_t)(end-text-1));wchar_t prefix[MAX_PATH];
    if(swprintf_s(prefix,MAX_PATH,L"%ls\\releases\\",roots->binaries)<0)return false;
    size_t n=wcslen(prefix);if(wcsncmp(image,prefix,n))return false;const wchar_t* slash=wcschr(image+n,L'\\');
    if(!slash || slash-image-n>=32)return false;wchar_t version[32],expected[MAX_PATH];L4Layout layout;
    wcsncpy_s(version,32,image+n,(size_t)(slash-image-n));
    if(!l4_layout_from_roots(&layout,roots->binaries,roots->data,version) || !l4_layout_component(&layout,L"l4superv",L"l4superv.exe",expected) || wcscmp(image,expected))return false;
    *suffix=end+1;return true;
}
static bool valid(const L4Layout* roots,const L4RecoveryPlan* p){
    if(!roots || !p || !nonzero(&p->operation,16) || !p->sequence || !p->worker_pid || !p->supervisor_pid ||
       (!p->supervisor_created.dwLowDateTime && !p->supervisor_created.dwHighDateTime) ||
       (!p->worker_created.dwLowDateTime && !p->worker_created.dwHighDateTime) || !p->armed_utc || p->deadline_utc<=p->armed_utc ||
       !p->recovery_ms || p->recovery_ms>300000 || (p->start_type!=SERVICE_AUTO_START && p->start_type!=SERVICE_DEMAND_START) ||
       !p->old_size || !p->new_size || !nonzero(p->old_sha256,32) || !nonzero(p->new_sha256,32) ||
       p->old_config_size>L4_RECOVERY_CONFIG_LIMIT || p->new_config_size>L4_RECOVERY_CONFIG_LIMIT || (!p->old_exists && p->old_config_size) ||
       (p->old_config_size && !p->old_config) || (p->new_config_size && !p->new_config) || !policy(p->config_sd,p->config_sd_size))return false;
    wchar_t old[MAX_PATH],candidate[MAX_PATH];const wchar_t *a,*b;
    return command(roots,p->before,&a,old) && command(roots,p->after,&b,candidate) && wcscmp(old,candidate) && !wcscmp(a,b);
}
bool l4_recovery_encode(const L4Layout* roots,const L4RecoveryPlan* p,BYTE** result,DWORD* size){
    if(!result || !size)return bad(ERROR_INVALID_PARAMETER);*result=NULL;*size=0;if(!valid(roots,p))return bad(ERROR_INVALID_DATA);
    DWORD old=(DWORD)wcslen(p->before)*2,newer=(DWORD)wcslen(p->after)*2;
    DWORD length=224+old+newer+p->old_config_size+p->new_config_size+p->config_sd_size;
    BYTE* b=calloc(1,length);if(!b)return bad(ERROR_NOT_ENOUGH_MEMORY);
    memcpy(b,"L4RBK01",8);w32(b+8,length);w32(b+12,p->old_exists?1:0);memcpy(b+16,&p->operation,16);w64(b+32,p->sequence);
    w32(b+40,p->worker_pid);w32(b+44,p->recovery_ms);memcpy(b+48,&p->worker_created,8);w64(b+56,p->armed_utc);w64(b+64,p->deadline_utc);
    w64(b+72,p->old_size);w64(b+80,p->new_size);w32(b+88,p->old_config_size);w32(b+92,p->new_config_size);w32(b+96,p->config_sd_size);
    w32(b+100,p->start_type);w32(b+104,old);w32(b+108,newer);w32(b+112,p->supervisor_pid);memcpy(b+116,&p->supervisor_created,8);
    memcpy(b+128,p->old_sha256,32);memcpy(b+160,p->new_sha256,32);
    DWORD at=192;memcpy(b+at,p->before,old);at+=old;memcpy(b+at,p->after,newer);at+=newer;
    if(p->old_config_size)memcpy(b+at,p->old_config,p->old_config_size);at+=p->old_config_size;
    if(p->new_config_size)memcpy(b+at,p->new_config,p->new_config_size);at+=p->new_config_size;memcpy(b+at,p->config_sd,p->config_sd_size);
    if(!l4_recovery_hash(b,length-32,b+length-32)){free(b);return false;}*result=b;*size=length;return true;
}
bool l4_recovery_decode(const L4Layout* roots,const BYTE* b,DWORD size,L4RecoveryPlan* result){
    if(!result)return bad(ERROR_INVALID_PARAMETER);memset(result,0,sizeof(*result));
    if(!roots || !b || size<224 || size>L4_RECOVERY_PLAN_LIMIT || memcmp(b,"L4RBK01",8) || u32(b+8)!=size || u32(b+12)>1 || u32(b+124))return bad(ERROR_INVALID_DATA);
    DWORD old=u32(b+104),newer=u32(b+108),a=u32(b+88),c=u32(b+92),sd=u32(b+96);BYTE hash[32];
    if(!old || old>=4096 || !newer || newer>=4096 || (old&1) || (newer&1) || a>L4_RECOVERY_CONFIG_LIMIT || c>L4_RECOVERY_CONFIG_LIMIT ||
       !sd || sd>4096 || (ULONGLONG)224+old+newer+a+c+sd!=size || !l4_recovery_hash(b,size-32,hash) || memcmp(hash,b+size-32,32))return bad(ERROR_INVALID_DATA);
    L4RecoveryPlan p={0};memcpy(&p.operation,b+16,16);p.sequence=u64(b+32);p.old_exists=u32(b+12)!=0;p.worker_pid=u32(b+40);p.recovery_ms=u32(b+44);
    memcpy(&p.worker_created,b+48,8);p.armed_utc=u64(b+56);p.deadline_utc=u64(b+64);p.old_size=u64(b+72);p.new_size=u64(b+80);
    p.old_config_size=a;p.new_config_size=c;p.config_sd_size=sd;p.start_type=u32(b+100);p.supervisor_pid=u32(b+112);memcpy(&p.supervisor_created,b+116,8);
    memcpy(p.old_sha256,b+128,32);memcpy(p.new_sha256,b+160,32);
    DWORD at=192;memcpy(p.before,b+at,old);at+=old;memcpy(p.after,b+at,newer);at+=newer;
    if(wmemchr(p.before,0,old/2) || wmemchr(p.after,0,newer/2))return bad(ERROR_INVALID_DATA);
    p.old_config=b+at;at+=a;p.new_config=b+at;at+=c;p.config_sd=b+at;
    if(!valid(roots,&p))return bad(ERROR_INVALID_DATA);*result=p;return true;
}
static bool status_valid(L4RecoveryStatus status,DWORD error){return status>=1 && status<=4 && (status==L4_RECOVERY_FAILED?error!=0:error==0);}
bool l4_recovery_result_encode(const L4RecoveryPlan* p,const BYTE hash[32],L4RecoveryStatus status,DWORD error,BYTE bytes[96]){
    if(!p || !hash || !bytes || !status_valid(status,error))return bad(ERROR_INVALID_PARAMETER);
    memset(bytes,0,96);memcpy(bytes,"L4RBS01",8);memcpy(bytes+8,&p->operation,16);memcpy(bytes+24,hash,32);w32(bytes+56,status);w32(bytes+60,error);
    return l4_recovery_hash(bytes,64,bytes+64);
}
bool l4_recovery_result_decode(const L4RecoveryPlan* p,const BYTE hash[32],const BYTE* bytes,DWORD size,L4RecoveryStatus* status,DWORD* error){
    BYTE actual[32];if(!p || !hash || !bytes || !status || !error || size!=96 || memcmp(bytes,"L4RBS01",8) || memcmp(bytes+8,&p->operation,16) ||
       memcmp(bytes+24,hash,32) || !l4_recovery_hash(bytes,64,actual) || memcmp(actual,bytes+64,32) || !status_valid((L4RecoveryStatus)u32(bytes+56),u32(bytes+60)))return bad(ERROR_INVALID_DATA);
    *status=(L4RecoveryStatus)u32(bytes+56);*error=u32(bytes+60);return true;
}
bool l4_recovery_decide(const L4RecoveryPlan* p,L4RecoveryStatus status,ULONGLONG now,bool boot,L4RecoveryAction* action){
    if(!p || !action || (unsigned)status>4)return bad(ERROR_INVALID_PARAMETER);
    if(status==L4_RECOVERY_COMMITTED || status==L4_RECOVERY_RESTORED){*action=L4_RECOVERY_DONE;return true;}
    if(status==L4_RECOVERY_FAILED){*action=L4_RECOVERY_BLOCKED;return true;}
    if(status==L4_RECOVERY_STARTED){*action=L4_RECOVERY_REQUIRED;return true;}
    /* Protected boot recovery cannot depend on a clock restored backwards. */
    if(boot){*action=L4_RECOVERY_REQUIRED;return true;}
    if(now<p->armed_utc)return bad(ERROR_TIME_SKEW);
    *action=now>=p->deadline_utc?L4_RECOVERY_REQUIRED:L4_RECOVERY_WAIT;return true;
}
