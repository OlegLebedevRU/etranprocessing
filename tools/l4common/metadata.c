#include "metadata.h"
#include "metadata_owner_key.h"
#include <bcrypt.h>
#include <string.h>
#pragma comment(lib,"bcrypt.lib")

static bool refuse(DWORD error){SetLastError(error);return false;}
const char* l4_metadata_key_id(void){return L4_METADATA_OWNER_KEY_ID;}
bool l4_metadata_verify_trusted(const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,BYTE digest[32]){
    return l4_metadata_verify(bytes,size,signature,signature_size,L4_METADATA_OWNER_KEY,sizeof(L4_METADATA_OWNER_KEY),digest);
}
bool l4_metadata_verify(const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,
    const BYTE* trusted_public,DWORD public_size,BYTE digest[32]){
    if(!digest)return refuse(ERROR_INVALID_PARAMETER);memset(digest,0,32);
    if(!bytes || !size || size>L4_METADATA_MAX_BYTES || !signature || signature_size!=L4_METADATA_SIGNATURE_BYTES ||
       !trusted_public || public_size!=L4_METADATA_PUBLIC_BYTES)return refuse(ERROR_INVALID_PARAMETER);
    BCRYPT_RSAKEY_BLOB header;memcpy(&header,trusted_public,sizeof(header));
    const BYTE* exponent=trusted_public+sizeof(header);const BYTE* modulus=exponent+3;
    if(header.Magic!=BCRYPT_RSAPUBLIC_MAGIC || header.BitLength!=3072 || header.cbPublicExp!=3 || header.cbModulus!=384 ||
       header.cbPrime1 || header.cbPrime2 || exponent[0]!=1 || exponent[1]!=0 || exponent[2]!=1 || !(modulus[0]&0x80) || !(modulus[383]&1))
        return refuse(ERROR_INVALID_DATA);
    BCRYPT_ALG_HANDLE sha=NULL,rsa=NULL;BCRYPT_HASH_HANDLE hash=NULL;BCRYPT_KEY_HANDLE key=NULL;BYTE computed[32]={0};
    BCRYPT_PKCS1_PADDING_INFO padding={BCRYPT_SHA256_ALGORITHM};
    bool ok=BCryptOpenAlgorithmProvider(&sha,BCRYPT_SHA256_ALGORITHM,NULL,0)==0;
    if(ok)ok=BCryptCreateHash(sha,&hash,NULL,0,NULL,0,0)==0;
    if(ok)ok=BCryptHashData(hash,(PUCHAR)bytes,size,0)==0;
    if(ok)ok=BCryptFinishHash(hash,computed,32,0)==0;
    if(ok)ok=BCryptOpenAlgorithmProvider(&rsa,BCRYPT_RSA_ALGORITHM,NULL,0)==0;
    if(ok)ok=BCryptImportKeyPair(rsa,NULL,BCRYPT_RSAPUBLIC_BLOB,&key,(PUCHAR)trusted_public,public_size,0)==0;
    if(ok)ok=BCryptVerifySignature(key,&padding,computed,32,(PUCHAR)signature,signature_size,BCRYPT_PAD_PKCS1)==0;
    if(key)BCryptDestroyKey(key);if(rsa)BCryptCloseAlgorithmProvider(rsa,0);
    if(hash)BCryptDestroyHash(hash);if(sha)BCryptCloseAlgorithmProvider(sha,0);
    if(!ok)return refuse(ERROR_INVALID_DATA);memcpy(digest,computed,32);return true;
}
