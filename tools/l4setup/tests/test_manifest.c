#include "../src/manifest.h"
#include <wincrypt.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL manifest %u: %s (error=%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static bool digest_bytes(const char* bytes,DWORD length,BYTE digest[32]){DWORD size=32;return CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM,0,NULL,(const BYTE*)bytes,length,digest,&size)!=0 && size==32;}
static void bad_document(const char* original,const char* old,const char* replacement,const L4Layout* layout,const char* arch,const BYTE publisher[32]){
    const char* found=strstr(original,old);CHECK(found);if(!found)return;
    char edited[65536];size_t prefix=(size_t)(found-original);memcpy(edited,original,prefix);
    strcpy_s(edited+prefix,sizeof(edited)-prefix,replacement);strcat_s(edited,sizeof(edited),found+strlen(old));
    BYTE digest[32];CHECK(digest_bytes(edited,(DWORD)strlen(edited),digest));SetupManifest* result=NULL;
    CHECK(!setup_manifest_parse(edited,(DWORD)strlen(edited),digest,publisher,layout,arch,&result));CHECK(!result);
}
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 1;char arch[8];CHECK(WideCharToMultiByte(CP_UTF8,0,argv[2],-1,arch,sizeof(arch),NULL,NULL));
    FILE* input=NULL;CHECK(_wfopen_s(&input,argv[1],L"rb")==0 && input);if(!input)return 1;
    char bytes[65536];size_t length=fread(bytes,1,sizeof(bytes)-1,input);CHECK(!ferror(input) && feof(input));fclose(input);bytes[length]=0;
    L4Layout layout;CHECK(l4_layout_from_roots(&layout,L"C:\\L4ManifestFixture\\Programs",L"C:\\L4ManifestFixture\\Data",L"1.13.3"));
    BYTE descriptor[32],publisher[32];memset(publisher,0x11,32);CHECK(digest_bytes(bytes,(DWORD)length,descriptor));
    SetupManifest* manifest=NULL;CHECK(setup_manifest_parse(bytes,(DWORD)length,descriptor,publisher,&layout,arch,&manifest));
    if(manifest){
        unsigned count=0;const L4ReleaseFile* files=setup_manifest_files(manifest,&count);CHECK(files && count>=13 && count<=64);
        wchar_t source[MAX_PATH],target[MAX_PATH];
        for(unsigned i=0;i<3;i++){CHECK(setup_manifest_config(manifest,i,source,target));CHECK(wcsstr(source,L"\\releases\\1.13.3\\templates\\"));CHECK(wcsstr(target,L"\\Data\\config\\"));}
        CHECK(!setup_manifest_config(manifest,3,source,target));CHECK(!setup_manifest_prepare(manifest,L"C:\\outside.zip"));
        setup_manifest_free(manifest);manifest=NULL;
    }
    BYTE wrong[32]={1};CHECK(!setup_manifest_parse(bytes,(DWORD)length,wrong,publisher,&layout,arch,&manifest));CHECK(GetLastError()==ERROR_CRC && !manifest);
    CHECK(!setup_manifest_parse(bytes,(DWORD)length,descriptor,wrong,&layout,arch,&manifest));CHECK(!manifest);
    CHECK(!setup_manifest_parse(bytes,(DWORD)length,descriptor,publisher,&layout,!strcmp(arch,"x86")?"x64":"x86",&manifest));
    CHECK(!setup_manifest_parse(bytes,(DWORD)length,descriptor,publisher,&layout,arch,NULL));
    bad_document(bytes,"1.13.3","1.13.4",&layout,arch,publisher);
    bad_document(bytes,"\"schema\": 1","\"schema\": 1, \"extra\": 0",&layout,arch,publisher);
    bad_document(bytes,"\"schema\": 1","\"schema\": 1, \"schema\": 1",&layout,arch,publisher);
    bad_document(bytes,"l4launch/l4launch.exe","l4launch/README.txt",&layout,arch,publisher);
    bad_document(bytes,"templates/mosquitto/acl.conf","mosquitto/acl.conf",&layout,arch,publisher);
    bad_document(bytes,"l4con/l4con.exe","l4con/../l4con.exe",&layout,arch,publisher);
    bad_document(bytes,"l4con/l4con.exe","l4con/state/l4con.exe",&layout,arch,publisher);
    bad_document(bytes,"l4con/l4con.exe","l4con/l4con.cmd",&layout,arch,publisher);
    bad_document(bytes,"l4con/l4con.exe","l4con/CON.exe",&layout,arch,publisher);
    bad_document(bytes,"l4con/l4con.exe","l4con/l4con.exe:stream",&layout,arch,publisher);
    bad_document(bytes,"l4sql/l4sql.exe","l4con/l4con.exe",&layout,arch,publisher);
    bad_document(bytes,"suite/term_tool-user-guide.md","unknown/guide.md",&layout,arch,publisher);
    bad_document(bytes,"suite/term_tool-user-guide.md","l4con/log/guide.md",&layout,arch,publisher);
    bad_document(bytes,"\"size\": 9","\"size\": -1",&layout,arch,publisher);
    bad_document(bytes,"\"size\": 9","\"size\": 536870913",&layout,arch,publisher);
    BYTE shortened[32];CHECK(digest_bytes(bytes,(DWORD)length-2,shortened));CHECK(!setup_manifest_parse(bytes,(DWORD)length-2,shortened,publisher,&layout,arch,&manifest));
    printf("Manifest producer/native consumer: %u checks, %u failures; config plan is read-only\n",checks,failures);return failures?1:0;
}
