#include "remote_result.h"
#include <string.h>
static bool fail(void){SetLastError(ERROR_INVALID_DATA);return false;}
static void put32(BYTE* b,DWORD n){for(unsigned i=0;i<4;i++)b[i]=(BYTE)(n>>(8*i));}
static void put64(BYTE* b,ULONGLONG n){for(unsigned i=0;i<8;i++)b[i]=(BYTE)(n>>(8*i));}
static DWORD get32(const BYTE* b){DWORD n=0;for(unsigned i=0;i<4;i++)n|=(DWORD)b[i]<<(8*i);return n;}
static ULONGLONG get64(const BYTE* b){ULONGLONG n=0;for(unsigned i=0;i<8;i++)n|=(ULONGLONG)b[i]<<(8*i);return n;}
static bool version(const char* s,bool latest){
    if(!memchr(s,0,32))return false;if(latest && !strcmp(s,"latest"))return true;
    for(unsigned part=0;part<3;part++){
        const char* begin=s;unsigned digits=0;
        while(*s>='0' && *s<='9'){s++;if(++digits>9)return false;}
        if(!digits || (digits>1 && *begin=='0'))return false;
        if(part<2){if(*s++!='.')return false;}else if(*s)return false;
    }return true;
}
static bool uuid(const char s[37]){
    if(s[36])return false;bool nonzero=false;
    for(unsigned i=0;i<36;i++){
        if(i==8 || i==13 || i==18 || i==23){if(s[i]!='-')return false;continue;}
        char c=s[i];if(!((c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F')))return false;
        if(c!='0')nonzero=true;
    }return nonzero;
}
static bool time_valid(ULONGLONG ticks){FILETIME f={(DWORD)ticks,(DWORD)(ticks>>32)};SYSTEMTIME s;
    return ticks && FileTimeToSystemTime(&f,&s) && s.wYear>=2000 && s.wYear<=9999;
}
static bool valid(const L4RemoteResult* v){
    return v && uuid(v->operation_id) && v->target==1 &&
        (v->result==L4_REMOTE_RESULT_FAILED || v->result==L4_REMOTE_RESULT_CANCELLED) && v->error &&
        (v->result!=L4_REMOTE_RESULT_CANCELLED || v->error==ERROR_CANCELLED) &&
        (v->result!=L4_REMOTE_RESULT_FAILED || v->error!=ERROR_CANCELLED) &&
        version(v->requested_version,true) && version(v->previous_version,false) &&
        (!v->resolved_version[0] || version(v->resolved_version,false)) &&
        time_valid(v->started_at) && time_valid(v->finished_at) && v->finished_at>=v->started_at;
}
bool l4_remote_result_encode(const L4RemoteResult* v,BYTE out[L4_REMOTE_RESULT_BYTES]){
    if(out)memset(out,0,L4_REMOTE_RESULT_BYTES);if(!out || !valid(v))return fail();
    memcpy(out,"L4RSLT01",8);put32(out+8,1);put32(out+12,v->target);put32(out+16,v->result);put32(out+20,v->error);
    put64(out+24,v->started_at);put64(out+32,v->finished_at);
    memcpy(out+40,v->operation_id,36);memcpy(out+80,v->requested_version,strlen(v->requested_version));
    memcpy(out+112,v->previous_version,strlen(v->previous_version));memcpy(out+144,v->resolved_version,strlen(v->resolved_version));return true;
}
bool l4_remote_result_decode(const void* data,DWORD size,L4RemoteResult* v){
    if(v)memset(v,0,sizeof(*v));if(!data || !v || size!=L4_REMOTE_RESULT_BYTES)return fail();
    const BYTE* b=data;L4RemoteResult decoded={0};BYTE canonical[L4_REMOTE_RESULT_BYTES];
    if(memcmp(b,"L4RSLT01",8) || get32(b+8)!=1 || !memchr(b+80,0,32) || !memchr(b+112,0,32) || !memchr(b+144,0,32))return fail();
    decoded.target=get32(b+12);decoded.result=get32(b+16);decoded.error=get32(b+20);
    decoded.started_at=get64(b+24);decoded.finished_at=get64(b+32);memcpy(decoded.operation_id,b+40,36);
    memcpy(decoded.requested_version,b+80,32);memcpy(decoded.previous_version,b+112,32);memcpy(decoded.resolved_version,b+144,32);
    if(!l4_remote_result_encode(&decoded,canonical) || memcmp(canonical,b,size))return fail();*v=decoded;return true;
}
