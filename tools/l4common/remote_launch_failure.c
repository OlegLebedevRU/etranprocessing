#include "remote_launch_failure.h"
#include <string.h>
static bool fail(void){SetLastError(ERROR_INVALID_DATA);return false;}
static void put(BYTE* b,ULONGLONG n,unsigned width){for(unsigned i=0;i<width;i++)b[i]=(BYTE)(n>>(8*i));}
static ULONGLONG get(const BYTE* b,unsigned width){ULONGLONG n=0;for(unsigned i=0;i<width;i++)n|=(ULONGLONG)b[i]<<(8*i);return n;}
bool l4_remote_launch_failure_encode(const L4RemoteLaunchFailure* v,BYTE out[L4_REMOTE_LAUNCH_FAILURE_BYTES]){
    if(out)memset(out,0,L4_REMOTE_LAUNCH_FAILURE_BYTES);
    if(!v || !out || !v->plan_sequence || v->stage<1 || v->stage>8 || !v->result.resolved_version[0] ||
        !l4_remote_result_encode(&v->result,out+8))return fail();
    memcpy(out,"L4RFAL01",8);put(out+208,v->plan_sequence,8);put(out+216,v->stage,4);put(out+220,v->cleanup_error,4);return true;
}
bool l4_remote_launch_failure_decode(const void* data,DWORD size,L4RemoteLaunchFailure* v){
    if(v)memset(v,0,sizeof(*v));if(!data || !v || size!=L4_REMOTE_LAUNCH_FAILURE_BYTES)return fail();
    const BYTE* b=data;L4RemoteLaunchFailure decoded={0};BYTE canonical[L4_REMOTE_LAUNCH_FAILURE_BYTES];
    if(memcmp(b,"L4RFAL01",8) || !l4_remote_result_decode(b+8,L4_REMOTE_RESULT_BYTES,&decoded.result))return fail();
    decoded.plan_sequence=get(b+208,8);decoded.stage=(DWORD)get(b+216,4);decoded.cleanup_error=(DWORD)get(b+220,4);
    if(!l4_remote_launch_failure_encode(&decoded,canonical) || memcmp(b,canonical,size))return fail();*v=decoded;return true;
}
