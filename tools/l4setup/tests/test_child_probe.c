#include <winsock2.h>
#include "../../l4common/child_probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
static unsigned checks,failed,calls;static HANDLE observed;
#define CHECK(x) do { ++checks;if(!(x)){++failed;printf("FAIL %u: %s (%lu)\n",__LINE__,#x,GetLastError());} } while(0)
static SOCKET listener(WORD port,WORD* actual) {
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==INVALID_SOCKET)return s;
    BOOL exclusive=TRUE;struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);
    int size=sizeof(a);
    if(setsockopt(s,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(const char*)&exclusive,sizeof(exclusive)) ||
       bind(s,(struct sockaddr*)&a,sizeof(a)) || listen(s,2) || getsockname(s,(struct sockaddr*)&a,&size)) {closesocket(s);return INVALID_SOCKET;}
    *actual=ntohs(a.sin_port);return s;
}
typedef struct {WORD a,b;int kind;} Test;
static bool check_child(HANDLE process,DWORD pid,ULONGLONG deadline,void* context) {
    Test* test=context;++calls;
    CHECK(DuplicateHandle(GetCurrentProcess(),process,GetCurrentProcess(),&observed,SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,0));
    if(test->kind==1)return false;
    if(test->kind==2){Sleep(150);return true;}
    while(GetTickCount64()<deadline && WaitForSingleObject(process,0)==WAIT_TIMEOUT) {
        if(l4_child_listeners(process,pid,test->a,test->b))return true;Sleep(10);
    }
    return false;
}
int wmain(int argc,wchar_t** argv) {
    WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa))return 1;
    if(argc==4 && !wcscmp(argv[1],L"--listen")) {
        WORD a=0,b=0;SOCKET first=listener((WORD)_wtoi(argv[2]),&a),second=listener((WORD)_wtoi(argv[3]),&b);
        if(first==INVALID_SOCKET || second==INVALID_SOCKET)return 7;
        Sleep(INFINITE);return 0;
    }
    wchar_t exe[MAX_PATH],directory[MAX_PATH],command[512];CHECK(GetModuleFileNameW(NULL,exe,MAX_PATH));
    wcscpy_s(directory,MAX_PATH,exe);wchar_t* slash=wcsrchr(directory,L'\\');CHECK(slash!=NULL);if(!slash)return 1;*slash=0;
    WORD a,b;SOCKET first=listener(0,&a),second=listener(0,&b);CHECK(first!=INVALID_SOCKET && second!=INVALID_SOCKET);
    CHECK(l4_child_listeners(GetCurrentProcess(),GetCurrentProcessId(),a,b));
    CHECK(!l4_child_listeners(GetCurrentProcess(),GetCurrentProcessId()+1,a,b));
    CHECK(!l4_child_listeners(GetCurrentProcess(),GetCurrentProcessId(),a,a));
    closesocket(first);closesocket(second);Test test={a,b,0};
    swprintf_s(command,512,L"\"%ls\" --listen %u %u",exe,(unsigned)a,(unsigned)b);
    CHECK(l4_child_probe(exe,directory,command,3000,check_child,&test));
    CHECK(observed && WaitForSingleObject(observed,0)==WAIT_OBJECT_0);CloseHandle(observed);observed=NULL;
    first=listener(a,&a);second=listener(b,&b);CHECK(first!=INVALID_SOCKET && second!=INVALID_SOCKET);
    /* A foreign listener cannot satisfy either ownership check or child startup. */
    swprintf_s(command,512,L"\"%ls\" --listen %u %u",exe,(unsigned)a,(unsigned)b);
    CHECK(!l4_child_probe(exe,directory,command,1000,check_child,&test));
    CHECK(observed && WaitForSingleObject(observed,0)==WAIT_OBJECT_0);CloseHandle(observed);observed=NULL;
    CHECK(l4_child_listeners(GetCurrentProcess(),GetCurrentProcessId(),a,b));closesocket(first);closesocket(second);
    for(int kind=1;kind<=2;kind++) {
        test.kind=kind;swprintf_s(command,512,L"\"%ls\" --listen %u %u",exe,(unsigned)a,(unsigned)b);
        CHECK(!l4_child_probe(exe,directory,command,100,check_child,&test));
        CHECK(observed && WaitForSingleObject(observed,0)==WAIT_OBJECT_0);CloseHandle(observed);observed=NULL;
    }
    unsigned before=calls;wcscpy_s(command,512,L"missing");
    CHECK(!l4_child_probe(L"C:\\no-such-l4probe.exe",directory,command,100,check_child,&test));CHECK(calls==before);
    CHECK(!l4_child_probe(exe,directory,command,99,check_child,&test));CHECK(calls==before);
    CHECK(WaitForSingleObject(GetCurrentProcess(),0)==WAIT_TIMEOUT);
    WSACleanup();printf("child probe: %u passed, %u failed\n",checks-failed,failed);return failed?1:0;
}
