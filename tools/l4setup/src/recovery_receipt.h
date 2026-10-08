#pragma once
#include "bootstrap_receipt.h"
typedef struct SetupRecoveryReceipt SetupRecoveryReceipt;
/* SYSTEM/native roots + owned deployment lock; pinned immutable protected PF
 * helper and initial owner-signed receipt. No replacement or ACL repair. */
bool setup_recovery_receipt_load(L4Journal* original,const char* arch,SetupRecoveryReceipt** result);
/* Read-only fresh preflight: only missing destination under held authenticated
 * native PF ancestry is acceptable absence. Existing complete identity must match;
 * unsafe/partial/denied destination refuses. No create/adopt/repair/write. */
bool setup_recovery_receipt_check(L4Journal* original,const char* arch,const SetupBootstrapReceipt* expected,SetupAdmissionPolicy policy);
const L4RecoveryHelper* setup_recovery_receipt_helper(const SetupRecoveryReceipt* receipt);
void setup_recovery_receipt_free(SetupRecoveryReceipt* receipt);
/* Internal fresh installer producer; inputs must be held, authenticated assets.
 * Re-authenticates metadata; exact existing installation is only reusable, never
 * replaced. Caller maintains local admission policy, remote load remains strict. */
bool setup_recovery_receipt_install(L4Journal* original,const char* arch,const SetupBootstrapReceipt* expected,
    HANDLE helper,const void* document,DWORD document_size,const BYTE* signature,DWORD signature_size,SetupAdmissionPolicy policy);
