#include "active_updater.h"
#include "journal_internal.h"
#include "journal_reader_internal.h"
#include "bootstrap_history.h"
#include "metadata.h"
#include "../leo4proxy/src/policy_json.h"
#include <bcrypt.h>
#include <sddl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
struct L4ActiveUpdater {L4Layout roots;L4ActiveUpdaterInfo info;wchar_t path[MAX_PATH];HANDLE pointer,image,root,signature;L4FileFence state,ancestry,inputs,installer;L4JournalReader* history;};
static bool fail(DWORD code){SetLastError(code);return false;}
static bool any(const BYTE* b){BYTE v=0;for(unsigned i=0;i<32;i++)v|=b[i];return v!=0;}
static bool uuid(const char* s){if(!s||strlen(s)!=36)return false;bool nz=false;for(unsigned i=0;i<36;i++){
 if(i==8||i==13||i==18||i==23){if(s[i]!='-')return false;}else if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;else nz|=s[i]!='0';}return nz;}
static bool version(const char* text){wchar_t wide[32];L4Layout roots;return text&&strlen(text)<32&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,wide,32)&&l4_layout_from_roots(&roots,L"C:\\L4ActiveFixture\\Programs",L"C:\\L4ActiveFixture\\Data",wide);}
static bool fields(const PolicyJson* j,int at,unsigned count){if(at<0||j->tokens[at].type!='{')return false;unsigned actual=0;for(int n=at+1;n<j->tokens[at].next;n=j->tokens[n+1].next)actual++;return actual==count;}
static bool text(const PolicyJson* j,int at,const char* key,char* out,size_t capacity){return policy_json_string(j,policy_json_field(j,at,key),out,capacity);}
static bool digest(const PolicyJson* j,int at,const char* key,BYTE out[32]){char s[65];if(!text(j,at,key,s,65)||strlen(s)!=64)return false;
 for(unsigned i=0;i<32;i++){unsigned a=(unsigned char)s[2*i],b=(unsigned char)s[2*i+1];a=a>='0'&&a<='9'?a-'0':a>='a'&&a<='f'?a-'a'+10:16;b=b>='0'&&b<='9'?b-'0':b>='a'&&b<='f'?b-'a'+10:16;if(a>15||b>15)return false;out[i]=(BYTE)((a<<4)|b);}return any(out);}
bool l4_active_updater_decode(const void* bytes,DWORD size,L4ActiveUpdaterInfo* out){if(out)memset(out,0,sizeof(*out));if(!bytes||!out||!size||size>512)return fail(ERROR_INVALID_PARAMETER);
 PolicyJson j;ULONGLONG schema;L4ActiveUpdaterInfo result={0};
 if(!policy_json_parse(&j,bytes,size)||!fields(&j,0,5)||!policy_json_uint(&j,policy_json_field(&j,0,"schema"),&schema)||schema!=1||
  !text(&j,0,"version",result.version,32)||!version(result.version)||!text(&j,0,"arch",result.arch,8)||(strcmp(result.arch,"x86")&&strcmp(result.arch,"x64"))||
  !text(&j,0,"origin",result.origin,37)||!uuid(result.origin)||!digest(&j,0,"root_sha256",result.root_sha256))return fail(ERROR_INVALID_DATA);
 *out=result;return true;
}
bool l4_active_updater_encode(const L4ActiveUpdaterInfo* info,char* bytes,DWORD capacity,DWORD* size){if(size)*size=0;if(!info||!bytes||!size||!capacity||
 !memchr(info->version,0,32)||!memchr(info->arch,0,8)||!memchr(info->origin,0,37)||!version(info->version)||!uuid(info->origin)||(strcmp(info->arch,"x86")&&strcmp(info->arch,"x64"))||!any(info->root_sha256))return fail(ERROR_INVALID_PARAMETER);
 char hex[65];for(unsigned i=0;i<32;i++)sprintf_s(hex+2*i,65-2*i,"%02x",info->root_sha256[i]);int n=snprintf(bytes,capacity,"{\"schema\":1,\"version\":\"%s\",\"arch\":\"%s\",\"origin\":\"%s\",\"root_sha256\":\"%s\"}",info->version,info->arch,info->origin,hex);
 if(n<0||(DWORD)n>=capacity)return fail(ERROR_INSUFFICIENT_BUFFER);*size=(DWORD)n;return true;
}
static bool system_roots(const L4Layout* roots){HANDLE thread=NULL,token=NULL;BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD n=0,session=1;
 if(!roots)return fail(ERROR_INVALID_PARAMETER);if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread)){CloseHandle(thread);return fail(ERROR_ACCESS_DENIED);}if(GetLastError()!=ERROR_NO_TOKEN)return false;
 const wchar_t* v=wcsrchr(roots->release,L'\\');L4Layout actual;bool ok=v&&l4_layout_resolve(&actual,v+1)&&!memcmp(&actual,roots,sizeof(actual))&&OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)&&
  GetTokenInformation(token,TokenUser,user,sizeof(user),&n)&&IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid)&&GetTokenInformation(token,TokenSessionId,&session,sizeof(session),&n)&&session==0;
 if(token)CloseHandle(token);return ok?true:fail(ERROR_ACCESS_DENIED);
}
static bool safe_file(HANDLE h,bool private_file){BY_HANDLE_FILE_INFORMATION info;BYTE* sd=NULL;DWORD size=0;bool ok=GetFileInformationByHandle(h,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&l4_store_security(h,private_file,&sd,&size);free(sd);return ok;}
static bool read_bytes(HANDLE h,DWORD maximum,BYTE** bytes,DWORD* size){*bytes=NULL;*size=0;LARGE_INTEGER length,zero={0};DWORD got=0;
 if(!safe_file(h,true)||!GetFileSizeEx(h,&length)||length.QuadPart<=0||length.QuadPart>maximum||!SetFilePointerEx(h,zero,NULL,FILE_BEGIN))return fail(ERROR_INVALID_DATA);
 BYTE* b=malloc((size_t)length.QuadPart);if(!b)return fail(ERROR_NOT_ENOUGH_MEMORY);bool ok=ReadFile(h,b,(DWORD)length.QuadPart,&got,NULL)&&got==(DWORD)length.QuadPart;if(!ok){free(b);return false;}*bytes=b;*size=got;return true;}
static HANDLE open_fixed(const wchar_t* directory,const wchar_t* name){wchar_t path[MAX_PATH];if(swprintf_s(path,MAX_PATH,L"%ls\\%ls",directory,name)<0){SetLastError(ERROR_FILENAME_EXCED_RANGE);return INVALID_HANDLE_VALUE;}return CreateFileW(path,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);}
static bool held_image(L4ActiveUpdater* a){if(!safe_file(a->image,false))return fail(ERROR_ACCESS_DENIED);LARGE_INTEGER size,zero={0};if(!GetFileSizeEx(a->image,&size)||size.QuadPart<=0||(ULONGLONG)size.QuadPart!=a->info.image_size||!SetFilePointerEx(a->image,zero,NULL,FILE_BEGIN))return fail(ERROR_INVALID_DATA);
 BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;BYTE block[32768],actual[32];DWORD got=0;ULONGLONG total=0;
 bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)==0&&BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)==0;
 while(ok){ok=ReadFile(a->image,block,sizeof(block),&got,NULL)!=0;if(!ok||!got)break;total+=got;ok=total<=a->info.image_size&&BCryptHashData(hash,block,got,0)==0;}
 if(ok)ok=total==a->info.image_size&&BCryptFinishHash(hash,actual,32,0)==0&&!memcmp(actual,a->info.image_sha256,32);if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok?true:fail(ERROR_CRC);
}
typedef struct {ULONGLONG fresh,bootstrap;} History;
static bool records(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){(void)bytes;(void)size;History* h=context;
 if(kind==85){if(h->fresh)return fail(ERROR_INVALID_DATA);h->fresh=sequence;}else if(kind==40){if(h->bootstrap)return fail(ERROR_INVALID_DATA);h->bootstrap=sequence;}else if(kind==92||kind==102)return fail(ERROR_INVALID_DATA);return true;}
static bool asset(const PolicyJson* j,int files,const char* name,ULONGLONG limit,ULONGLONG* size,BYTE sha[32]){int at=policy_json_field(j,files,name);return fields(j,at,2)&&policy_json_uint(j,policy_json_field(j,at,"size"),size)&&*size&&*size<=limit&&digest(j,at,"sha256",sha);}
static bool root_claim(L4ActiveUpdater* a,const BYTE* bytes,DWORD size,const BYTE* signature,DWORD signature_size,const BYTE* receipt,DWORD receipt_size){
 if(!a||!bytes||!signature||!receipt||receipt_size<68)return fail(ERROR_INVALID_DATA);
 DWORD receipt_count=l4_store_get32(receipt+56),receipt_descriptor=l4_store_get32(receipt+60),receipt_signature=l4_store_get32(receipt+64);
 if(receipt_count<13||receipt_count>15||!receipt_descriptor||receipt_descriptor>65535||receipt_signature!=384||receipt_size!=68+receipt_count*8+receipt_descriptor+receipt_signature)return fail(ERROR_INVALID_DATA);
 BYTE root[32];if(!l4_metadata_verify_trusted(bytes,size,signature,signature_size,root)||memcmp(root,a->info.root_sha256,32)||receipt_size<68||memcmp(root,receipt+8,32))return fail(ERROR_CRC);
 PolicyJson j;ULONGLONG schema;bool signed_ok,dirty;char v[32],status[16],minimum[8];
 if(!policy_json_parse(&j,(const char*)bytes,size)||!policy_json_uint(&j,policy_json_field(&j,0,"schema"),&schema)||schema!=1||!text(&j,0,"version",v,32)||strcmp(v,a->info.version)||
  !policy_json_bool(&j,policy_json_field(&j,0,"signed"),&signed_ok)||!signed_ok||!policy_json_bool(&j,policy_json_field(&j,0,"dirty"),&dirty)||dirty||
  !text(&j,0,"signature_status",status,16)||strcmp(status,"Valid")||!text(&j,0,"min_os",minimum,8)||strcmp(minimum,"6.1")||!digest(&j,0,"publisher_certificate_sha256",a->info.publisher))return fail(ERROR_INVALID_DATA);
 int architectures=policy_json_field(&j,0,"arch");unsigned mask=0;if(architectures<0||j.tokens[architectures].type!='[')return fail(ERROR_INVALID_DATA);
 for(int n=architectures+1;n<j.tokens[architectures].next;n=j.tokens[n].next){char arch[8];if(!policy_json_string(&j,n,arch,8))return fail(ERROR_INVALID_DATA);unsigned bit=!strcmp(arch,"x86")?1u:!strcmp(arch,"x64")?2u:0;if(!bit||(mask&bit))return fail(ERROR_INVALID_DATA);mask|=bit;}if(mask!=3)return fail(ERROR_INVALID_DATA);
 int files=policy_json_field(&j,0,"files");char name[48];ULONGLONG descriptor_size,detached_size;BYTE descriptor_sha[32],detached_sha[32],actual[32];
 if(!asset(&j,files,"l4setup.exe",1024ULL*1024*1024,&a->info.image_size,a->info.image_sha256))return fail(ERROR_INVALID_DATA);
 sprintf_s(name,48,"l4tools-layout-%s.json",a->info.arch);if(!asset(&j,files,name,65535,&descriptor_size,descriptor_sha))return fail(ERROR_INVALID_DATA);
 sprintf_s(name,48,"l4tools-layout-%s.json.sig",a->info.arch);if(!asset(&j,files,name,384,&detached_size,detached_sha)||detached_size!=384)return fail(ERROR_INVALID_DATA);
 DWORD count=l4_store_get32(receipt+56),d=l4_store_get32(receipt+60),s=l4_store_get32(receipt+64);const BYTE* descriptor=receipt+68+count*8;
 if(d!=descriptor_size||s!=detached_size||!l4_store_hash(descriptor,d,NULL,0,actual)||memcmp(actual,descriptor_sha,32)||!l4_store_hash(descriptor+d,s,NULL,0,actual)||memcmp(actual,detached_sha,32)||
  !l4_metadata_verify_trusted(descriptor,d,descriptor+d,s,actual)||memcmp(actual,descriptor_sha,32))return fail(ERROR_CRC);return true;
}
static bool pin_inputs(L4ActiveUpdater* a,const L4Journal* view,wchar_t input[MAX_PATH]){
 /* Public read on ProgramData ancestry is separate from private operation
  * inputs. Both fences stay held through metadata/image admission. */
 return swprintf_s(input,MAX_PATH,L"%ls\\inputs",view->directory)>0&&l4_store_pin(view->directory,a->roots.data,false,&a->ancestry)&&l4_store_pin(input,view->directory,true,&a->inputs);
}
static bool authenticate_history(L4ActiveUpdater* a,L4Journal* view){History h={0};if(!l4_journal_replay(view,records,&h)||!h.fresh||!h.bootstrap||h.bootstrap>=h.fresh)return fail(ERROR_INVALID_DATA);
 L4BootstrapPlan plan;bool committed=false,aborted=false;if(!l4_bootstrap_load(view,h.bootstrap,&plan)||!l4_bootstrap_terminal(view,h.bootstrap,&committed,&aborted)||!committed||aborted)return fail(ERROR_INVALID_DATA);
 const wchar_t* tail=wcsrchr(plan.layout.release,L'\\');char selected[32];if(!tail||!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,tail+1,-1,selected,32,NULL,NULL)||strcmp(selected,a->info.version))return fail(ERROR_REVISION_MISMATCH);
 BYTE *receipt=NULL,*root=NULL,*signature=NULL;DWORD n=0,rn=0,sn=0;bool ok=l4_store_find_record(view,85,h.fresh,&receipt,&n)&&n>=68&&!memcmp(receipt,"L4FRSH01",8)&&l4_store_get64(receipt+40)==h.bootstrap&&l4_store_get64(receipt+48)&&l4_store_get64(receipt+48)<h.fresh;
 DWORD count=ok?l4_store_get32(receipt+56):0,d=ok?l4_store_get32(receipt+60):0,s=ok?l4_store_get32(receipt+64):0;
 if(ok)ok=count>=13&&count<=15&&d&&d<=65535&&s==384&&n==68+count*8+d+s;
 for(unsigned i=0;ok&&i<count;i++){ULONGLONG seq=l4_store_get64(receipt+68+i*8);BYTE* b=NULL;DWORD length=0;ok=seq&&seq<h.bootstrap;for(unsigned k=0;ok&&k<i;k++)ok=seq!=l4_store_get64(receipt+68+k*8);if(ok)ok=l4_store_find_record(view,20,seq,&b,&length);free(b);}
 if(ok){BYTE* path=NULL;DWORD path_size=0;ok=l4_store_find_record(view,80,l4_store_get64(receipt+48),&path,&path_size)&&path_size>=26&&!memcmp(path,"L4PATH01",8);
  if(ok){DWORD old_type=l4_store_get32(path+8),old_size=l4_store_get32(path+12),new_type=l4_store_get32(path+16),new_size=l4_store_get32(path+20);
   ok=old_size<=65534&&new_size>=2&&new_size<=65534&&!(old_size%2)&&!(new_size%2)&&path_size==24+old_size+new_size&&
    ((!old_size&&!old_type)||((old_type==REG_SZ||old_type==REG_EXPAND_SZ)&&old_size>=2&&wcsnlen_s((wchar_t*)(path+24),old_size/2)==old_size/2-1))&&
    (new_type==REG_SZ||new_type==REG_EXPAND_SZ)&&new_type==(old_type?old_type:REG_EXPAND_SZ)&&wcsnlen_s((wchar_t*)(path+24+old_size),new_size/2)==new_size/2-1;
  }free(path);
 }
 wchar_t input[MAX_PATH];if(ok)ok=pin_inputs(a,view,input);
 if(ok){a->root=open_fixed(input,L"l4tools-release.json");a->signature=open_fixed(input,L"l4tools-release.json.sig");ok=a->root!=INVALID_HANDLE_VALUE&&a->signature!=INVALID_HANDLE_VALUE&&read_bytes(a->root,65535,&root,&rn)&&read_bytes(a->signature,384,&signature,&sn)&&root_claim(a,root,rn,signature,sn,receipt,n);}
 free(receipt);free(root);free(signature);if(!ok)return false;wchar_t installer[MAX_PATH];if(swprintf_s(installer,MAX_PATH,L"%ls\\setup\\%hs",a->roots.binaries,a->info.version)<0)return fail(ERROR_FILENAME_EXCED_RANGE);
 if(!l4_store_pin(installer,a->roots.binaries,false,&a->installer)||swprintf_s(a->path,MAX_PATH,L"%ls\\l4setup.exe",installer)<0)return false;
 a->image=open_fixed(installer,L"l4setup.exe");return a->image!=INVALID_HANDLE_VALUE&&held_image(a);
}
void l4_active_updater_close(L4ActiveUpdater* a){if(!a)return;DWORD error=GetLastError();HANDLE files[]={a->pointer,a->image,a->root,a->signature};for(unsigned i=0;i<4;i++)if(files[i]&&files[i]!=INVALID_HANDLE_VALUE)CloseHandle(files[i]);l4_store_unpin(&a->state);l4_store_unpin(&a->inputs);l4_store_unpin(&a->ancestry);l4_store_unpin(&a->installer);l4_journal_reader_close(a->history);free(a);SetLastError(error);}
bool l4_active_updater_open_fixed(const L4Layout* roots,L4ActiveUpdater** out){if(out)*out=NULL;if(!out||!system_roots(roots))return false;
 L4ActiveUpdater* a=calloc(1,sizeof(*a));if(!a)return fail(ERROR_NOT_ENOUGH_MEMORY);a->roots=*roots;BYTE* pointer=NULL;DWORD n=0;
 bool ok=l4_store_pin(roots->state,roots->data,false,&a->state);if(ok){a->pointer=open_fixed(roots->state,L"updater-active.json");ok=a->pointer!=INVALID_HANDLE_VALUE&&read_bytes(a->pointer,512,&pointer,&n)&&l4_active_updater_decode(pointer,n,&a->info);}free(pointer);
 wchar_t origin[37];if(ok)ok=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,a->info.origin,-1,origin,37)&&l4_journal_reader_open_fixed_immutable_reference(roots,origin,&a->history)&&l4_journal_reader_is_immutable(a->history);
 L4Journal* view=ok?l4_journal_reader_codec_view(a->history):NULL;if(ok)ok=view&&authenticate_history(a,view);DWORD error=GetLastError();if(!ok){l4_active_updater_close(a);return fail(error?error:ERROR_INVALID_DATA);}*out=a;return true;
}
bool l4_active_updater_open(L4Journal* owner,L4ActiveUpdater** out){if(out)*out=NULL;if(!out||!owner||owner->poisoned||!owner->lock||owner->lock==INVALID_HANDLE_VALUE||!owner->file||owner->file==INVALID_HANDLE_VALUE)return fail(ERROR_ACCESS_DENIED);
 L4ActiveUpdater* a=NULL;if(!l4_active_updater_open_fixed(&owner->layout,&a))return false;wchar_t origin[37];L4JournalReader* bound=NULL;
 bool ok=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,a->info.origin,-1,origin,37)&&l4_journal_reader_open(owner,origin,&bound);DWORD error=GetLastError();l4_journal_reader_close(bound);if(!ok){l4_active_updater_close(a);return fail(error);}*out=a;return true;
}
bool l4_active_updater_verify(L4ActiveUpdater* a){if(!a||!system_roots(&a->roots))return false;BYTE* b=NULL;DWORD n=0;L4ActiveUpdaterInfo pointer={0};bool ok=read_bytes(a->pointer,512,&b,&n)&&l4_active_updater_decode(b,n,&pointer)&&!strcmp(pointer.version,a->info.version)&&!strcmp(pointer.arch,a->info.arch)&&!strcmp(pointer.origin,a->info.origin)&&!memcmp(pointer.root_sha256,a->info.root_sha256,32)&&held_image(a);free(b);return ok?true:fail(ERROR_CRC);}
const L4ActiveUpdaterInfo* l4_active_updater_info(const L4ActiveUpdater* a){return a?&a->info:NULL;}
HANDLE l4_active_updater_file(const L4ActiveUpdater* a){return a?a->image:INVALID_HANDLE_VALUE;}
const wchar_t* l4_active_updater_path(const L4ActiveUpdater* a){return a?a->path:NULL;}
bool l4_active_updater_initialize_fresh(L4Journal* fresh,const char* arch){
 if(!fresh||fresh->poisoned||!fresh->lock||fresh->lock==INVALID_HANDLE_VALUE||!fresh->file||fresh->file==INVALID_HANDLE_VALUE||!arch||(strcmp(arch,"x86")&&strcmp(arch,"x64"))||!system_roots(&fresh->layout))return fail(ERROR_ACCESS_DENIED);
 L4ActiveUpdater* a=calloc(1,sizeof(*a));if(!a)return fail(ERROR_NOT_ENOUGH_MEMORY);a->roots=fresh->layout;const wchar_t* id=wcsrchr(fresh->directory,L'\\'),*v=wcsrchr(fresh->layout.release,L'\\');History h={0};BYTE* receipt=NULL;DWORD rn=0;
 bool ok=id&&v&&WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,id+1,-1,a->info.origin,37,NULL,NULL)&&uuid(a->info.origin)&&WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,v+1,-1,a->info.version,32,NULL,NULL)&&version(a->info.version)&&l4_journal_replay(fresh,records,&h)&&h.fresh&&l4_store_find_record(fresh,85,h.fresh,&receipt,&rn)&&rn>=40;
 strcpy_s(a->info.arch,8,arch);if(ok)memcpy(a->info.root_sha256,receipt+8,32);free(receipt);if(ok)ok=authenticate_history(a,fresh)&&l4_store_pin(a->roots.state,a->roots.data,false,&a->state);
 char json[512];DWORD n=0;if(ok)ok=l4_active_updater_encode(&a->info,json,sizeof(json),&n);
 wchar_t path[MAX_PATH],temporary[MAX_PATH];if(ok)ok=swprintf_s(path,MAX_PATH,L"%ls\\updater-active.json",a->roots.state)>0&&swprintf_s(temporary,MAX_PATH,L"%ls\\updater-active-%hs.tmp",a->roots.state,a->info.origin)>0;
 if(ok){HANDLE existing=CreateFileW(path,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
  if(existing!=INVALID_HANDLE_VALUE){BYTE* b=NULL;DWORD count=0;ok=read_bytes(existing,512,&b,&count)&&count==n&&!memcmp(b,json,n);free(b);CloseHandle(existing);if(!ok)SetLastError(ERROR_ALREADY_EXISTS);}
  else if(GetLastError()!=ERROR_FILE_NOT_FOUND)ok=false;
  else{PSECURITY_DESCRIPTOR sd=NULL;ok=ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:SYG:SYD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,NULL);SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE};HANDLE file=ok?CreateFileW(temporary,GENERIC_READ|GENERIC_WRITE|READ_CONTROL,0,&sa,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,NULL):INVALID_HANDLE_VALUE;
   if(file==INVALID_HANDLE_VALUE)ok=false;else{ok=safe_file(file,true)&&l4_store_write(file,json,n)&&FlushFileBuffers(file);CloseHandle(file);if(ok)ok=MoveFileExW(temporary,path,MOVEFILE_WRITE_THROUGH)!=0;}
   if(sd)LocalFree(sd);
  }
 }DWORD error=GetLastError();l4_active_updater_close(a);return ok?true:fail(error?error:ERROR_INVALID_DATA);
}
