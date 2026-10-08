#pragma once
#include "update_metadata.h"
#include "../../l4common/remote_outcome.h"
/* INTERNAL: only an opaque whole-suite completion/restore producer may call
 * this after its final native recheck and settled guards. A caller-supplied
 * value/reference is never completion authority. Immutable identity is derived
 * from original92/93/64/69; exact proof102 or108 bytes must already be flushed.
 * No marker publication or clearing. SUCCESS error0; RESTORED original error.
 * REQUIRED is failure evidence only and carries no proof reference. */
bool setup_remote_outcome_append_bound(L4Journal* journal,const SetupOperationPlan* authenticated64,
    const L4RemoteOutcome* expected,ULONGLONG proof_reference,const BYTE proof_hash[32],L4RemoteOutcome* result);
