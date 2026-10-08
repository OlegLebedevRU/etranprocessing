#include "remote_config.h"
#include "broker_config.h"
#include "../../l4common/launcher.h"
#include "../../l4common/journal_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static const wchar_t* const relatives[]={L"l4superv.json",L"mosquitto\\mosquitto.conf",L"mosquitto\\acl.conf",L"launchers\\leo4proxy.target",L"launchers\\mosquitto.target",L"launchers\\l4con.target",L"launchers\\l4superv.target",L"launchers\\l4desk.target",L"launchers\\l4sql.target",L"launchers\\l4pin.target",L"launchers\\l4capture.target",L"launchers\\ffmpeg.target"};
static const wchar_t* const tools[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv",L"l4desk",L"l4sql",L"l4pin",L"l4capture",L"ffmpeg"};
static bool reject(DWORD code){SetLastError(code);return false;}
static bool template_bytes(const SetupManifest* m,unsigned index,BYTE** bytes,DWORD* size){
 *bytes=NULL;*size=0;wchar_t path[MAX_PATH],destination[MAX_PATH];if(!setup_manifest_config(m,index,path,destination))return false;
 unsigned count=0;const L4ReleaseFile* files=setup_manifest_files(m,&count);const L4ReleaseFile* selected=NULL;const L4Layout* layout=setup_manifest_layout(m);
 for(unsigned i=0;files&&i<count;i++){wchar_t candidate[MAX_PATH];if(!l4_layout_component(layout,files[i].component,files[i].file,candidate))return false;if(!wcscmp(candidate,path)){if(selected)return reject(ERROR_DUP_NAME);selected=files+i;}}
 if(!selected || !selected->size || selected->size>65536)return reject(ERROR_INVALID_DATA);L4ReleaseFence* fence=NULL;if(!l4_release_pin(layout,selected,&fence,path))return false;
 HANDLE input=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);BYTE* b=malloc((size_t)selected->size);DWORD n=0;
 bool ok=input!=INVALID_HANDLE_VALUE&&b&&ReadFile(input,b,(DWORD)selected->size,&n,NULL)&&n==selected->size;DWORD error=GetLastError();if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);l4_release_unpin(fence);if(!ok){free(b);return reject(error?error:ERROR_INVALID_DATA);}*bytes=b;*size=n;return true;
}
static bool broker_identity(const L4Layout* layout,const BYTE* bytes,DWORD size,char sn[128]){
 if(!bytes || !size || size>=8192 || memchr(bytes,0,size))return reject(ERROR_INVALID_DATA);char text[8192];memcpy(text,bytes,size);text[size]=0;
 const char* prefix="\nremote_clientid ";char* at=strstr(text,prefix);if(!at || strstr(at+1,prefix))return reject(ERROR_NOT_SUPPORTED);at+=strlen(prefix);char* end=strchr(at,'\n');if(!end || end==at || end-at>127)return reject(ERROR_INVALID_DATA);
 memcpy(sn,at,(size_t)(end-at));sn[end-at]=0;SetupBrokerConfig expected;if(!setup_broker_render(layout,sn,&expected))return false;
 return expected.size==size&&!memcmp(expected.bytes,bytes,size)?true:reject(ERROR_NOT_SUPPORTED);
}
static bool launcher_baseline(const SetupManifest* manifest,unsigned tool,const BYTE* bytes,DWORD size){
 unsigned count=0;const L4ReleaseFile* files=setup_manifest_files(manifest,&count);const L4ReleaseFile* found=NULL;wchar_t exe[48];
 if(tool>=9)return reject(ERROR_INVALID_PARAMETER);if(tool==7)wcscpy_s(exe,48,L"bin\\l4capture.exe");else swprintf_s(exe,48,L"%ls.exe",tools[tool]);
 for(unsigned i=0;files&&i<count;i++)if(!wcscmp(files[i].component,tools[tool])&&!wcscmp(files[i].file,exe)){if(found)return reject(ERROR_DUP_NAME);found=files+i;}
 const L4Layout* layout=setup_manifest_layout(manifest);const wchar_t* version=layout?wcsrchr(layout->release,L'\\'):NULL;char ascii[32],hash[65],record[160];
 if(!found||!found->size||!version||!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,version+1,-1,ascii,sizeof(ascii),NULL,NULL))return reject(ERROR_INVALID_DATA);
 for(unsigned i=0;i<32;i++)sprintf_s(hash+i*2,3,"%02x",found->sha256[i]);int n=sprintf_s(record,sizeof(record),"L4CLI1\n%s\n%llu\n%s\n",ascii,found->size,hash);
 return n>0&&(DWORD)n==size&&!memcmp(bytes,record,size)?true:reject(ERROR_NOT_SUPPORTED);
}
static bool current_hold(L4Journal* j,unsigned index,const BYTE* expected,DWORD expected_size,L4FileFence* parents,HANDLE* file){
 *file=INVALID_HANDLE_VALUE;wchar_t relative[MAX_PATH],path[MAX_PATH],parent[MAX_PATH];if(swprintf_s(relative,MAX_PATH,L"config\\%ls",relatives[index])<0||!l4_layout_data_path(&j->layout,relative,path))return false;
 wcscpy_s(parent,MAX_PATH,path);*wcsrchr(parent,L'\\')=0;if(!l4_store_pin(parent,j->layout.data,false,parents))return false;
 *file=CreateFileW(path,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);if(*file==INVALID_HANDLE_VALUE)return false;
 BY_HANDLE_FILE_INFORMATION info;LARGE_INTEGER length;BYTE* sd=NULL;DWORD sd_size=0;BYTE* actual=malloc(expected_size);DWORD n=0;
 bool ok=actual&&GetFileInformationByHandle(*file,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&
  GetFileSizeEx(*file,&length)&&length.QuadPart==expected_size&&l4_store_security(*file,false,&sd,&sd_size)&&ReadFile(*file,actual,expected_size,&n,NULL)&&n==expected_size&&!memcmp(actual,expected,expected_size);
 DWORD error=GetLastError();free(sd);free(actual);return ok?true:reject(error?error:ERROR_NOT_SUPPORTED);
}
bool setup_remote_config_prepare(L4Journal* j,SetupInstalledSource* source,const SetupManifest* target,SetupRemoteConfigProposals* output){
 if(!output)return reject(ERROR_INVALID_PARAMETER);memset(output,0,sizeof(*output));const L4BootstrapPlan* source_plan=setup_installed_source_plan(source);const L4Layout* old=source_plan?&source_plan->layout:NULL;const L4Layout* next=setup_manifest_layout(target);
 if(!j||!target||!next||!old||!setup_installed_source_owned_by(source,j)||wcscmp(j->layout.binaries,old->binaries)||wcscmp(j->layout.data,old->data)||wcscmp(old->binaries,next->binaries)||wcscmp(old->data,next->data)||!setup_manifest_arch(target)||strcmp(setup_manifest_arch(target),setup_installed_source_arch(source)))return reject(ERROR_REVISION_MISMATCH);
 if(!setup_installed_source_verify(source)||!setup_manifest_verify(target))return false;
 BYTE* baseline[12]={0};DWORD sizes[12]={0};BYTE* candidates[3]={0};DWORD candidate_sizes[3]={0};L4FileFence holds[12]={0};HANDLE files[12];for(unsigned i=0;i<12;i++)files[i]=INVALID_HANDLE_VALUE;bool ok=true;char sn[128]={0};SetupBrokerConfig broker={0};
 for(unsigned i=0;ok&&i<12;i++)ok=setup_installed_source_config(source,i,&baseline[i],&sizes[i]);
 unsigned template_indices[]={0,1};unsigned slots[]={0,2};
 for(unsigned i=0;ok&&i<2;i++){BYTE* original=NULL;DWORD original_size=0;ok=template_bytes(setup_installed_source_manifest(source),template_indices[i],&original,&original_size)&&original_size==sizes[slots[i]]&&!memcmp(original,baseline[slots[i]],original_size);free(original);if(!ok){SetLastError(ERROR_NOT_SUPPORTED);break;}ok=template_bytes(target,template_indices[i],&candidates[slots[i]],&candidate_sizes[slots[i]]);}
 for(unsigned i=0;ok&&i<9;i++)ok=launcher_baseline(setup_installed_source_manifest(source),i,baseline[3+i],sizes[3+i]);
 if(ok)ok=broker_identity(old,baseline[1],sizes[1],sn)&&setup_broker_render(next,sn,&broker);
 if(ok){candidates[1]=malloc(broker.size);ok=candidates[1]!=NULL;if(ok){memcpy(candidates[1],broker.bytes,broker.size);candidate_sizes[1]=broker.size;}else SetLastError(ERROR_NOT_ENOUGH_MEMORY);}
 /* Validate ALL old bytes before journaling any proposal, and retain files until
  * snapshot capture/verification finishes. No partial custom overwrite. */
 for(unsigned i=0;ok&&i<12;i++)ok=current_hold(j,i,baseline[i],sizes[i],&holds[i],&files[i]);
 ULONGLONG* refs[]={&output->supervisor,&output->broker,&output->acl};
 for(unsigned i=0;ok&&i<3;i++)ok=l4_config_prepare(j,relatives[i],candidates[i],candidate_sizes[i],refs[i])&&l4_config_verify(j,*refs[i],false);
 unsigned target_count=0;const L4ReleaseFile* target_files=setup_manifest_files(target,&target_count);
 for(unsigned i=0;ok&&i<9;i++)ok=l4_launcher_prepare(j,next,tools[i],target_files,target_count,&output->launchers[i])&&l4_config_verify(j,output->launchers[i],false);
 DWORD error=GetLastError();for(unsigned i=0;i<12;i++){free(baseline[i]);if(i<3)free(candidates[i]);if(files[i]!=INVALID_HANDLE_VALUE)CloseHandle(files[i]);l4_store_unpin(&holds[i]);}
 if(!ok){memset(output,0,sizeof(*output));return reject(error?error:ERROR_INVALID_DATA);}return true;
}
