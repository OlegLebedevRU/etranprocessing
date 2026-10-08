#include "remote_outcome.h"
#include <string.h>
static void put32(BYTE* b,DWORD n){for(unsigned i=0;i<4;i++)b[i]=(BYTE)(n>>(i*8));}
static void put64(BYTE* b,ULONGLONG n){for(unsigned i=0;i<8;i++)b[i]=(BYTE)(n>>(i*8));}
static DWORD get32(const BYTE* b){DWORD n=0;for(unsigned i=0;i<4;i++)n|=(DWORD)b[i]<<(i*8);return n;}
static ULONGLONG get64(const BYTE* b){ULONGLONG n=0;for(unsigned i=0;i<8;i++)n|=(ULONGLONG)b[i]<<(i*8);return n;}
static bool fail(void){SetLastError(ERROR_INVALID_DATA);return false;}
static bool nonzero(const BYTE* bytes){BYTE v=0;for(unsigned i=0;i<32;i++)v|=bytes[i];return v!=0;}
bool l4_remote_outcome_encode(const L4RemoteOutcome* p,BYTE bytes[L4_REMOTE_OUTCOME_BYTES]){
 if(bytes)memset(bytes,0,L4_REMOTE_OUTCOME_BYTES);if(!p||!bytes||!p->plan_sequence||!nonzero(p->plan_sha256)||!p->result.resolved_version[0])return fail();
 DWORD kind=p->result.result;if(kind<L4_REMOTE_OUTCOME_SUCCESS||kind>L4_REMOTE_OUTCOME_RECOVERY_REQUIRED||(kind==L4_REMOTE_OUTCOME_SUCCESS&&p->result.error)||(kind!=L4_REMOTE_OUTCOME_SUCCESS&&!p->result.error))return fail();
 if(kind==L4_REMOTE_OUTCOME_RECOVERY_REQUIRED){if(p->proof_sequence||nonzero(p->proof_sha256))return fail();}
 else if(p->proof_sequence<=p->plan_sequence||!nonzero(p->proof_sha256))return fail();
 /* Reuse strictly bounded UUID/version/time validation, not94's semantics. */
 L4RemoteResult identity=p->result;if(!identity.error)identity.error=ERROR_GEN_FAILURE;
 identity.result=identity.error==ERROR_CANCELLED?L4_REMOTE_RESULT_CANCELLED:L4_REMOTE_RESULT_FAILED;
 if(!l4_remote_result_encode(&identity,bytes))return false;memcpy(bytes,"L4ROUT01",8);put32(bytes+16,kind);put32(bytes+20,p->result.error);
 put64(bytes+200,p->plan_sequence);memcpy(bytes+208,p->plan_sha256,32);put64(bytes+240,p->proof_sequence);memcpy(bytes+248,p->proof_sha256,32);return true;
}
bool l4_remote_outcome_decode(const void* data,DWORD size,L4RemoteOutcome* out){
 if(out)memset(out,0,sizeof(*out));if(!data||!out||size!=L4_REMOTE_OUTCOME_BYTES)return fail();const BYTE* bytes=data;
 if(memcmp(bytes,"L4ROUT01",8))return fail();DWORD kind=get32(bytes+16),error=get32(bytes+20);BYTE identity[L4_REMOTE_RESULT_BYTES];memcpy(identity,bytes,sizeof(identity));memcpy(identity,"L4RSLT01",8);
 put32(identity+16,error==ERROR_CANCELLED?L4_REMOTE_RESULT_CANCELLED:L4_REMOTE_RESULT_FAILED);put32(identity+20,error?error:ERROR_GEN_FAILURE);L4RemoteOutcome value={0};
 if(!l4_remote_result_decode(identity,sizeof(identity),&value.result))return false;value.result.result=kind;value.result.error=error;
 value.plan_sequence=get64(bytes+200);memcpy(value.plan_sha256,bytes+208,32);value.proof_sequence=get64(bytes+240);memcpy(value.proof_sha256,bytes+248,32);
 BYTE canonical[L4_REMOTE_OUTCOME_BYTES];if(!l4_remote_outcome_encode(&value,canonical)||memcmp(bytes,canonical,size))return fail();*out=value;return true;
}
