#pragma once
#include "root_manifest.h"
#include "../../l4common/recovery_task.h"
typedef struct {L4RecoveryHelper helper;BYTE publisher[32];bool owner_trusted;} SetupBootstrapReceipt;
/* Fixed helper name/ABI, owner signature first. binding is the authenticated
 * fresh root, or NULL only when reading an already protected initial receipt. */
bool setup_bootstrap_receipt_trusted(const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,
    const L4CatalogRelease* binding,const char* arch,SetupBootstrapReceipt* result);
/* Fixture/integration trust input only; result cannot authorize production installation. */
bool setup_bootstrap_receipt_signed(const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,
    const BYTE* public_key,DWORD public_size,const char* key_id,const L4CatalogRelease* binding,
    const char* arch,SetupBootstrapReceipt* result);
