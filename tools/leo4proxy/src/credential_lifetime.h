#ifndef LEO4_CREDENTIAL_LIFETIME_H
#define LEO4_CREDENTIAL_LIFETIME_H
#include "schannel_tls.h"
/* One owner per acquired SSPI handle; sessions keep retired credentials alive. */
bool credential_register(CredHandle* handle, PCCERT_CONTEXT certificate);
CredHandle* credential_handle(void* lease);
void* credential_borrow(const CredHandle* handle);
void credential_release(void* lease);
void credential_retire(CredHandle* handle);
#endif
