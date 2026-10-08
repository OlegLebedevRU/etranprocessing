#ifndef L4_PHYSICAL_CONSOLE_TOKEN_H
#define L4_PHYSICAL_CONSOLE_TOKEN_H
#include <windows.h>
#include <wtsapi32.h>
#include <stdbool.h>
#pragma comment(lib,"wtsapi32.lib")

/* Physical console identity only. Prefer its limited token; an unlinked Default
 * token (standard user or UAC off/autologon admin) keeps its actual OS rights.
 * No SYSTEM/service/alternate-session fallback and no FM capability/ACL grant. */
static HANDLE l4_physical_console_token(DWORD* session_id,LUID* authentication){
 if(!session_id||!authentication){SetLastError(ERROR_INVALID_PARAMETER);return NULL;}
 DWORD session=WTSGetActiveConsoleSessionId(),size=0,error=ERROR_ACCESS_DENIED;HANDLE primary=NULL,linked=NULL,selected=NULL,fresh=NULL,impersonation=NULL;
 TOKEN_ELEVATION_TYPE elevation=TokenElevationTypeDefault,selected_elevation=TokenElevationTypeDefault;TOKEN_STATISTICS original={0},statistics={0},repeated={0};
 BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE],chosen[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE],again[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD actual_session=0;
 if(session==0xffffffff || session==0){SetLastError(ERROR_NO_SUCH_LOGON_SESSION);return NULL;}
 if(!WTSQueryUserToken(session,&primary))return NULL;
#define L4_CONSOLE_TOKEN_REQUIRE(x) do{SetLastError(ERROR_SUCCESS);if(!(x)){error=GetLastError()?GetLastError():ERROR_ACCESS_DENIED;goto done;}}while(0)
 L4_CONSOLE_TOKEN_REQUIRE(GetTokenInformation(primary,TokenUser,user,sizeof(user),&size)&&GetTokenInformation(primary,TokenSessionId,&actual_session,sizeof(actual_session),&size)&&actual_session==session&&GetTokenInformation(primary,TokenStatistics,&original,sizeof(original),&size)&&original.TokenType==TokenPrimary);
 L4_CONSOLE_TOKEN_REQUIRE(!IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid)&&!IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalServiceSid)&&!IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinNetworkServiceSid));
 L4_CONSOLE_TOKEN_REQUIRE(GetTokenInformation(primary,TokenElevationType,&elevation,sizeof(elevation),&size));selected=primary;
 if(elevation==TokenElevationTypeFull || elevation==TokenElevationTypeDefault){
  TOKEN_LINKED_TOKEN pair={0};SetLastError(ERROR_SUCCESS);BOOL exists=GetTokenInformation(primary,TokenLinkedToken,&pair,sizeof(pair),&size);DWORD failure=GetLastError();
  if(exists){linked=pair.LinkedToken;L4_CONSOLE_TOKEN_REQUIRE(linked!=NULL&&linked!=INVALID_HANDLE_VALUE);selected=linked;}
  else if(elevation!=TokenElevationTypeDefault || failure!=ERROR_NO_SUCH_LOGON_SESSION){error=failure?failure:ERROR_INVALID_DATA;goto done;}
 }else L4_CONSOLE_TOKEN_REQUIRE(elevation==TokenElevationTypeLimited);
 L4_CONSOLE_TOKEN_REQUIRE(GetTokenInformation(selected,TokenUser,chosen,sizeof(chosen),&size)&&EqualSid(((TOKEN_USER*)user)->User.Sid,((TOKEN_USER*)chosen)->User.Sid)&&GetTokenInformation(selected,TokenSessionId,&actual_session,sizeof(actual_session),&size)&&actual_session==session&&GetTokenInformation(selected,TokenElevationType,&selected_elevation,sizeof(selected_elevation),&size));
 L4_CONSOLE_TOKEN_REQUIRE(selected==primary?selected_elevation==elevation:selected_elevation==TokenElevationTypeLimited);
 L4_CONSOLE_TOKEN_REQUIRE(DuplicateTokenEx(selected,TOKEN_QUERY|TOKEN_IMPERSONATE,NULL,SecurityImpersonation,TokenImpersonation,&impersonation));
 BOOL admin=TRUE;BYTE sid[SECURITY_MAX_SID_SIZE];DWORD sid_size=sizeof(sid);
 L4_CONSOLE_TOKEN_REQUIRE(CreateWellKnownSid(WinBuiltinAdministratorsSid,NULL,sid,&sid_size)&&CheckTokenMembership(impersonation,sid,&admin)&&((selected==primary&&elevation==TokenElevationTypeDefault)||!admin)&&GetTokenInformation(impersonation,TokenStatistics,&statistics,sizeof(statistics),&size)&&statistics.TokenType==TokenImpersonation);
 /* A logoff/new logon can reuse the same console session number. Compare the
  * fresh WTS primary logon identity as well as SID/session before returning. */
 L4_CONSOLE_TOKEN_REQUIRE(WTSGetActiveConsoleSessionId()==session&&WTSQueryUserToken(session,&fresh)&&GetTokenInformation(fresh,TokenUser,again,sizeof(again),&size)&&EqualSid(((TOKEN_USER*)user)->User.Sid,((TOKEN_USER*)again)->User.Sid)&&GetTokenInformation(fresh,TokenSessionId,&actual_session,sizeof(actual_session),&size)&&actual_session==session&&GetTokenInformation(fresh,TokenStatistics,&repeated,sizeof(repeated),&size)&&repeated.TokenType==TokenPrimary&&original.AuthenticationId.HighPart==repeated.AuthenticationId.HighPart&&original.AuthenticationId.LowPart==repeated.AuthenticationId.LowPart);
 *session_id=session;*authentication=statistics.AuthenticationId;error=ERROR_SUCCESS;
done:
 if(fresh)CloseHandle(fresh);if(linked&&linked!=INVALID_HANDLE_VALUE)CloseHandle(linked);if(primary)CloseHandle(primary);
 if(error!=ERROR_SUCCESS){if(impersonation)CloseHandle(impersonation);impersonation=NULL;}
 SetLastError(error);return impersonation;
#undef L4_CONSOLE_TOKEN_REQUIRE
}

/* Revalidate retained selection before source/handoff checks. The original token
 * is never replaced; a logoff, console switch, new logon or selection drift
 * refuses. Only the newly queried token is closed by this function. */
static __inline bool l4_physical_console_token_matches(HANDLE retained,DWORD session,LUID authentication){
 if(!retained || retained==INVALID_HANDLE_VALUE || !session || session==0xffffffff || (!authentication.LowPart&&!authentication.HighPart)){SetLastError(ERROR_NO_TOKEN);return false;}
 DWORD fresh_session=0;LUID fresh_auth={0};HANDLE fresh=l4_physical_console_token(&fresh_session,&fresh_auth);
 if(!fresh)return false;
 TOKEN_STATISTICS stats={0},fresh_stats={0};BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE],current[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD n=0,actual=0;
 SetLastError(ERROR_SUCCESS);
 bool ok=fresh_session==session && fresh_auth.HighPart==authentication.HighPart && fresh_auth.LowPart==authentication.LowPart &&
  GetTokenInformation(retained,TokenStatistics,&stats,sizeof(stats),&n) && stats.TokenType==TokenImpersonation &&
  stats.AuthenticationId.HighPart==authentication.HighPart && stats.AuthenticationId.LowPart==authentication.LowPart &&
  GetTokenInformation(fresh,TokenStatistics,&fresh_stats,sizeof(fresh_stats),&n) && fresh_stats.TokenType==TokenImpersonation &&
  fresh_stats.AuthenticationId.HighPart==fresh_auth.HighPart && fresh_stats.AuthenticationId.LowPart==fresh_auth.LowPart &&
  GetTokenInformation(retained,TokenSessionId,&actual,sizeof(actual),&n) && actual==session &&
  GetTokenInformation(retained,TokenUser,user,sizeof(user),&n) && GetTokenInformation(fresh,TokenUser,current,sizeof(current),&n) && EqualSid(((TOKEN_USER*)user)->User.Sid,((TOKEN_USER*)current)->User.Sid);
 DWORD error=ok?ERROR_SUCCESS:(GetLastError()?GetLastError():ERROR_NO_SUCH_LOGON_SESSION);CloseHandle(fresh);SetLastError(error);return ok;
}
#endif
