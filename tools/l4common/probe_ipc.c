#include "probe_ipc.h"
#include <sddl.h>
#include <objbase.h>
#pragma comment(lib, "ole32.lib")
#include <stdio.h>
#include <stdlib.h>
#ifndef L4_PROBE_PREFIX
#define L4_PROBE_PREFIX L"L4"
#endif
struct L4ProbeServer {HANDLE pipe,stop,thread;L4ProbeCallback callback;L4DrainCallback drain,recovery;L4EvidenceCallback evidence;void* context;};
static bool evidence_caller(HANDLE pipe){
    if(!ImpersonateNamedPipeClient(pipe))return false;HANDLE token=NULL;BYTE bytes[512];DWORD n=0,session=~0u;
    bool ok=OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&token) && GetTokenInformation(token,TokenUser,bytes,sizeof(bytes),&n) &&
        IsWellKnownSid(((TOKEN_USER*)bytes)->User.Sid,WinLocalSystemSid) && GetTokenInformation(token,TokenSessionId,&session,sizeof(session),&n) && !session;
    if(token)CloseHandle(token);if(!RevertToSelf()){TerminateProcess(GetCurrentProcess(),ERROR_CANNOT_IMPERSONATE);ExitProcess(ERROR_CANNOT_IMPERSONATE);}
    if(!ok)SetLastError(ERROR_ACCESS_DENIED);return ok;
}
static bool name_of(const wchar_t* component,wchar_t name[128]){
    if(!component || (wcscmp(component,L"con") && wcscmp(component,L"superv"))){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    return swprintf_s(name,128,L"\\\\.\\pipe\\%ls_%ls_Health_v1",L4_PROBE_PREFIX,component)>0;
}
static DWORD left(ULONGLONG deadline){ULONGLONG now=GetTickCount64();return now<deadline?(DWORD)(deadline-now):0;}
static bool update_expired(const L4UpdateState* state){
    FILETIME time;GetSystemTimeAsFileTime(&time);return state &&
        (((ULONGLONG)time.dwHighDateTime<<32)|time.dwLowDateTime)>=state->deadline_utc;
}
static bool io_done(HANDLE pipe,OVERLAPPED* io,BOOL immediate,HANDLE stop,ULONGLONG deadline,DWORD* bytes){
    if(!immediate && GetLastError()!=ERROR_IO_PENDING)return false;
    HANDLE waits[2]={io->hEvent,stop};DWORD result=immediate?WAIT_OBJECT_0:WaitForMultipleObjects(stop?2:1,waits,FALSE,left(deadline));
    if(result!=WAIT_OBJECT_0){CancelIoEx(pipe,io);GetOverlappedResult(pipe,io,bytes,TRUE);SetLastError(result==WAIT_OBJECT_0+1?ERROR_CANCELLED:ERROR_TIMEOUT);return false;}
    return GetOverlappedResult(pipe,io,bytes,FALSE)!=0;
}
/* Windows x86/x64 are LE; encode fixed offsets, never compiler struct padding. */
static void encode_update(BYTE bytes[72],const L4UpdateState* state){
    memset(bytes,0,72);memcpy(bytes,state->owner,40);memcpy(bytes+40,&state->window,4);
    memcpy(bytes+48,&state->generation,8);memcpy(bytes+56,&state->plan_sequence,8);memcpy(bytes+64,&state->deadline_utc,8);
}
static bool decode_update(const BYTE bytes[72],L4UpdateState* state){
    memset(state,0,sizeof(*state));memcpy(state->owner,bytes,40);memcpy(&state->window,bytes+40,4);
    memcpy(&state->generation,bytes+48,8);memcpy(&state->plan_sequence,bytes+56,8);memcpy(&state->deadline_utc,bytes+64,8);
    bool nonzero=false;for(unsigned i=0;i<36;i++){
        char c=state->owner[i];if(i==8 || i==13 || i==18 || i==23){if(c!='-')return false;}
        else if((c>='0' && c<='9') || (c>='a' && c<='f')){if(c!='0')nonzero=true;}else return false;
    }
    DWORD reserved=0;memcpy(&reserved,bytes+44,4);
    return nonzero && !bytes[36] && !bytes[37] && !bytes[38] && !bytes[39] && !reserved &&
        state->window>=1 && state->window<=2 && state->generation && state->plan_sequence && state->deadline_utc;
}
static DWORD WINAPI serve(void* context){
    L4ProbeServer* s=(L4ProbeServer*)context;OVERLAPPED io={0};io.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);if(!io.hEvent)return 1;
    while(WaitForSingleObject(s->stop,0)==WAIT_TIMEOUT){
        DWORD bytes=0;ResetEvent(io.hEvent);BOOL connected=ConnectNamedPipe(s->pipe,&io);DWORD code=GetLastError();
        if(!connected && code!=ERROR_PIPE_CONNECTED && !io_done(s->pipe,&io,connected,s->stop,GetTickCount64()+60000,&bytes)){
            if(WaitForSingleObject(s->stop,0)!=WAIT_TIMEOUT)break;DisconnectNamedPipe(s->pipe);continue;
        }
        DWORD request[22]={0},answer=ERROR_INVALID_DATA;BYTE response[44+L4_LINK_EVIDENCE_BYTES]={0};DWORD response_size=4;
        ULONGLONG deadline=GetTickCount64()+1000;
        ResetEvent(io.hEvent);BOOL read=ReadFile(s->pipe,request,sizeof(request),NULL,&io);
        if(io_done(s->pipe,&io,read,s->stop,deadline,&bytes) && request[2] && request[2]<=300000 && !request[3]){
            if(bytes==16 && request[0]==1 && request[1]<=1){
                deadline=GetTickCount64()+request[2];answer=s->callback(request[1],request[2],s->stop,s->context);
            }else if(bytes==sizeof(request) && request[0]==2 && (request[1]==2 || request[1]==3)){
                L4UpdateState expected;response_size=76;memcpy(response+4,(BYTE*)request+16,72);
                if(decode_update((BYTE*)request+16,&expected) && (request[1]==2 || expected.window==L4_UPDATE_COMMUNICATION)){
                    deadline=GetTickCount64()+request[2];
                    L4DrainCallback handler=request[1]==2?s->drain:s->recovery;
                    answer=handler?handler(&expected,request[2],s->stop,s->context):ERROR_NOT_SUPPORTED;
                }
            }else if(bytes==56 && request[0]==3 && request[1]==4){
                response_size=sizeof(response);memcpy(response+4,(BYTE*)request+16,40);L4LinkProbeEvidence evidence={0};
                if(l4_link_nonce_valid((const char*)request+16)){
                    deadline=GetTickCount64()+request[2];
                    answer=!evidence_caller(s->pipe)?ERROR_ACCESS_DENIED:!s->evidence?ERROR_NOT_SUPPORTED:s->evidence(request[2],s->stop,s->context,&evidence);
                    if(!answer && !l4_link_evidence_encode(&evidence,response+44))answer=ERROR_INVALID_DATA;
                }
            }
            if(!left(deadline))answer=ERROR_TIMEOUT;
            if(WaitForSingleObject(s->stop,0)!=WAIT_TIMEOUT)answer=ERROR_CANCELLED;
        }
        memcpy(response,&answer,4);
        if(answer && response_size==sizeof(response))memset(response+44,0,L4_LINK_EVIDENCE_BYTES);
        ResetEvent(io.hEvent);BOOL written=WriteFile(s->pipe,response,response_size,NULL,&io);
        if(io_done(s->pipe,&io,written,s->stop,GetTickCount64()+1000,&bytes)){
            BYTE receipt=0;ResetEvent(io.hEvent);BOOL read_receipt=ReadFile(s->pipe,&receipt,1,NULL,&io);
            io_done(s->pipe,&io,read_receipt,s->stop,GetTickCount64()+1000,&bytes);
        }
        DisconnectNamedPipe(s->pipe);
    }
    CloseHandle(io.hEvent);return 0;
}
bool l4_probe_server_start_evidence(const wchar_t* component,L4ProbeCallback callback,L4DrainCallback drain,L4DrainCallback recovery,L4EvidenceCallback evidence,void* context,L4ProbeServer** result){
    if(!result || !callback){SetLastError(ERROR_INVALID_PARAMETER);return false;}*result=NULL;wchar_t name[128];if(!name_of(component,name))return false;
    L4ProbeServer* s=(L4ProbeServer*)calloc(1,sizeof(*s));if(!s){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return false;}
    PSECURITY_DESCRIPTOR sd=NULL;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",SDDL_REVISION_1,&sd,NULL)){free(s);return false;}
    SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE};s->pipe=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,512,128,0,&sa);DWORD code=GetLastError();LocalFree(sd);
    if(s->pipe==INVALID_HANDLE_VALUE){free(s);SetLastError(code);return false;}
    s->callback=callback;s->drain=drain;s->recovery=recovery;s->evidence=evidence;s->context=context;s->stop=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(s->stop)s->thread=CreateThread(NULL,0,serve,s,0,NULL);
    if(!s->thread){code=GetLastError();if(s->stop)CloseHandle(s->stop);CloseHandle(s->pipe);free(s);SetLastError(code);return false;}
    *result=s;return true;
}
bool l4_probe_server_start_update(const wchar_t* component,L4ProbeCallback callback,L4DrainCallback drain,L4DrainCallback recovery,void* context,L4ProbeServer** result){
    return l4_probe_server_start_evidence(component,callback,drain,recovery,NULL,context,result);
}
bool l4_probe_server_start_ex(const wchar_t* component,L4ProbeCallback callback,L4DrainCallback drain,void* context,L4ProbeServer** result){
    return l4_probe_server_start_update(component,callback,drain,NULL,context,result);
}
bool l4_probe_server_start(const wchar_t* component,L4ProbeCallback callback,void* context,L4ProbeServer** result){
    return l4_probe_server_start_ex(component,callback,NULL,context,result);
}
void l4_probe_server_stop(L4ProbeServer* s){if(!s)return;SetEvent(s->stop);WaitForSingleObject(s->thread,INFINITE);CloseHandle(s->thread);CloseHandle(s->stop);CloseHandle(s->pipe);free(s);}
static bool call(const wchar_t* component,DWORD expected_pid,DWORD mode,const L4UpdateState* expected,DWORD timeout_ms,L4LinkProbeEvidence* evidence){
    if(evidence)memset(evidence,0,sizeof(*evidence));
    if(!expected_pid || mode>4 || ((mode==2 || mode==3)!= (expected!=NULL)) || ((mode==4)!=(evidence!=NULL)) || !timeout_ms || timeout_ms>300000){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    if(update_expired(expected)){SetLastError(ERROR_TIMEOUT);return false;}
    FILETIME started;GetSystemTimeAsFileTime(&started);ULONGLONG start_utc=((ULONGLONG)started.dwHighDateTime<<32)|started.dwLowDateTime;
    wchar_t name[128];if(!name_of(component,name))return false;ULONGLONG deadline=GetTickCount64()+timeout_ms;HANDLE pipe=INVALID_HANDLE_VALUE;
    while(left(deadline)){
        pipe=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,NULL);
        if(pipe!=INVALID_HANDLE_VALUE)break;DWORD code=GetLastError();if(code!=ERROR_PIPE_BUSY && code!=ERROR_FILE_NOT_FOUND && code!=ERROR_NO_DATA && code!=ERROR_PIPE_NOT_CONNECTED)return false;
        Sleep(left(deadline)<25?left(deadline):25);
    }
    if(pipe==INVALID_HANDLE_VALUE){SetLastError(ERROR_TIMEOUT);return false;}
    ULONG pid=0;bool ok=GetNamedPipeServerProcessId(pipe,&pid)!=0;DWORD code=GetLastError();
    if(ok && pid!=expected_pid){ok=false;code=ERROR_REVISION_MISMATCH;}
    DWORD read_mode=PIPE_READMODE_MESSAGE;if(ok){ok=SetNamedPipeHandleState(pipe,&read_mode,NULL,NULL)!=0;code=GetLastError();}
    OVERLAPPED io={0};io.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);if(!io.hEvent){ok=false;code=GetLastError();}
    DWORD bytes=0,answer=ERROR_NOT_READY,request[22]={expected?2:1,mode,left(deadline),0};
    BYTE response[44+L4_LINK_EVIDENCE_BYTES]={0};DWORD request_size=expected?sizeof(request):16,response_size=expected?76:4;
    if(evidence){GUID nonce;wchar_t text[40];request[0]=3;request_size=56;response_size=sizeof(response);
        if(FAILED(CoCreateGuid(&nonce)) || StringFromGUID2(&nonce,text,40)!=39){ok=false;code=ERROR_GEN_FAILURE;}
        else{char* out=(char*)request+16;for(unsigned i=0;i<36;i++){wchar_t c=text[i+1];out[i]=(char)(c>=L'A' && c<=L'F'?c+32:c);}}
    }
    if(expected){L4UpdateState checked;encode_update((BYTE*)request+16,expected);
        if(!decode_update((BYTE*)request+16,&checked)){ok=false;code=ERROR_INVALID_PARAMETER;}}
    if(!request[2]){ok=false;code=ERROR_TIMEOUT;}
    if(ok){BOOL sent=WriteFile(pipe,request,request_size,NULL,&io);ok=io_done(pipe,&io,sent,NULL,deadline,&bytes)&&bytes==request_size;code=GetLastError();}
    if(ok){ResetEvent(io.hEvent);BOOL read=ReadFile(pipe,response,response_size,NULL,&io);ok=io_done(pipe,&io,read,NULL,deadline,&bytes)&&bytes==response_size;code=GetLastError();}
    if(ok){memcpy(&answer,response,4);if((expected && memcmp(response+4,(BYTE*)request+16,72)) || (evidence && memcmp(response+4,(BYTE*)request+16,40))){ok=false;code=ERROR_REVISION_MISMATCH;}}
    if(ok){ULONG again=0;if(!GetNamedPipeServerProcessId(pipe,&again) || again!=expected_pid){ok=false;code=ERROR_REVISION_MISMATCH;}}
    if(ok){BYTE receipt=1;ResetEvent(io.hEvent);BOOL sent=WriteFile(pipe,&receipt,1,NULL,&io);ok=io_done(pipe,&io,sent,NULL,deadline,&bytes)&&bytes==1;code=GetLastError();}
    if(ok && (!left(deadline) || update_expired(expected))){ok=false;code=ERROR_TIMEOUT;}
    if(ok && answer){ok=false;code=answer;}
    if(ok && evidence && !l4_link_evidence_decode(response+44,evidence)){ok=false;code=ERROR_INVALID_DATA;}
    if(ok && evidence){FILETIME finished;GetSystemTimeAsFileTime(&finished);ULONGLONG end_utc=((ULONGLONG)finished.dwHighDateTime<<32)|finished.dwLowDateTime;
        if(evidence->req_sent_utc<start_utc || evidence->eva_received_utc>end_utc){ok=false;code=ERROR_INVALID_DATA;}}
    if(!ok && evidence)memset(evidence,0,sizeof(*evidence));
    if(io.hEvent)CloseHandle(io.hEvent);CloseHandle(pipe);if(!ok)SetLastError(code?code:ERROR_INVALID_DATA);return ok;
}
bool l4_probe_call(const wchar_t* component,DWORD expected_pid,DWORD mode,DWORD timeout_ms){
    if(mode>1){SetLastError(ERROR_INVALID_PARAMETER);return false;}return call(component,expected_pid,mode,NULL,timeout_ms,NULL);
}
bool l4_probe_drain_call(const wchar_t* component,DWORD expected_pid,const L4UpdateState* expected,DWORD timeout_ms){
    if(!expected){SetLastError(ERROR_INVALID_PARAMETER);return false;}return call(component,expected_pid,2,expected,timeout_ms,NULL);
}

bool l4_probe_recovery_call(DWORD expected_pid,const L4UpdateState* expected,DWORD timeout_ms){
    if(!expected || expected->window!=L4_UPDATE_COMMUNICATION){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    return call(L"superv",expected_pid,3,expected,timeout_ms,NULL);
}

bool l4_probe_evidence_call(DWORD expected_pid,DWORD timeout_ms,L4LinkProbeEvidence* evidence){
    if(!evidence){SetLastError(ERROR_INVALID_PARAMETER);return false;}return call(L"con",expected_pid,4,NULL,timeout_ms,evidence);
}
