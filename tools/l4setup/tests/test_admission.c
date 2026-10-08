#include "../src/admission.h"
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "admission_fixture.h"
static unsigned checks,failures,verified,fixture_closed;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL admission %u: %s (error=%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static LONG fixture_status,close_status,fallback_status;static bool local_policy;
static bool actual_provider,missing_data;
static CRYPT_PROVIDER_DATA provider;
static CRYPT_PROVIDER_SGNR fixture_signer_state,counter;
static CRYPT_PROVIDER_CERT chain;
static CERT_CONTEXT certificate;
static BYTE encoded[]={1,2,3},fixture_publisher[32];
static LONG WINAPI fixture_trust(HWND hwnd,GUID* action,LPVOID input){
    WINTRUST_DATA* trust=(WINTRUST_DATA*)input;
    if(actual_provider)return WinVerifyTrust(hwnd,action,input);
    CHECK(hwnd==INVALID_HANDLE_VALUE && trust->dwUIChoice==WTD_UI_NONE && trust->dwUnionChoice==WTD_CHOICE_FILE);
    CHECK(trust->pFile && trust->pFile->hFile && trust->pFile->pcwszFilePath);
    if(local_policy){
        CHECK((trust->dwProvFlags&WTD_CACHE_ONLY_URL_RETRIEVAL)!=0);
        CHECK((trust->fdwRevocationChecks==WTD_REVOKE_WHOLECHAIN && trust->dwProvFlags==(WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT|WTD_CACHE_ONLY_URL_RETRIEVAL)) ||
            (trust->fdwRevocationChecks==WTD_REVOKE_NONE && trust->dwProvFlags==(WTD_REVOCATION_CHECK_NONE|WTD_CACHE_ONLY_URL_RETRIEVAL)));
    }else CHECK(trust->fdwRevocationChecks==WTD_REVOKE_WHOLECHAIN && trust->dwProvFlags==WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT);
    if(trust->dwStateAction==WTD_STATEACTION_CLOSE){++fixture_closed;return close_status;}
    CHECK(trust->dwStateAction==WTD_STATEACTION_VERIFY);++verified;trust->hWVTStateData=(HANDLE)(ULONG_PTR)1;return trust->fdwRevocationChecks==WTD_REVOKE_NONE?fallback_status:fixture_status;
}
static CRYPT_PROVIDER_DATA* WINAPI fixture_data(HANDLE state){if(actual_provider)return WTHelperProvDataFromStateData(state);CHECK(state==(HANDLE)(ULONG_PTR)1);return missing_data?NULL:&provider;}
static CRYPT_PROVIDER_SGNR* WINAPI fixture_get_signer(CRYPT_PROVIDER_DATA* data,DWORD index,BOOL countersigner,DWORD counter_index){if(actual_provider)return WTHelperGetProvSignerFromChain(data,index,countersigner,counter_index);CHECK(data==&provider && !index && !countersigner && !counter_index);return &fixture_signer_state;}
#define WinVerifyTrust fixture_trust
#define WTHelperProvDataFromStateData fixture_data
#define WTHelperGetProvSignerFromChain fixture_get_signer
#include "../src/admission.c"
#undef WinVerifyTrust
#undef WTHelperProvDataFromStateData
#undef WTHelperGetProvSignerFromChain
static void reset(void){
    fixture_status=close_status=fallback_status=0;local_policy=false;missing_data=false;verified=fixture_closed=0;
    memset(&fixture_signer_state,0,sizeof(fixture_signer_state));memset(&counter,0,sizeof(counter));memset(&chain,0,sizeof(chain));memset(&certificate,0,sizeof(certificate));
    certificate.pbCertEncoded=encoded;certificate.cbCertEncoded=sizeof(encoded);chain.pCert=&certificate;
    fixture_signer_state.csCertChain=counter.csCertChain=1;fixture_signer_state.pasCertChain=counter.pasCertChain=&chain;fixture_signer_state.csCounterSigners=1;fixture_signer_state.pasCounterSigners=&counter;
}
static void cleanup(const wchar_t* root){
    wchar_t pattern[MAX_PATH],path[MAX_PATH];swprintf_s(pattern,MAX_PATH,L"%ls\\*",root);
    WIN32_FIND_DATAW entry;HANDLE search=FindFirstFileW(pattern,&entry);
    if(search!=INVALID_HANDLE_VALUE){do{
        if(!wcscmp(entry.cFileName,L".") || !wcscmp(entry.cFileName,L".."))continue;
        swprintf_s(path,MAX_PATH,L"%ls\\%ls",root,entry.cFileName);
        CHECK(!(entry.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT));
        if(entry.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)continue;
        if(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)cleanup(path);else CHECK(DeleteFileW(path));
    }while(FindNextFileW(search,&entry));FindClose(search);}CHECK(RemoveDirectoryW(root));
}
static void package_test(void){
    wchar_t temp[MAX_PATH],root[MAX_PATH],programs[MAX_PATH],data[MAX_PATH],archive[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH,temp)>0);swprintf_s(root,MAX_PATH,L"%lsl4admission-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());
    CHECK(CreateDirectoryW(root,NULL));swprintf_s(programs,MAX_PATH,L"%ls\\Programs",root);swprintf_s(data,MAX_PATH,L"%ls\\Data",root);
    L4Layout layout;CHECK(l4_layout_from_roots(&layout,programs,data,L"1.13.2"));CHECK(l4_layout_prepare(&layout));
    CHECK(l4_layout_data_path(&layout,L"update\\cache\\fixture.zip",archive));
    HANDLE out=CreateFileW(archive,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);DWORD written;
    CHECK(out!=INVALID_HANDLE_VALUE && WriteFile(out,admission_archive,sizeof(admission_archive),&written,NULL) && written==sizeof(admission_archive));CloseHandle(out);
    BYTE hash[32];DWORD size=32;CHECK(CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM,0,NULL,admission_archive,sizeof(admission_archive),hash,&size));
    L4ReleaseFile file={L"l4con",L"l4con.exe",9,{0}};size=32;CHECK(CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM,0,NULL,(const BYTE*)"MZfixture",9,file.sha256,&size));
    reset();fixture_status=TRUST_E_NOSIGNATURE;CHECK(!setup_release_prepare(&layout,archive,hash,&file,1,fixture_publisher));
    CHECK(verified==1 && fixture_closed==1 && GetFileAttributesW(layout.release)==INVALID_FILE_ATTRIBUTES);
    reset();BYTE wrong[32]={1};CHECK(!setup_release_prepare(&layout,archive,hash,&file,1,wrong));
    CHECK(verified==1 && fixture_closed==1 && GetFileAttributesW(layout.release)==INVALID_FILE_ATTRIBUTES);
    reset();counter.dwError=1;CHECK(!setup_release_prepare(&layout,archive,hash,&file,1,fixture_publisher));
    CHECK(GetFileAttributesW(layout.release)==INVALID_FILE_ATTRIBUTES);
    reset();CHECK(setup_release_prepare(&layout,archive,hash,&file,1,fixture_publisher));CHECK(verified==1 && fixture_closed==1);
    reset();CHECK(setup_release_verify(&layout,&file,1,fixture_publisher));CHECK(verified==1 && fixture_closed==1);
    reset();fixture_status=1;CHECK(!setup_release_prepare(&layout,archive,hash,&file,1,fixture_publisher));CHECK(l4_release_verify(&layout,&file,1));
    reset();local_policy=true;fixture_status=CRYPT_E_REVOCATION_OFFLINE;
    CHECK(setup_release_verify_policy(&layout,&file,1,fixture_publisher,SETUP_ADMISSION_LOCAL_OFFLINE) && verified==2);
    reset();local_policy=true;fixture_status=CERT_E_REVOKED;
    CHECK(!setup_release_verify_policy(&layout,&file,1,fixture_publisher,SETUP_ADMISSION_LOCAL_OFFLINE) && verified==1);
    reset();CHECK(!setup_release_verify_policy(&layout,&file,1,fixture_publisher,(SetupAdmissionPolicy)7) && !verified);
    reset();actual_provider=true;CHECK(!setup_release_verify(&layout,&file,1,fixture_publisher));actual_provider=false;
    CHECK(l4_release_verify(&layout,&file,1));cleanup(root);CHECK(GetFileAttributesW(root)==INVALID_FILE_ATTRIBUTES);
}
int wmain(int argc,wchar_t** argv){
    DWORD size=32;CHECK(CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM,0,NULL,encoded,sizeof(encoded),fixture_publisher,&size) && size==32);
    wchar_t executable[MAX_PATH];CHECK(GetModuleFileNameW(NULL,executable,MAX_PATH)>0);
    HANDLE file=CreateFileW(executable,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(file!=INVALID_HANDLE_VALUE);
    reset();CHECK(signed_file(file,executable,fixture_publisher));CHECK(verified==1 && fixture_closed==1);
    BYTE wrong[32]={1};reset();CHECK(!signed_file(file,executable,wrong));CHECK(GetLastError()==ERROR_REVISION_MISMATCH && fixture_closed==1);
    reset();fixture_status=1;CHECK(!signed_file(file,executable,fixture_publisher));CHECK(fixture_closed==1); /* Positive LONG is still failure. */
    reset();fixture_status=TRUST_E_NOSIGNATURE;CHECK(!signed_file(file,executable,fixture_publisher));CHECK(fixture_closed==1);
    reset();fixture_status=CRYPT_E_REVOCATION_OFFLINE;CHECK(!signed_file(file,executable,fixture_publisher));CHECK(fixture_closed==1);
    reset();missing_data=true;CHECK(!signed_file(file,executable,fixture_publisher));CHECK(fixture_closed==1);
    reset();fixture_signer_state.dwError=1;CHECK(!signed_file(file,executable,fixture_publisher));CHECK(fixture_closed==1);
    reset();fixture_signer_state.csCounterSigners=0;CHECK(!signed_file(file,executable,fixture_publisher));CHECK(fixture_closed==1);
    reset();counter.dwError=1;CHECK(!signed_file(file,executable,fixture_publisher));CHECK(fixture_closed==1);
    reset();counter.csCertChain=0;CHECK(!signed_file(file,executable,fixture_publisher));CHECK(fixture_closed==1);
    reset();close_status=ERROR_ACCESS_DENIED;CHECK(!signed_file(file,executable,fixture_publisher));CHECK(fixture_closed==1);
    reset();actual_provider=true;CHECK(!signed_file(file,executable,fixture_publisher));
    CHECK(!setup_signed_executable_policy(file,executable,fixture_publisher,SETUP_ADMISSION_LOCAL_OFFLINE));actual_provider=false;
    /* Local-only cache lookup may tolerate unavailable revocation evidence,
     * while every cryptographic/identity/known-revocation failure stays fatal. */
    const LONG unknowns[]={CRYPT_E_REVOCATION_OFFLINE,CRYPT_E_NO_REVOCATION_CHECK};
    for(unsigned i=0;i<_countof(unknowns);i++){
        reset();local_policy=true;fixture_status=unknowns[i];
        CHECK(setup_signed_executable_policy(file,executable,fixture_publisher,SETUP_ADMISSION_LOCAL_OFFLINE));CHECK(verified==2 && fixture_closed==2);
        reset();fixture_status=unknowns[i];CHECK(!setup_signed_executable_policy(file,executable,fixture_publisher,SETUP_ADMISSION_STRICT_REMOTE));CHECK(verified==1);
    }
    const LONG forbidden[]={CERT_E_REVOKED,CRYPT_E_REVOKED,CERT_E_EXPIRED,CERT_E_UNTRUSTEDROOT,TRUST_E_BAD_DIGEST,TRUST_E_NOSIGNATURE,CERT_E_REVOCATION_FAILURE};
    for(unsigned i=0;i<_countof(forbidden);i++){
        reset();local_policy=true;fixture_status=forbidden[i];CHECK(!setup_signed_executable_policy(file,executable,fixture_publisher,SETUP_ADMISSION_LOCAL_OFFLINE));CHECK(verified==1 && fixture_closed==1);
        reset();local_policy=true;fixture_status=CRYPT_E_REVOCATION_OFFLINE;fallback_status=forbidden[i];
        CHECK(!setup_signed_executable_policy(file,executable,fixture_publisher,SETUP_ADMISSION_LOCAL_OFFLINE));CHECK(verified==2 && fixture_closed==2);
    }
    reset();local_policy=true;fixture_status=CRYPT_E_REVOCATION_OFFLINE;chain.dwError=(DWORD)CERT_E_REVOKED;
    CHECK(!setup_signed_executable_policy(file,executable,fixture_publisher,SETUP_ADMISSION_LOCAL_OFFLINE) && verified==1);
    reset();local_policy=true;fixture_status=CRYPT_E_REVOCATION_OFFLINE;
    CERT_CHAIN_CONTEXT revoked_context={0};revoked_context.TrustStatus.dwErrorStatus=CERT_TRUST_IS_REVOKED;fixture_signer_state.pChainContext=&revoked_context;
    CHECK(!setup_signed_executable_policy(file,executable,fixture_publisher,SETUP_ADMISSION_LOCAL_OFFLINE) && verified==1);
    reset();local_policy=true;chain.dwError=(DWORD)CERT_E_REVOKED;
    CHECK(!setup_signed_executable_policy(file,executable,fixture_publisher,SETUP_ADMISSION_LOCAL_OFFLINE) && verified==1);
    reset();local_policy=true;fixture_status=CRYPT_E_REVOCATION_OFFLINE;counter.dwError=(DWORD)CRYPT_E_REVOKED;
    CHECK(!setup_signed_executable_policy(file,executable,fixture_publisher,SETUP_ADMISSION_LOCAL_OFFLINE) && verified==1);
    reset();local_policy=true;fixture_status=CRYPT_E_REVOCATION_OFFLINE;
    CHECK(!setup_signed_executable_policy(file,executable,wrong,SETUP_ADMISSION_LOCAL_OFFLINE) && verified==2);
    reset();local_policy=true;fixture_status=CRYPT_E_REVOCATION_OFFLINE;counter.dwError=(DWORD)TRUST_E_TIME_STAMP;
    CHECK(!setup_signed_executable_policy(file,executable,fixture_publisher,SETUP_ADMISSION_LOCAL_OFFLINE) && verified==2);
    reset();CHECK(!setup_signed_executable_policy(file,executable,fixture_publisher,(SetupAdmissionPolicy)7) && !verified);
    BYTE zero[32]={0};CHECK(!setup_release_prepare(NULL,NULL,NULL,NULL,0,NULL));CHECK(!setup_release_verify(NULL,NULL,0,zero));
    CHECK(!publisher_valid(zero) && publisher_valid(fixture_publisher));
    CloseHandle(file);
    wchar_t temp[MAX_PATH],path[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp)>0);CHECK(GetTempFileNameW(temp,L"l4a",0,path));
    HANDLE output=CreateFileW(path,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);DWORD written;
    CHECK(output!=INVALID_HANDLE_VALUE && WriteFile(output,"data",4,&written,NULL) && written==4);CloseHandle(output);
    file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(file!=INVALID_HANDLE_VALUE);
    reset();CHECK(admit(file,path,fixture_publisher));CHECK(!verified && !fixture_closed); /* Hash-protected data. */
    LARGE_INTEGER origin={0};SetFilePointerEx(file,origin,NULL,FILE_BEGIN);CHECK(!admit(file,L"invalid.exe",fixture_publisher));CHECK(GetLastError()==ERROR_BAD_EXE_FORMAT);CloseHandle(file);
    output=CreateFileW(path,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);CHECK(WriteFile(output,"MZ",2,&written,NULL));CloseHandle(output);
    file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);reset();CHECK(admit(file,path,fixture_publisher));CHECK(verified==1 && fixture_closed==1);CloseHandle(file);CHECK(DeleteFileW(path));
    package_test();
    if(argc==3){
        BYTE expected[32];DWORD expected_size=32;
        CHECK(CryptStringToBinaryW(argv[2],0,CRYPT_STRING_HEXRAW,expected,&expected_size,NULL,NULL) && expected_size==32);
        HANDLE signed_fixture=CreateFileW(argv[1],GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
        CHECK(signed_fixture!=INVALID_HANDLE_VALUE);actual_provider=true;
        CHECK(signed_file(signed_fixture,argv[1],expected));
        CHECK(setup_signed_executable_policy(signed_fixture,argv[1],expected,SETUP_ADMISSION_LOCAL_OFFLINE));actual_provider=false;CloseHandle(signed_fixture);
        puts("Actual pre-existing signed PE: publisher/timestamp/Windows trust verification exercised");
    }else CHECK(argc==1);
    printf("Signed admission: %u checks, %u failures; provider success modeled, actual unsigned PE rejected\n",checks,failures);return failures?1:0;
}
