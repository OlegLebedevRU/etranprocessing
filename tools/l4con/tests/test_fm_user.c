/* WinAPI-modeled token selection only; never fabricates an OS/logon token. */
#include <windows.h>
#include <wtsapi32.h>
#include <sddl.h>
#include <stdio.h>
#include <stdbool.h>
static PSID user_sid,other_sid;static TOKEN_ELEVATION_TYPE type,linked_type;static bool primary_admin,has_link,wrong_link_sid,wrong_link_session,wrong_fresh_sid,new_logon,console_switch,duplicate_fail;static DWORD link_error;static unsigned wts_calls,console_calls,duplicates,checks,failures;static HANDLE duplicated_from;static bool held_wrong_sid,held_wrong_session,held_wrong_type;static DWORD held_auth;
#define PRIMARY ((HANDLE)(ULONG_PTR)1)
#define LINKED ((HANDLE)(ULONG_PTR)2)
#define FRESH ((HANDLE)(ULONG_PTR)3)
#define IMPERSONATION ((HANDLE)(ULONG_PTR)4)
#define HELD ((HANDLE)(ULONG_PTR)5)
static DWORD WINAPI console(void){return console_switch&&++console_calls>1?2:1;}
static BOOL WINAPI query(ULONG session,PHANDLE output){if(session!=1){SetLastError(ERROR_NO_SUCH_LOGON_SESSION);return FALSE;}*output=++wts_calls==1?PRIMARY:FRESH;return TRUE;}
static BOOL WINAPI information(HANDLE token,TOKEN_INFORMATION_CLASS cls,LPVOID out,DWORD capacity,PDWORD size){
 if(cls==TokenLinkedToken){*size=sizeof(TOKEN_LINKED_TOKEN);if(!has_link){SetLastError(link_error);return FALSE;}((TOKEN_LINKED_TOKEN*)out)->LinkedToken=LINKED;return TRUE;}
 if(cls==TokenUser){if(capacity<sizeof(TOKEN_USER))return FALSE;*size=sizeof(TOKEN_USER);((TOKEN_USER*)out)->User.Sid=(token==LINKED&&wrong_link_sid)||(token==FRESH&&wrong_fresh_sid)||(token==HELD&&held_wrong_sid)?other_sid:user_sid;return TRUE;}
 if(cls==TokenSessionId){*(DWORD*)out=(token==LINKED&&wrong_link_session)||(token==HELD&&held_wrong_session)?2:1;*size=sizeof(DWORD);return TRUE;}
 if(cls==TokenElevationType){*(TOKEN_ELEVATION_TYPE*)out=token==LINKED?linked_type:type;*size=sizeof(TOKEN_ELEVATION_TYPE);return TRUE;}
 if(cls==TokenStatistics){TOKEN_STATISTICS* stats=out;ZeroMemory(stats,sizeof(*stats));stats->TokenType=token==IMPERSONATION||(token==HELD&&!held_wrong_type)?TokenImpersonation:TokenPrimary;stats->AuthenticationId.LowPart=(token==LINKED||(token==IMPERSONATION&&duplicated_from==LINKED))?200:100;if(token==HELD)stats->AuthenticationId.LowPart=held_auth;if(token==FRESH&&new_logon)stats->AuthenticationId.LowPart=101;*size=sizeof(*stats);return TRUE;}
 SetLastError(ERROR_INVALID_PARAMETER);return FALSE;
}
static BOOL WINAPI duplicate(HANDLE input,DWORD rights,LPSECURITY_ATTRIBUTES attributes,SECURITY_IMPERSONATION_LEVEL level,TOKEN_TYPE token_type,PHANDLE output){(void)attributes;if(rights!=(TOKEN_QUERY|TOKEN_IMPERSONATE)||level!=SecurityImpersonation||token_type!=TokenImpersonation){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}duplicates++;if(duplicate_fail){SetLastError(ERROR_BAD_IMPERSONATION_LEVEL);return FALSE;}duplicated_from=input;*output=IMPERSONATION;return TRUE;}
static BOOL WINAPI membership(HANDLE token,PSID sid,PBOOL admin){(void)sid;if(token!=IMPERSONATION){SetLastError(ERROR_BAD_TOKEN_TYPE);return FALSE;}*admin=duplicated_from==LINKED?linked_type==TokenElevationTypeFull:primary_admin;return TRUE;}
static BOOL WINAPI close_token(HANDLE token){return token!=NULL;}
#define WTSGetActiveConsoleSessionId console
#define WTSQueryUserToken query
#define GetTokenInformation information
#define DuplicateTokenEx duplicate
#define CheckTokenMembership membership
#define CloseHandle close_token
#include "../src/fm_user.h"
#undef CloseHandle
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL token %u: %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static void reset(TOKEN_ELEVATION_TYPE elevation,bool admin){type=elevation;primary_admin=admin;has_link=false;linked_type=TokenElevationTypeLimited;link_error=ERROR_NO_SUCH_LOGON_SESSION;wrong_link_sid=wrong_link_session=wrong_fresh_sid=new_logon=console_switch=duplicate_fail=false;wts_calls=console_calls=duplicates=0;duplicated_from=NULL;}
static HANDLE capture(DWORD* session,LUID* auth){return fm_desktop_token(session,auth);}
int main(void){CHECK(ConvertStringSidToSidW(L"S-1-5-21-1-2-3-1001",&user_sid));CHECK(ConvertStringSidToSidW(L"S-1-5-21-1-2-3-1002",&other_sid));DWORD session=0;LUID auth={0};HANDLE result;
 reset(TokenElevationTypeDefault,false);result=capture(&session,&auth);CHECK(result==IMPERSONATION&&duplicated_from==PRIMARY&&session==1&&auth.LowPart==100);
 reset(TokenElevationTypeDefault,true);result=capture(&session,&auth);CHECK(result==IMPERSONATION&&duplicated_from==PRIMARY&&auth.LowPart==100); /* UAC off/autologon nonsplit admin */
 reset(TokenElevationTypeLimited,false);has_link=true;linked_type=TokenElevationTypeFull;CHECK(capture(&session,&auth)==IMPERSONATION&&duplicated_from==PRIMARY);
 reset(TokenElevationTypeFull,true);has_link=true;CHECK(capture(&session,&auth)==IMPERSONATION&&duplicated_from==LINKED&&auth.LowPart==200);
 reset(TokenElevationTypeDefault,true);has_link=true;CHECK(capture(&session,&auth)==IMPERSONATION&&duplicated_from==LINKED); /* Prefer a verified limited pair if present. */
 reset(TokenElevationTypeFull,true);CHECK(!capture(&session,&auth)&&duplicates==0); /* No fallback on missing linked token. */
 reset(TokenElevationTypeDefault,true);link_error=ERROR_ACCESS_DENIED;CHECK(!capture(&session,&auth)&&duplicates==0&&GetLastError()==ERROR_ACCESS_DENIED);
 reset(TokenElevationTypeFull,true);has_link=true;linked_type=TokenElevationTypeFull;CHECK(!capture(&session,&auth)&&duplicates==0);
 reset(TokenElevationTypeFull,true);has_link=true;linked_type=TokenElevationTypeDefault;CHECK(!capture(&session,&auth)&&duplicates==0);
 reset(TokenElevationTypeFull,true);has_link=true;wrong_link_sid=true;CHECK(!capture(&session,&auth)&&duplicates==0);
 reset(TokenElevationTypeFull,true);has_link=true;wrong_link_session=true;CHECK(!capture(&session,&auth)&&duplicates==0);
 reset(TokenElevationTypeLimited,true);CHECK(!capture(&session,&auth)); /* An unexpectedly enabled admin is not a limited token. */
 reset(TokenElevationTypeDefault,true);new_logon=true;CHECK(!capture(&session,&auth));
 reset(TokenElevationTypeDefault,true);wrong_fresh_sid=true;CHECK(!capture(&session,&auth));
 reset(TokenElevationTypeDefault,true);console_switch=true;CHECK(!capture(&session,&auth));
 reset(TokenElevationTypeDefault,true);duplicate_fail=true;CHECK(!capture(&session,&auth)&&GetLastError()==ERROR_BAD_IMPERSONATION_LEVEL);
 reset((TOKEN_ELEVATION_TYPE)77,false);CHECK(!capture(&session,&auth)&&duplicates==0);
 CHECK(!fm_desktop_token(NULL,&auth)&&GetLastError()==ERROR_INVALID_PARAMETER);
 /* Retained actor is compared, never silently substituted by the fresh WTS token. */
 reset(TokenElevationTypeDefault,false);held_auth=100;held_wrong_sid=held_wrong_session=held_wrong_type=false;session=1;auth.HighPart=0;auth.LowPart=100;
 CHECK(l4_physical_console_token_matches(HELD,session,auth));
 reset(TokenElevationTypeDefault,true);CHECK(l4_physical_console_token_matches(HELD,session,auth));
 reset(TokenElevationTypeFull,true);has_link=true;held_auth=200;auth.LowPart=200;CHECK(l4_physical_console_token_matches(HELD,session,auth));
 reset(TokenElevationTypeDefault,false);held_auth=100;auth.LowPart=100;held_wrong_sid=true;CHECK(!l4_physical_console_token_matches(HELD,session,auth));held_wrong_sid=false;
 reset(TokenElevationTypeDefault,false);held_wrong_session=true;CHECK(!l4_physical_console_token_matches(HELD,session,auth));held_wrong_session=false;
 reset(TokenElevationTypeDefault,false);held_wrong_type=true;CHECK(!l4_physical_console_token_matches(HELD,session,auth));held_wrong_type=false;
 reset(TokenElevationTypeDefault,false);held_auth=101;CHECK(!l4_physical_console_token_matches(HELD,session,auth));held_auth=100;
 reset(TokenElevationTypeDefault,false);new_logon=true;CHECK(!l4_physical_console_token_matches(HELD,session,auth));
 reset(TokenElevationTypeDefault,false);console_switch=true;CHECK(!l4_physical_console_token_matches(HELD,session,auth));
 reset(TokenElevationTypeDefault,false);link_error=ERROR_ACCESS_DENIED;CHECK(!l4_physical_console_token_matches(HELD,session,auth)&&GetLastError()==ERROR_ACCESS_DENIED);
 CHECK(!l4_physical_console_token_matches(NULL,session,auth)&&GetLastError()==ERROR_NO_TOKEN);
 CHECK(!l4_physical_console_token_matches(HELD,0,auth)&&GetLastError()==ERROR_NO_TOKEN);
 LocalFree(user_sid);LocalFree(other_sid);printf("FM physical token selection: %u checks, %u failures\n",checks,failures);return failures?1:0;}
