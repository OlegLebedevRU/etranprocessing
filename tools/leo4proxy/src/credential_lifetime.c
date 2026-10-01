#include "credential_lifetime.h"
#include <stdlib.h>

typedef struct CredentialEntry {
    CredHandle handle;
    CredHandle token;
    PCCERT_CONTEXT certificate;
    unsigned references;
    bool retired;
    struct CredentialEntry* next;
} CredentialEntry;
static SRWLOCK credential_lock = SRWLOCK_INIT;
static CredentialEntry* credentials;
static unsigned long long next_token;

static void dispose(CredentialEntry* entry) {
    FreeCredentialsHandle(&entry->handle);
    if (entry->certificate) CertFreeCertificateContext(entry->certificate);
    free(entry);
}

bool credential_register(CredHandle* handle, PCCERT_CONTEXT certificate) {
    if (!handle || !SecIsValidHandle(handle)) return false;
    CredentialEntry* entry = (CredentialEntry*)calloc(1, sizeof(*entry));
    if (!entry) return false;
    if (certificate && !(entry->certificate = CertDuplicateCertificateContext(certificate))) {
        free(entry);
        return false;
    }
    entry->handle = *handle;
    entry->references = 1;
    AcquireSRWLockExclusive(&credential_lock);
    /* The public handle is a unique token; SSPI handle reuse cannot revive a stale copy. */
    next_token++;
    entry->token.dwLower = (ULONG_PTR)(next_token & 0xFFFFFFFFULL);
    entry->token.dwUpper = (ULONG_PTR)(next_token >> 32);
    *handle = entry->token;
    entry->next = credentials;
    credentials = entry;
    ReleaseSRWLockExclusive(&credential_lock);
    return true;
}

void* credential_borrow(const CredHandle* handle) {
    if (!handle || !SecIsValidHandle(handle)) return NULL;
    CredentialEntry* found = NULL;
    AcquireSRWLockExclusive(&credential_lock);
    for (CredentialEntry* entry = credentials; entry; entry = entry->next) {
        if (!entry->retired && entry->token.dwLower == handle->dwLower && entry->token.dwUpper == handle->dwUpper) {
            entry->references++;
            found = entry;
            break;
        }
    }
    ReleaseSRWLockExclusive(&credential_lock);
    return found;
}

static CredentialEntry* remove_if_unused(CredentialEntry* entry) {
    if (entry->references) return NULL;
    CredentialEntry** link = &credentials;
    while (*link && *link != entry) link = &(*link)->next;
    if (*link) *link = entry->next;
    return entry;
}

CredHandle* credential_handle(void* lease) {
    return lease ? &((CredentialEntry*)lease)->handle : NULL;
}

void credential_release(void* lease) {
    if (!lease) return;
    AcquireSRWLockExclusive(&credential_lock);
    CredentialEntry* entry = (CredentialEntry*)lease;
    entry->references--;
    CredentialEntry* unused = remove_if_unused(entry);
    ReleaseSRWLockExclusive(&credential_lock);
    if (unused) dispose(unused);
}

void credential_retire(CredHandle* handle) {
    if (!handle || !SecIsValidHandle(handle)) return;
    CredentialEntry* unused = NULL;
    AcquireSRWLockExclusive(&credential_lock);
    for (CredentialEntry* entry = credentials; entry; entry = entry->next) {
        if (entry->token.dwLower == handle->dwLower && entry->token.dwUpper == handle->dwUpper) {
            if (!entry->retired) {
                entry->retired = true;
                entry->references--;
                unused = remove_if_unused(entry);
            }
            break;
        }
    }
    SecInvalidateHandle(handle);
    ReleaseSRWLockExclusive(&credential_lock);
    if (unused) dispose(unused);
}
