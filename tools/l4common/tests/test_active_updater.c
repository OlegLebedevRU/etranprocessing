/* Pure metadata/record boundary model. Production owner RSA verifier is tested
 * separately; no trusted-key injection, native file write, SCM or broker here. */
#include "../metadata.h"
#include "../journal_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned verified;static bool allow_signature=true;
static bool fixture_signature(const void* bytes,DWORD n,const BYTE* signature,DWORD s,BYTE digest[32]){
 ++verified;if(!allow_signature||!signature||s!=384||signature[0]!=0x5a){memset(digest,0,32);return false;}return l4_store_hash(bytes,n,NULL,0,digest);
}
#define l4_metadata_verify_trusted fixture_signature
#include "../active_updater.c"
#undef l4_metadata_verify_trusted
static void hex(const BYTE bytes[32],char text[65]){for(unsigned i=0;i<32;i++)sprintf_s(text+2*i,65-2*i,"%02x",bytes[i]);}
int main(void){L4ActiveUpdaterInfo info={0},decoded;strcpy_s(info.version,32,"1.13.6");strcpy_s(info.arch,8,"x64");strcpy_s(info.origin,37,"17730000-0000-4000-8000-000000000001");memset(info.root_sha256,0x11,32);
 char pointer[512];DWORD n=0;assert(l4_active_updater_encode(&info,pointer,512,&n)&&l4_active_updater_decode(pointer,n,&decoded));assert(!memcmp(&info,&decoded,sizeof(info)));
 assert(!l4_active_updater_decode(pointer,n-1,&decoded));char duplicate[600];sprintf_s(duplicate,600,"{\"schema\":1,%s",pointer+1);assert(!l4_active_updater_decode(duplicate,(DWORD)strlen(duplicate),&decoded));
 sprintf_s(duplicate,600,"{\"path\":\"C:/caller.exe\",%s",pointer+1);assert(!l4_active_updater_decode(duplicate,(DWORD)strlen(duplicate),&decoded));
 strcpy_s(info.version,32,"latest");assert(!l4_active_updater_encode(&info,pointer,512,&n));strcpy_s(info.version,32,"1.13.6");strcpy_s(info.origin,37,"00000000-0000-0000-0000-000000000000");assert(!l4_active_updater_encode(&info,pointer,512,&n));strcpy_s(info.origin,37,"17730000-0000-4000-8000-0000000000AB");assert(!l4_active_updater_encode(&info,pointer,512,&n));
 L4ActiveUpdater active={0};strcpy_s(active.info.version,32,"1.13.6");strcpy_s(active.info.arch,8,"x64");BYTE signature[384]={0x5a};const char descriptor[]="fixture-descriptor";BYTE dsha[32],ssha[32];char dh[65],sh[65];assert(l4_store_hash(descriptor,sizeof(descriptor)-1,NULL,0,dsha)&&l4_store_hash(signature,384,NULL,0,ssha));hex(dsha,dh);hex(ssha,sh);
 char root[2000];int length=sprintf_s(root,2000,"{\"schema\":1,\"version\":\"1.13.6\",\"signed\":true,\"dirty\":false,\"signature_status\":\"Valid\",\"min_os\":\"6.1\",\"publisher_certificate_sha256\":\"%s\",\"arch\":[\"x86\",\"x64\"],\"files\":{\"l4setup.exe\":{\"size\":123,\"sha256\":\"%s\"},\"l4tools-layout-x64.json\":{\"size\":%u,\"sha256\":\"%s\"},\"l4tools-layout-x64.json.sig\":{\"size\":384,\"sha256\":\"%s\"}}}",dh,sh,(unsigned)sizeof(descriptor)-1,dh,sh);
 BYTE receipt[1024]={0};DWORD count=13,d=sizeof(descriptor)-1,size=68+count*8+d+384;l4_store_u32(receipt+56,count);l4_store_u32(receipt+60,d);l4_store_u32(receipt+64,384);memcpy(receipt+68+count*8,descriptor,d);memcpy(receipt+68+count*8+d,signature,384);
 assert(l4_store_hash(root,(DWORD)length,NULL,0,active.info.root_sha256));memcpy(receipt+8,active.info.root_sha256,32);
 assert(root_claim(&active,(BYTE*)root,(DWORD)length,signature,384,receipt,size)&&active.info.image_size==123);
 assert(!root_claim(&active,(BYTE*)root,(DWORD)length,signature,384,receipt,size-1));
 unsigned before=verified;allow_signature=false;assert(!root_claim(&active,(BYTE*)"invalid",7,signature,384,receipt,size)&&verified==before+1);allow_signature=true;
 strcpy_s(active.info.version,32,"1.13.7");assert(!root_claim(&active,(BYTE*)root,(DWORD)length,signature,384,receipt,size));strcpy_s(active.info.version,32,"1.13.6");
 receipt[68+count*8]^=1;assert(!root_claim(&active,(BYTE*)root,(DWORD)length,signature,384,receipt,size));receipt[68+count*8]^=1;
 strcpy_s(active.info.arch,8,"x86");assert(!root_claim(&active,(BYTE*)root,(DWORD)length,signature,384,receipt,size));
 History history={0};assert(records(85,1,NULL,0,&history));assert(!records(85,2,NULL,0,&history));assert(!records(92,2,NULL,0,&history));assert(!records(102,2,NULL,0,&history));
 L4ActiveUpdater* output=(L4ActiveUpdater*)1;assert(!l4_active_updater_open(NULL,&output)&&!output);assert(!l4_active_updater_open_fixed(NULL,&output)&&!output);assert(!l4_active_updater_initialize_fresh(NULL,"x64"));assert(!l4_active_updater_verify(NULL));l4_active_updater_close(NULL);
 puts("PASS: updater fixed pointer, suite-independent version, signature-first nomination, descriptor/arch binding, fresh-only history and absent-owner refusal");return 0;
}
