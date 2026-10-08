#include "journal_internal.h"
#include "bootstrap.h"
#include "switch_decode.h"
#include <stdlib.h>
#include <string.h>
typedef struct{BYTE* bytes;DWORD size,at;bool ok;} Codec;
static void number(Codec* c,ULONGLONG value,unsigned width){if(c->at+width>c->size){c->ok=false;return;}if(width==4)l4_store_u32(c->bytes+c->at,(DWORD)value);else l4_store_u64(c->bytes+c->at,value);c->at+=width;}
static ULONGLONG read_number(Codec* c,unsigned width){if(c->at+width>c->size){c->ok=false;return 0;}ULONGLONG n=width==4?l4_store_get32(c->bytes+c->at):l4_store_get64(c->bytes+c->at);c->at+=width;return n;}
static void string(Codec* c,const wchar_t* value){
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,NULL,0,NULL,NULL);
    if(n<1 || c->at+4+(DWORD)n>c->size){c->ok=false;return;}number(c,(DWORD)n-1,4);
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,(char*)c->bytes+c->at,n,NULL,NULL)){c->ok=false;return;}c->at+=(DWORD)n-1;
}
static void read_string(Codec* c,wchar_t* value,int capacity){DWORD n=(DWORD)read_number(c,4);if(!c->ok || !n || n>8192 || n>c->size-c->at || memchr(c->bytes+c->at,0,n)){c->ok=false;return;}
    int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(char*)c->bytes+c->at,(int)n,value,capacity-1);if(!count){c->ok=false;return;}value[count]=0;c->at+=n;
}
bool l4_journal_save_switch(L4Journal* j,const L4ServiceSwitch* plan,ULONGLONG* sequence){
    if(!j || !plan)return l4_store_fail(ERROR_INVALID_PARAMETER);const wchar_t* version=wcsrchr(plan->layout.release,L'\\');L4Layout expected;
    if(!version || !l4_layout_from_roots(&expected,plan->layout.binaries,plan->layout.data,version+1) || memcmp(&expected,&plan->layout,sizeof(expected)) ||
        _wcsicmp(j->layout.binaries,plan->layout.binaries)!=0 || _wcsicmp(j->layout.data,plan->layout.data)!=0 ||
        !plan->before.installed || !plan->service[0] || !plan->before.account[0])return l4_store_fail(ERROR_INVALID_DATA);
    BYTE* bytes=(BYTE*)calloc(1,16384);if(!bytes)return l4_store_fail(ERROR_NOT_ENOUGH_MEMORY);Codec c={bytes,16384,0,true};
    number(&c,1,4);string(&c,plan->layout.binaries);string(&c,plan->layout.data);string(&c,version+1);string(&c,plan->service);
    string(&c,plan->before.account);string(&c,plan->before.image_path);string(&c,plan->after);number(&c,plan->before.start_type,4);
    number(&c,plan->size,8);number(&c,plan->before_size,8);
    if(c.at+64>c.size)c.ok=false;else{memcpy(bytes+c.at,plan->sha256,32);memcpy(bytes+c.at+32,plan->before_sha256,32);c.at+=64;}
    bool ok=c.ok && l4_journal_append(j,L4_RECORD_SWITCH_PLAN,bytes,c.at,sequence);free(bytes);return ok?true:l4_store_fail(ERROR_INVALID_DATA);
}
bool l4_switch_decode(const L4Layout* roots,const BYTE* bytes,DWORD size,L4ServiceSwitch* plan){
    return l4_switch_decode_bytes(roots,bytes,size,plan);
}
bool l4_journal_load_switch(L4Journal* j,ULONGLONG sequence,L4ServiceSwitch* plan){
    if(!j || !plan)return l4_store_fail(ERROR_INVALID_PARAMETER);BYTE* bytes=NULL;DWORD size;
    if(!l4_store_find_record(j,L4_RECORD_SWITCH_PLAN,sequence,&bytes,&size))return false;
    bool ok=l4_switch_decode(&j->layout,bytes,size,plan);DWORD error=GetLastError();free(bytes);
    return ok?true:l4_store_fail(error);
}


bool l4_bootstrap_save(L4Journal* j,const L4BootstrapPlan* plan,ULONGLONG* sequence){
    if(!j || !l4_bootstrap_validate(plan) || _wcsicmp(plan->layout.binaries,j->layout.binaries) || _wcsicmp(plan->layout.data,j->layout.data))return l4_store_fail(ERROR_INVALID_DATA);
    for(unsigned i=0;i<L4_BOOTSTRAP_SERVICES;i++){L4ServiceInventory current={0};if(!l4_service_inventory(plan->services[i],&current))return false;if(current.installed)return l4_store_fail(ERROR_SERVICE_EXISTS);}
    BYTE* bytes=(BYTE*)calloc(1,49152);if(!bytes)return l4_store_fail(ERROR_NOT_ENOUGH_MEMORY);Codec c={bytes,49152,0,true};
    number(&c,1,4);string(&c,plan->layout.binaries);string(&c,plan->layout.data);string(&c,wcsrchr(plan->layout.release,L'\\')+1);
    for(unsigned i=0;i<L4_BOOTSTRAP_SERVICES;i++){
        string(&c,plan->services[i]);string(&c,plan->commands[i]);number(&c,plan->start_types[i],4);number(&c,plan->sizes[i],8);
        if(c.at+32>c.size)c.ok=false;else{memcpy(bytes+c.at,plan->sha256[i],32);c.at+=32;}
    }
    bool ok=c.ok && l4_journal_append(j,L4_RECORD_BOOTSTRAP_PLAN,bytes,c.at,sequence);DWORD code=GetLastError();free(bytes);return ok?true:l4_store_fail(code?code:ERROR_INVALID_DATA);
}
bool l4_bootstrap_load(L4Journal* j,ULONGLONG sequence,L4BootstrapPlan* plan){
    if(!j || !plan)return l4_store_fail(ERROR_INVALID_PARAMETER);BYTE* bytes=NULL;DWORD size;
    if(!l4_store_find_record(j,L4_RECORD_BOOTSTRAP_PLAN,sequence,&bytes,&size))return false;
    Codec c={bytes,size,0,true};L4BootstrapPlan result={0};wchar_t binaries[MAX_PATH],data[MAX_PATH],version[32];
    if(read_number(&c,4)!=1)c.ok=false;
    read_string(&c,binaries,MAX_PATH);read_string(&c,data,MAX_PATH);read_string(&c,version,32);
    for(unsigned i=0;i<L4_BOOTSTRAP_SERVICES;i++){
        read_string(&c,result.services[i],32);read_string(&c,result.commands[i],2048);result.start_types[i]=(DWORD)read_number(&c,4);result.sizes[i]=read_number(&c,8);
        if(!c.ok || c.at+32>size)c.ok=false;else{memcpy(result.sha256[i],bytes+c.at,32);c.at+=32;}
    }
    bool ok=c.ok && c.at==size && l4_layout_from_roots(&result.layout,binaries,data,version) && !_wcsicmp(result.layout.data,j->layout.data) &&
        !_wcsicmp(result.layout.binaries,j->layout.binaries) && l4_bootstrap_validate(&result);
    free(bytes);if(!ok)return l4_store_fail(ERROR_INVALID_DATA);*plan=result;return true;
}
