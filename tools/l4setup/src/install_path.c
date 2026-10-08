#include "install_path.h"
#include "../../l4common/journal_internal.h"
#include <stdlib.h>
#include <string.h>
#define PATH_LIMIT 65534u
#define PATH_PLAN 80u
#define PATH_INTENT 81u
#define PATH_DONE 82u
#define PATH_UNDO_INTENT 83u
#define PATH_UNDO_DONE 84u
struct SetupInstallPath {L4Journal* journal;L4Layout layout;BYTE header[24];HKEY key;BYTE* old;BYTE* next;DWORD old_size,next_size,old_type,next_type;ULONGLONG sequence;bool intent,done,undo,undo_intent;};
static bool reject(DWORD code){SetLastError(code);return false;}
static bool host(void){HANDLE token=NULL;BYTE user[512];DWORD size=0;
    if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&token)){CloseHandle(token);return reject(ERROR_ACCESS_DENIED);}
    if(GetLastError()!=ERROR_NO_TOKEN)return false;
    bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,user,sizeof(user),&size) && IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid);
    if(token)CloseHandle(token);return ok?true:reject(ERROR_ACCESS_DENIED);
}
static bool read_value(HKEY key,BYTE** bytes,DWORD* size,DWORD* type){
    *bytes=NULL;*size=*type=0;DWORD needed=0,t=0;LONG e=RegQueryValueExW(key,L"Path",NULL,&t,NULL,&needed);
    if(e==ERROR_FILE_NOT_FOUND)return true;if(e)return reject((DWORD)e);
    if((t!=REG_SZ && t!=REG_EXPAND_SZ) || needed<2 || needed>PATH_LIMIT || needed%2)return reject(ERROR_INVALID_DATA);
    BYTE* data=calloc(1,needed);if(!data)return reject(ERROR_NOT_ENOUGH_MEMORY);DWORD actual=needed;
    e=RegQueryValueExW(key,L"Path",NULL,&t,data,&actual);
    bool ok=!e && actual==needed && (t==REG_SZ || t==REG_EXPAND_SZ) && ((wchar_t*)data)[needed/2-1]==0 && wcslen((wchar_t*)data)==needed/2-1;
    if(!ok){free(data);return reject(e?(DWORD)e:ERROR_INVALID_DATA);}*bytes=data;*size=needed;*type=t;return true;
}
static bool equal(SetupInstallPath* p,bool next){BYTE* bytes=NULL;DWORD size=0,type=0;if(!read_value(p->key,&bytes,&size,&type))return false;
    DWORD expected_size=next?p->next_size:p->old_size,expected_type=next?p->next_type:p->old_type;
    bool ok=size==expected_size && type==expected_type && (!size || !memcmp(bytes,next?p->next:p->old,size));free(bytes);return ok?true:reject(ERROR_REVISION_MISMATCH);
}
static bool bound(L4Journal* j,SetupInstallPath* p){return j && p && j==p->journal && j->lock && j->lock!=INVALID_HANDLE_VALUE && !j->poisoned && !memcmp(j->header,p->header,24) && !memcmp(&j->layout,&p->layout,sizeof(p->layout)) && host()?true:reject(ERROR_REVISION_MISMATCH);}
static bool event(L4Journal* j,SetupInstallPath* p,DWORD kind){BYTE bytes[8];l4_store_u64(bytes,p->sequence);return l4_journal_append(j,kind,bytes,sizeof(bytes),NULL);}
static bool launcher_present(const wchar_t* original,const wchar_t* launcher){
    for(const wchar_t* at=original;*at;){const wchar_t* end=wcschr(at,L';');if(!end)end=at+wcslen(at);const wchar_t* start=at;size_t n=(size_t)(end-start);
        while(n && (*start==L' ' || *start==L'\t')){start++;n--;}while(n && (start[n-1]==L' ' || start[n-1]==L'\t'))n--;
        if(n>=2 && start[0]==L'"' && start[n-1]==L'"'){start++;n-=2;}while(n && start[n-1]==L'\\')n--;
        if(n==wcslen(launcher) && !_wcsnicmp(start,launcher,n))return true;at=*end?end+1:end;
    }return false;
}
void setup_path_free(SetupInstallPath* p){if(!p)return;if(p->key)RegCloseKey(p->key);free(p->old);free(p->next);free(p);}
bool setup_path_prepare(L4Journal* j,SetupInstallPath** output){
    if(!output)return reject(ERROR_INVALID_PARAMETER);*output=NULL;
    if(!j || !j->lock || j->lock==INVALID_HANDLE_VALUE || j->poisoned || !host())return reject(ERROR_ACCESS_DENIED);
    const wchar_t* launcher=j->layout.launchers;if(!*launcher || wcspbrk(launcher,L";%\r\n\""))return reject(ERROR_INVALID_NAME);
    SetupInstallPath* p=calloc(1,sizeof(*p));if(!p)return reject(ERROR_NOT_ENOUGH_MEMORY);
    LONG e=RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",0,KEY_QUERY_VALUE|KEY_SET_VALUE|KEY_WOW64_64KEY,&p->key);
    bool ok=!e && read_value(p->key,&p->old,&p->old_size,&p->old_type);if(e)SetLastError((DWORD)e);
    const wchar_t* original=p->old?(wchar_t*)p->old:L"";bool present=ok && launcher_present(original,launcher);
    if(ok){size_t n=wcslen(original), add=present?0:wcslen(launcher)+(n && original[n-1]!=L';'?1:0), total=(n+add+1)*2;
        if(total>PATH_LIMIT)ok=reject(ERROR_BUFFER_OVERFLOW);else{p->next=calloc(1,total);if(!p->next)ok=reject(ERROR_NOT_ENOUGH_MEMORY);else{
            wcscpy_s((wchar_t*)p->next,total/2,original);if(!present){if(n && original[n-1]!=L';')wcscat_s((wchar_t*)p->next,total/2,L";");wcscat_s((wchar_t*)p->next,total/2,launcher);}
            p->next_size=(DWORD)total;p->next_type=p->old_type?p->old_type:REG_EXPAND_SZ;}}
    }
    if(ok){DWORD size=24+p->old_size+p->next_size;BYTE* plan=malloc(size);if(!plan)ok=reject(ERROR_NOT_ENOUGH_MEMORY);else{
        memcpy(plan,"L4PATH01",8);l4_store_u32(plan+8,p->old_type);l4_store_u32(plan+12,p->old_size);l4_store_u32(plan+16,p->next_type);l4_store_u32(plan+20,p->next_size);
        if(p->old_size)memcpy(plan+24,p->old,p->old_size);memcpy(plan+24+p->old_size,p->next,p->next_size);ok=l4_journal_append(j,PATH_PLAN,plan,size,&p->sequence);free(plan);}}
    if(!ok){DWORD code=GetLastError();setup_path_free(p);return reject(code?code:ERROR_INVALID_DATA);}p->journal=j;p->layout=j->layout;memcpy(p->header,j->header,24);*output=p;return true;
}
bool setup_path_apply(L4Journal* j,SetupInstallPath* p){
    if(!bound(j,p) || p->undo || p->undo_intent)return reject(ERROR_INVALID_PARAMETER);if(p->done)return equal(p,true);
    if(p->intent)return reject(ERROR_RETRY);if(!equal(p,false) || !event(j,p,PATH_INTENT))return false;p->intent=true;
    if(!equal(p,false))return false;LONG e=RegSetValueExW(p->key,L"Path",0,p->next_type,p->next,p->next_size);if(e)return reject((DWORD)e);
    e=RegFlushKey(p->key);if(e)return reject((DWORD)e);if(!equal(p,true) || !event(j,p,PATH_DONE))return false;p->done=true;return true;
}
bool setup_path_rollback(L4Journal* j,SetupInstallPath* p){
    if(!bound(j,p))return false;if(p->undo)return equal(p,false);if(!p->intent)return equal(p,false);
    bool original=equal(p,false);if(!original && !equal(p,true))return false;
    if(!p->undo_intent && !event(j,p,PATH_UNDO_INTENT))return false;p->undo_intent=true;
    if(!original){if(!equal(p,true))return false;LONG e=p->old_size?RegSetValueExW(p->key,L"Path",0,p->old_type,p->old,p->old_size):RegDeleteValueW(p->key,L"Path");if(e)return reject((DWORD)e);e=RegFlushKey(p->key);if(e)return reject((DWORD)e);}
    if(!equal(p,false) || !event(j,p,PATH_UNDO_DONE))return false;p->undo=true;return true;
}
ULONGLONG setup_path_sequence(const SetupInstallPath* p){return p?p->sequence:0;}
static bool string_bytes(const BYTE* bytes,DWORD size){return size>=2 && size<=PATH_LIMIT && !(size%2) && ((const wchar_t*)bytes)[size/2-1]==0 && wcslen((const wchar_t*)bytes)==size/2-1;}
static bool replay_path(DWORD kind,ULONGLONG seq,const void* bytes,DWORD size,void* context){
    SetupInstallPath* p=context;if(kind<PATH_INTENT || kind>PATH_UNDO_DONE)return true;
    if(size!=8)return reject(ERROR_INVALID_DATA);if(l4_store_get64(bytes)!=p->sequence)return true;
    if(seq<=p->sequence)return reject(ERROR_INVALID_DATA);
    if(kind==PATH_INTENT){if(p->intent || p->undo_intent)return reject(ERROR_INVALID_DATA);p->intent=true;}
    if(kind==PATH_DONE){if(!p->intent || p->done || p->undo_intent)return reject(ERROR_INVALID_DATA);p->done=true;}
    if(kind==PATH_UNDO_INTENT){if(!p->intent || p->undo_intent)return reject(ERROR_INVALID_DATA);p->undo_intent=true;}
    if(kind==PATH_UNDO_DONE){if(!p->undo_intent || p->undo)return reject(ERROR_INVALID_DATA);p->undo=true;}
    return true;
}
bool setup_path_load(L4Journal* j,ULONGLONG sequence,SetupInstallPath** output){
    if(!output)return reject(ERROR_INVALID_PARAMETER);*output=NULL;
    if(!j || !sequence || !j->lock || j->lock==INVALID_HANDLE_VALUE || j->poisoned || !host())return reject(ERROR_ACCESS_DENIED);
    BYTE* bytes=NULL;DWORD size=0;if(!l4_store_find_record(j,PATH_PLAN,sequence,&bytes,&size))return false;
    SetupInstallPath* p=calloc(1,sizeof(*p));if(!p){free(bytes);return reject(ERROR_NOT_ENOUGH_MEMORY);}
    bool ok=size>=26 && !memcmp(bytes,"L4PATH01",8);
    if(ok){p->old_type=l4_store_get32(bytes+8);p->old_size=l4_store_get32(bytes+12);p->next_type=l4_store_get32(bytes+16);p->next_size=l4_store_get32(bytes+20);
        ok=p->old_size<=PATH_LIMIT && p->next_size<=PATH_LIMIT && size==24+p->old_size+p->next_size &&
            ((!p->old_size && !p->old_type) || ((p->old_type==REG_SZ || p->old_type==REG_EXPAND_SZ) && string_bytes(bytes+24,p->old_size))) &&
            (p->next_type==REG_SZ || p->next_type==REG_EXPAND_SZ) && string_bytes(bytes+24+p->old_size,p->next_size) && p->next_type==(p->old_type?p->old_type:REG_EXPAND_SZ);
    }
    if(ok){if(p->old_size){p->old=malloc(p->old_size);if(p->old)memcpy(p->old,bytes+24,p->old_size);}p->next=malloc(p->next_size);
        ok=(!p->old_size || p->old) && p->next;if(ok)memcpy(p->next,bytes+24+p->old_size,p->next_size);}
    free(bytes);p->sequence=sequence;p->journal=j;p->layout=j->layout;memcpy(p->header,j->header,24);
    if(ok)ok=l4_journal_replay(j,replay_path,p);
    if(ok){const wchar_t* next=(const wchar_t*)p->next;const wchar_t* old=p->old?(const wchar_t*)p->old:L"";size_t n=wcslen(old),next_n=wcslen(next),launcher=wcslen(j->layout.launchers);
        /* Only exact unchanged value or append of this original PF/bin is valid. */
        bool append=n==0?next_n==launcher && !wcscmp(next,j->layout.launchers):
            next_n==n+launcher+(old[n-1]==L';'?0:1) && !wcsncmp(next,old,n) &&
            (old[n-1]==L';' || next[n]==L';') && !wcscmp(next+n+(old[n-1]==L';'?0:1),j->layout.launchers);
        bool present=launcher_present(old,j->layout.launchers);ok=present?!wcscmp(old,next):append;
    }
    if(ok){LONG e=RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",0,KEY_QUERY_VALUE|KEY_SET_VALUE|KEY_WOW64_64KEY,&p->key);ok=!e;if(e)SetLastError((DWORD)e);}
    if(!ok){DWORD code=GetLastError();setup_path_free(p);return reject(code?code:ERROR_INVALID_DATA);}*output=p;return true;
}
