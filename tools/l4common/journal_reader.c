#include "journal_reader_internal.h"
#include "journal_internal.h"
#include <objbase.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define HEADER 24u
#define FRAME 48u
#define LIMIT (8u*1024u*1024u)
#define END 0xc04d17edu
struct L4JournalReader {L4Journal view;L4FileFence roots;BYTE* snapshot;DWORD size;bool live;};
bool l4_journal_reader_is_immutable(const L4JournalReader* reader){return reader&&!reader->live;}
static bool uuid(const wchar_t* id,GUID* guid){if(!id || wcslen(id)!=36)return false;for(unsigned i=0;i<36;i++){bool dash=i==8||i==13||i==18||i==23;if(dash?id[i]!=L'-':!((id[i]>=L'0'&&id[i]<=L'9')||(id[i]>=L'a'&&id[i]<=L'f')))return false;}wchar_t text[40];GUID zero={0};swprintf_s(text,40,L"{%ls}",id);return SUCCEEDED(CLSIDFromString(text,guid))&&memcmp(guid,&zero,sizeof(zero));}
void l4_journal_reader_close(L4JournalReader* r){if(!r)return;if(r->view.file && r->view.file!=INVALID_HANDLE_VALUE)CloseHandle(r->view.file);l4_store_unpin(&r->view.fence);l4_store_unpin(&r->roots);free(r->snapshot);free(r);}
static bool validate(L4JournalReader* r){BYTE digest[32];if(!l4_store_hash(r->snapshot,HEADER,NULL,0,digest))return false;DWORD at=HEADER;ULONGLONG seq=0;
 while(at<r->size){DWORD remain=r->size-at;if(remain<FRAME+4)return l4_store_fail(r->live?ERROR_IO_PENDING:ERROR_INVALID_DATA);const BYTE* f=r->snapshot+at;DWORD kind=l4_store_get32(f),size=l4_store_get32(f+4);ULONGLONG number=l4_store_get64(f+8);
 if(!kind||size>L4_JOURNAL_MAX_RECORD||number!=seq+1)return l4_store_fail(ERROR_INVALID_DATA);
 if(size>remain-FRAME-4)return l4_store_fail(r->live?ERROR_IO_PENDING:ERROR_INVALID_DATA);
 BYTE prefix[48],actual[32];memcpy(prefix,f,16);memcpy(prefix+16,digest,32);
 if(l4_store_get32(f+FRAME+size)!=END || !l4_store_hash(prefix,48,f+FRAME,size,actual)||memcmp(actual,f+16,32))return l4_store_fail(ERROR_CRC);
 memcpy(digest,actual,32);seq=number;at+=FRAME+size+4;
 }r->view.sequence=seq;r->view.end=at;memcpy(r->view.digest,digest,32);return true;
}
static bool snapshot(const L4Layout* layout,const wchar_t* id,const GUID* identity,HANDLE lock,bool live,L4JournalReader** result){ GUID guid=*identity;
 L4JournalReader* r=calloc(1,sizeof(*r));if(!r)return l4_store_fail(ERROR_NOT_ENOUGH_MEMORY);r->view.layout=*layout;r->view.file=INVALID_HANDLE_VALUE;r->view.lock=lock;r->live=live;
 bool ok=l4_store_pin(layout->operations,layout->data,false,&r->roots);wchar_t relative[96],path[MAX_PATH];swprintf_s(relative,96,L"update\\operations\\%ls",id);
 if(ok)ok=l4_layout_data_path(layout,relative,r->view.directory)&&l4_store_pin(r->view.directory,r->view.directory,true,&r->view.fence);
 if(ok)ok=swprintf_s(path,MAX_PATH,L"%ls\\journal.bin",r->view.directory)>0;
 if(ok){r->view.file=CreateFileW(path,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ|(live?FILE_SHARE_WRITE:0),NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);ok=r->view.file!=INVALID_HANDLE_VALUE;}
 LARGE_INTEGER size={0};BYTE* sd=NULL;DWORD sd_size=0;BY_HANDLE_FILE_INFORMATION info;if(ok)ok=GetFileInformationByHandle(r->view.file,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&l4_store_security(r->view.file,true,&sd,&sd_size)&&GetFileSizeEx(r->view.file,&size)&&size.QuadPart>=HEADER&&size.QuadPart<=LIMIT;free(sd);
 if(ok){r->size=(DWORD)size.QuadPart;r->snapshot=malloc(r->size);ok=r->snapshot!=NULL;}DWORD read=0;
 if(ok)ok=ReadFile(r->view.file,r->snapshot,r->size,&read,NULL)&&read==r->size;
 LARGE_INTEGER after={0};if(ok && live && (!GetFileSizeEx(r->view.file,&after)||after.QuadPart!=size.QuadPart)){ok=false;SetLastError(ERROR_IO_PENDING);}
 if(ok){memcpy(r->view.header,"L4J1",4);l4_store_u32(r->view.header+4,1);memcpy(r->view.header+8,&guid,16);ok=!memcmp(r->view.header,r->snapshot,HEADER)&&validate(r);}
 DWORD code=GetLastError();if(!ok){l4_journal_reader_close(r);return l4_store_fail(code?code:ERROR_INVALID_DATA);}*result=r;return true;
}
bool l4_journal_reader_open(L4Journal* owner,const wchar_t* id,L4JournalReader** result){if(!result)return l4_store_fail(ERROR_INVALID_PARAMETER);*result=NULL;GUID guid;
 if(!owner||owner->poisoned||!owner->lock||owner->lock==INVALID_HANDLE_VALUE||!owner->file||owner->file==INVALID_HANDLE_VALUE||!uuid(id,&guid))return l4_store_fail(ERROR_ACCESS_DENIED);
 const wchar_t* current=wcsrchr(owner->directory,L'\\');if(!current||!_wcsicmp(current+1,id))return l4_store_fail(ERROR_INVALID_PARAMETER);
 BYTE* sd=NULL;DWORD sd_size=0;BY_HANDLE_FILE_INFORMATION info;
 if(!GetFileInformationByHandle(owner->lock,&info)||info.nNumberOfLinks!=1||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||!l4_store_security(owner->lock,true,&sd,&sd_size)){free(sd);return false;}free(sd);
 wchar_t expected[MAX_PATH],actual[MAX_PATH+4];if(!l4_layout_data_path(&owner->layout,L"update\\operations\\deployment.lock",expected))return false;
 DWORD n=GetFinalPathNameByHandleW(owner->lock,actual,_countof(actual),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);if(!n||n>=_countof(actual)||wcsncmp(actual,L"\\\\?\\",4)||_wcsicmp(actual+4,expected))return l4_store_fail(ERROR_ACCESS_DENIED);
 return snapshot(&owner->layout,id,&guid,owner->lock,false,result);
}
bool l4_journal_reader_replay(const L4JournalReader* r,L4JournalVisitor visit,void* context){if(!r||!visit)return l4_store_fail(ERROR_INVALID_PARAMETER);for(DWORD at=HEADER;at<r->size;){const BYTE* f=r->snapshot+at;DWORD n=l4_store_get32(f+4);if(!visit(l4_store_get32(f),l4_store_get64(f+8),f+FRAME,n,context))return false;at+=FRAME+n+4;}return true;}
typedef struct{DWORD kind;ULONGLONG seq;BYTE* bytes;DWORD size;} Find;
static bool found(DWORD kind,ULONGLONG seq,const void* bytes,DWORD size,void* context){Find* f=context;if(kind==f->kind&&seq==f->seq){f->bytes=malloc(size?size:1);if(!f->bytes)return l4_store_fail(ERROR_NOT_ENOUGH_MEMORY);memcpy(f->bytes,bytes,size);f->size=size;}return true;}
bool l4_journal_reader_find(const L4JournalReader* r,DWORD kind,ULONGLONG seq,BYTE** bytes,DWORD* size){if(!bytes||!size)return l4_store_fail(ERROR_INVALID_PARAMETER);*bytes=NULL;*size=0;Find f={kind,seq,NULL,0};if(!l4_journal_reader_replay(r,found,&f)){free(f.bytes);return false;}if(!f.bytes)return l4_store_fail(ERROR_NOT_FOUND);*bytes=f.bytes;*size=f.size;return true;}
const wchar_t* l4_journal_reader_directory(const L4JournalReader* r){return r?r->view.directory:NULL;}
L4Journal* l4_journal_reader_codec_view(L4JournalReader* r){return r&&!r->live?&r->view:NULL;}

bool l4_journal_reader_open_live(const L4Layout* layout,const wchar_t* id,L4JournalReader** result){if(!result)return l4_store_fail(ERROR_INVALID_PARAMETER);*result=NULL;GUID guid;
 if(!layout||!uuid(id,&guid))return l4_store_fail(ERROR_INVALID_NAME);HANDLE thread=NULL,token=NULL;BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD n=0;
 if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread)){CloseHandle(thread);return l4_store_fail(ERROR_ACCESS_DENIED);}if(GetLastError()!=ERROR_NO_TOKEN)return false;
 bool system=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)&&GetTokenInformation(token,TokenUser,user,sizeof(user),&n)&&IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid);if(token)CloseHandle(token);if(!system)return l4_store_fail(ERROR_ACCESS_DENIED);
 const wchar_t* version=wcsrchr(layout->release,L'\\');L4Layout expected;if(!version||!l4_layout_resolve(&expected,version+1)||memcmp(layout,&expected,sizeof(expected)))return l4_store_fail(ERROR_ACCESS_DENIED);
 return snapshot(layout,id,&guid,INVALID_HANDLE_VALUE,true,result);
}
bool l4_journal_reader_open_fixed_immutable_reference(const L4Layout* layout,const wchar_t* id,L4JournalReader** result){
 if(!result)return l4_store_fail(ERROR_INVALID_PARAMETER);*result=NULL;GUID guid;
 if(!layout||!uuid(id,&guid))return l4_store_fail(ERROR_INVALID_NAME);HANDLE thread=NULL,token=NULL;BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD n=0;
 if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread)){CloseHandle(thread);return l4_store_fail(ERROR_ACCESS_DENIED);}if(GetLastError()!=ERROR_NO_TOKEN)return false;
 DWORD session=1;bool system=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)&&GetTokenInformation(token,TokenUser,user,sizeof(user),&n)&&
  IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid)&&GetTokenInformation(token,TokenSessionId,&session,sizeof(session),&n)&&session==0;
 if(token)CloseHandle(token);if(!system)return l4_store_fail(ERROR_ACCESS_DENIED);
 const wchar_t* version=wcsrchr(layout->release,L'\\');L4Layout expected;
 if(!version||!l4_layout_resolve(&expected,version+1)||memcmp(layout,&expected,sizeof(expected)))return l4_store_fail(ERROR_ACCESS_DENIED);
 return snapshot(layout,id,&guid,INVALID_HANDLE_VALUE,false,result);
}
