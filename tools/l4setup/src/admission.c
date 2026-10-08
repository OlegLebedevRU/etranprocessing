#include "admission.h"
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <string.h>

static bool reject(DWORD error){SetLastError(error);return false;}
static bool publisher_valid(const BYTE* publisher){
    if(!publisher)return false;BYTE any=0;for(unsigned i=0;i<32;i++)any|=publisher[i];return any!=0;
}
static bool known_revoked(LONG error){return error==CERT_E_REVOKED || error==CRYPT_E_REVOKED;}
static bool chain_revoked(const CRYPT_PROVIDER_SGNR* signer){
    if(!signer)return false;if(known_revoked((LONG)signer->dwError))return true;
    if(signer->pChainContext && (signer->pChainContext->TrustStatus.dwErrorStatus&CERT_TRUST_IS_REVOKED))return true;
    if(signer->csCertChain>64 || (signer->csCertChain && !signer->pasCertChain))return true;
    for(DWORD i=0;i<signer->csCertChain;i++)if(known_revoked((LONG)signer->pasCertChain[i].dwError))return true;
    return false;
}
static bool provider_revoked(HANDLE state){
    CRYPT_PROVIDER_DATA* data=WTHelperProvDataFromStateData(state);
    CRYPT_PROVIDER_SGNR* signer=data?WTHelperGetProvSignerFromChain(data,0,FALSE,0):NULL;
    if(chain_revoked(signer))return true;
    if(signer && (signer->csCounterSigners>64 || (signer->csCounterSigners && !signer->pasCounterSigners)))return true;
    if(signer)for(DWORD i=0;i<signer->csCounterSigners;i++)if(chain_revoked(&signer->pasCounterSigners[i]))return true;
    return false;
}
static bool signed_pass(HANDLE file,const wchar_t* path,const BYTE publisher[32],SetupAdmissionPolicy policy,bool revocation,bool* unavailable){
    if(unavailable)*unavailable=false;
    WINTRUST_FILE_INFO info={0};info.cbStruct=sizeof(info);info.pcwszFilePath=path;info.hFile=file;
    WINTRUST_DATA trust={0};trust.cbStruct=sizeof(trust);trust.dwUIChoice=WTD_UI_NONE;
    trust.fdwRevocationChecks=revocation?WTD_REVOKE_WHOLECHAIN:WTD_REVOKE_NONE;trust.dwUnionChoice=WTD_CHOICE_FILE;
    trust.pFile=&info;trust.dwStateAction=WTD_STATEACTION_VERIFY;
    trust.dwProvFlags=revocation?WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT:WTD_REVOCATION_CHECK_NONE;
    if(policy==SETUP_ADMISSION_LOCAL_OFFLINE)trust.dwProvFlags|=WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status=WinVerifyTrust(INVALID_HANDLE_VALUE,&action,&trust);
    bool ok=status==0;DWORD error=ok?ERROR_INVALID_DATA:(DWORD)status;
    bool unknown=unavailable && policy==SETUP_ADMISSION_LOCAL_OFFLINE && revocation && (status==CRYPT_E_REVOCATION_OFFLINE || status==CRYPT_E_NO_REVOCATION_CHECK);
    if(policy==SETUP_ADMISSION_LOCAL_OFFLINE && (ok || unknown) && provider_revoked(trust.hWVTStateData)){ok=false;unknown=false;error=(DWORD)CERT_E_REVOKED;}
    if(ok){
        CRYPT_PROVIDER_DATA* data=WTHelperProvDataFromStateData(trust.hWVTStateData);
        CRYPT_PROVIDER_SGNR* signer=data?WTHelperGetProvSignerFromChain(data,0,FALSE,0):NULL;
        BYTE digest[32];DWORD size=sizeof(digest);
        ok=signer && !signer->dwError && signer->csCertChain && signer->pasCertChain && signer->pasCertChain[0].pCert &&
            CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM,0,NULL,signer->pasCertChain[0].pCert->pbCertEncoded,
                signer->pasCertChain[0].pCert->cbCertEncoded,digest,&size) && size==32;
        if(ok && memcmp(digest,publisher,32)){ok=false;error=ERROR_REVISION_MISMATCH;}
        /* Require a timestamp validated by the same Authenticode provider. */
        if(ok)ok=signer->csCounterSigners==1 && signer->pasCounterSigners && !signer->pasCounterSigners[0].dwError &&
            signer->pasCounterSigners[0].csCertChain && signer->pasCounterSigners[0].pasCertChain &&
            signer->pasCounterSigners[0].pasCertChain[0].pCert;
    }
    trust.dwStateAction=WTD_STATEACTION_CLOSE;
    LONG closed=WinVerifyTrust(INVALID_HANDLE_VALUE,&action,&trust);
    if(closed!=0){ok=false;unknown=false;error=(DWORD)closed;}
    if(unavailable)*unavailable=unknown;
    return ok?true:reject(error);
}
static bool policy_valid(SetupAdmissionPolicy policy){return policy==SETUP_ADMISSION_STRICT_REMOTE || policy==SETUP_ADMISSION_LOCAL_OFFLINE;}
static bool signed_policy(HANDLE file,const wchar_t* path,const BYTE publisher[32],SetupAdmissionPolicy policy){
    if(!policy_valid(policy))return reject(ERROR_INVALID_PARAMETER);bool unavailable=false;
    if(signed_pass(file,path,publisher,policy,true,&unavailable))return true;
    if(!unavailable)return false;LARGE_INTEGER zero={0};
    return SetFilePointerEx(file,zero,NULL,FILE_BEGIN) && signed_pass(file,path,publisher,policy,false,NULL);
}
static bool signed_file(HANDLE file,const wchar_t* path,const BYTE publisher[32]){return signed_policy(file,path,publisher,SETUP_ADMISSION_STRICT_REMOTE);}
typedef struct {const BYTE* publisher;SetupAdmissionPolicy policy;} AdmissionContext;
static bool admit_policy(HANDLE file,const wchar_t* path,void* context){
    AdmissionContext* admission=(AdmissionContext*)context;
    BYTE header[2]={0};DWORD bytes=0;
    if(!ReadFile(file,header,sizeof(header),&bytes,NULL))return false;
    const wchar_t* extension=wcsrchr(path,L'.');
    bool executable=extension && (!_wcsicmp(extension,L".exe") || !_wcsicmp(extension,L".dll") || !_wcsicmp(extension,L".sys"));
    bool mz=bytes==2 && header[0]=='M' && header[1]=='Z';
    if(!executable && !mz)return true; /* Data is still covered by authenticated file SHA256. */
    if(!mz)return reject(ERROR_BAD_EXE_FORMAT);
    LARGE_INTEGER zero={0};if(!SetFilePointerEx(file,zero,NULL,FILE_BEGIN))return false;
    return admission->policy==SETUP_ADMISSION_STRICT_REMOTE?signed_file(file,path,admission->publisher):signed_policy(file,path,admission->publisher,admission->policy);
}
static bool admit(HANDLE file,const wchar_t* path,void* publisher){AdmissionContext context={(const BYTE*)publisher,SETUP_ADMISSION_STRICT_REMOTE};return admit_policy(file,path,&context);}
bool setup_signed_executable(HANDLE file,const wchar_t* path,const BYTE publisher_sha256[32]){
    if(!path || file==INVALID_HANDLE_VALUE || !publisher_valid(publisher_sha256))return reject(ERROR_INVALID_PARAMETER);
    LARGE_INTEGER zero={0};return SetFilePointerEx(file,zero,NULL,FILE_BEGIN) && admit(file,path,(void*)publisher_sha256);
}
bool setup_release_prepare(const L4Layout* layout,const wchar_t* archive,const BYTE archive_sha256[32],const L4ReleaseFile* files,unsigned count,const BYTE publisher_sha256[32]){
    if(!publisher_valid(publisher_sha256))return reject(ERROR_INVALID_PARAMETER);
    return l4_release_unpack_publish_checked(layout,archive,archive_sha256,files,count,admit,(void*)publisher_sha256);
}
bool setup_release_verify(const L4Layout* layout,const L4ReleaseFile* files,unsigned count,const BYTE publisher_sha256[32]){
    if(!publisher_valid(publisher_sha256))return reject(ERROR_INVALID_PARAMETER);
    return l4_release_verify_checked(layout,files,count,admit,(void*)publisher_sha256);
}

bool setup_signed_executable_policy(HANDLE file,const wchar_t* path,const BYTE publisher[32],SetupAdmissionPolicy policy){
    if(!path || file==INVALID_HANDLE_VALUE || !publisher_valid(publisher) || !policy_valid(policy))return reject(ERROR_INVALID_PARAMETER);
    LARGE_INTEGER zero={0};AdmissionContext context={publisher,policy};return SetFilePointerEx(file,zero,NULL,FILE_BEGIN) && admit_policy(file,path,&context);
}
bool setup_release_prepare_policy(const L4Layout* layout,const wchar_t* archive,const BYTE digest[32],const L4ReleaseFile* files,unsigned count,const BYTE publisher[32],SetupAdmissionPolicy policy){
    if(!publisher_valid(publisher) || !policy_valid(policy))return reject(ERROR_INVALID_PARAMETER);AdmissionContext context={publisher,policy};
    return l4_release_unpack_publish_checked(layout,archive,digest,files,count,admit_policy,&context);
}
bool setup_release_verify_policy(const L4Layout* layout,const L4ReleaseFile* files,unsigned count,const BYTE publisher[32],SetupAdmissionPolicy policy){
    if(!publisher_valid(publisher) || !policy_valid(policy))return reject(ERROR_INVALID_PARAMETER);AdmissionContext context={publisher,policy};
    return l4_release_verify_checked(layout,files,count,admit_policy,&context);
}
