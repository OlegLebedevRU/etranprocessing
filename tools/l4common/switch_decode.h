#pragma once
#include "journal_internal.h"
#include <string.h>
typedef struct{const BYTE* bytes;DWORD size,at;bool ok;} L4SwitchReader;
static inline ULONGLONG l4_switch_number(L4SwitchReader* c,unsigned width){
    if(c->at+width>c->size){c->ok=false;return 0;}ULONGLONG n=width==4?l4_store_get32(c->bytes+c->at):l4_store_get64(c->bytes+c->at);c->at+=width;return n;
}
static inline void l4_switch_string(L4SwitchReader* c,wchar_t* value,int capacity){
    DWORD n=(DWORD)l4_switch_number(c,4);if(!c->ok || !n || n>8192 || n>c->size-c->at || memchr(c->bytes+c->at,0,n)){c->ok=false;return;}
    int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(char*)c->bytes+c->at,(int)n,value,capacity-1);if(!count){c->ok=false;return;}value[count]=0;c->at+=n;
}
static inline bool l4_switch_decode_bytes(const L4Layout* roots,const BYTE* input,DWORD size,L4ServiceSwitch* plan){
    if(!roots || !input || !plan || size>16384)return l4_store_fail(ERROR_INVALID_PARAMETER);
    const BYTE* bytes=input;
    L4SwitchReader c={bytes,size,0,true};L4ServiceSwitch result={0};wchar_t binaries[MAX_PATH],data[MAX_PATH],version[32];
    if(l4_switch_number(&c,4)!=1)c.ok=false;
    l4_switch_string(&c,binaries,MAX_PATH);l4_switch_string(&c,data,MAX_PATH);l4_switch_string(&c,version,32);l4_switch_string(&c,result.service,32);
    l4_switch_string(&c,result.before.account,256);l4_switch_string(&c,result.before.image_path,2048);l4_switch_string(&c,result.after,2048);
    result.before.installed=true;result.before.start_type=(DWORD)l4_switch_number(&c,4);result.size=l4_switch_number(&c,8);result.before_size=l4_switch_number(&c,8);
    if(!c.ok || c.at+64!=size)c.ok=false;else{memcpy(result.sha256,bytes+c.at,32);memcpy(result.before_sha256,bytes+c.at+32,32);}
    bool ok=c.ok && l4_layout_from_roots(&result.layout,binaries,data,version) && !_wcsicmp(result.layout.data,roots->data) && !_wcsicmp(result.layout.binaries,roots->binaries);
    if(!ok)return l4_store_fail(ERROR_INVALID_DATA);*plan=result;return true;
}
