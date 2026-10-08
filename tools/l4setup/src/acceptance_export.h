#pragma once
#include "../../l4common/layout.h"
#include <stdbool.h>
#define SETUP_ACCEPTANCE_EXPORT_LIMIT 8192u
/* Primary SYSTEM/session0 only, after the local acceptance host/worker retire.
 * Existing exclusive deployment.lock and immutable original journal are held;
 * no create/repair/adoption of history or state. Authenticated local purpose,
 * signed operation, real102/108+103 and exact actual clear are mandatory.
 * Writes only fixed original-operation acceptance.result.json, CREATE_NEW,
 * owner SYSTEM / SYSTEM full / Administrators read. No caller verdict/path.
 * Observed terminal773/tenant1 are local stand scope attestation; the typed
 * outcome proves the shared executor's runtime gates, not a new IoT identity.
 * Repetition refuses an existing export instead of rewriting evidence. */
bool setup_acceptance_export(const L4Layout* roots,const wchar_t* operation);
