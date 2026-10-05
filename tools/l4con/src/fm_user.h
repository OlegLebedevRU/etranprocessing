#ifndef L4CON_FM_USER_H
#define L4CON_FM_USER_H
#include <windows.h>
#include <wtsapi32.h>
#include <stdbool.h>
#pragma comment(lib,"wtsapi32.lib")

/* Only the physical desktop user. No elevated token selection and no SYSTEM fallback. */
static HANDLE fm_desktop_token(DWORD* session_id, LUID* authentication) {
    DWORD session=WTSGetActiveConsoleSessionId(),size=0;
    HANDLE primary=NULL,limited=NULL,impersonation=NULL;
    TOKEN_ELEVATION_TYPE elevation=TokenElevationTypeDefault;
    if(session==0xffffffff || !WTSQueryUserToken(session,&primary))return NULL;
    bool ok=GetTokenInformation(primary,TokenElevationType,&elevation,sizeof(elevation),&size)!=0;
    if(ok && elevation==TokenElevationTypeFull) {
        TOKEN_LINKED_TOKEN linked={0};
        ok=GetTokenInformation(primary,TokenLinkedToken,&linked,sizeof(linked),&size)!=0;
        if(ok)limited=linked.LinkedToken;
    }
    if(ok)ok=DuplicateTokenEx(limited?limited:primary,TOKEN_QUERY|TOKEN_IMPERSONATE,NULL,SecurityImpersonation,TokenImpersonation,&impersonation)!=0;
    if(limited)CloseHandle(limited);CloseHandle(primary);
    BOOL admin=TRUE;BYTE sid[SECURITY_MAX_SID_SIZE];DWORD sid_size=sizeof(sid);
    TOKEN_STATISTICS stats={0};
    ok=ok && CreateWellKnownSid(WinBuiltinAdministratorsSid,NULL,sid,&sid_size) &&
        CheckTokenMembership(impersonation,sid,&admin) && !admin &&
        GetTokenInformation(impersonation,TokenStatistics,&stats,sizeof(stats),&size);
    if(!ok) {if(impersonation)CloseHandle(impersonation);return NULL;}
    *session_id=session;*authentication=stats.AuthenticationId;return impersonation;
}
#endif
