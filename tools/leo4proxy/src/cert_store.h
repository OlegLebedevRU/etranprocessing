/**
 * @file cert_store.h
 * @brief Windows Certificate Store inspection and certificate selection for Leo4Proxy.
 */

#ifndef LEO4_CERT_STORE_H
#define LEO4_CERT_STORE_H

#include "config.h"
#include <wincrypt.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char subject[512];
    char issuer[512];
    char serial[128];
    char thumbprint[64];
    char email[128];
    char sn[128];
    char urn[128];
    char san_dns[256];
    char san_dns_list[8][128];
    int san_dns_count;
    char local_hostname[256];
    char not_before[64];
    char not_after[64];
    FILETIME ft_not_before;
    FILETIME ft_not_after;
    bool has_private_key;
    PCCERT_CONTEXT pCertContext;
} CertDetails;

/**
 * @brief Searches the Windows Certificate Store for the newest terminal certificate.
 * 
 * Priority order:
 * 1. Exact match by thumbprint (if specified).
 * 2. Match by custom email pattern (if specified).
 * 3. Primary default: ends with @leo4.ru (newest NotBefore).
 * 4. Fallback default: ends with @forpay.ru (newest NotBefore).
 * 
 * @param config Configuration options.
 * @param out_details Output structure with certificate details and PCCERT_CONTEXT handle.
 * @return true if a valid certificate with private key was found, false otherwise.
 */
bool cert_store_find_best_cert(const ProxyConfig* config, CertDetails* out_details);

/**
 * @brief Frees resources allocated in CertDetails (e.g. pCertContext).
 */
void cert_store_free_details(CertDetails* details);

/**
 * @brief Prints certificate details to stdout.
 */
void cert_store_print_details(const CertDetails* details);

/**
 * @brief Helper to query just the active Device SN from the store.
 */
bool cert_store_get_active_sn(char* out_sn, size_t out_sn_size);

/**
 * @brief Checks if a certificate has expired (NotAfter < CurrentSystemTime).
 */
bool cert_store_is_cert_expired(const CertDetails* details);

#endif /* LEO4_CERT_STORE_H */
