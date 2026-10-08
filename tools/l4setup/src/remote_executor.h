#pragma once
#include "worker_entry.h"
/* Internal trusted composition, not enabled by worker_entry/main. One-hop only;
 * no argument/environment policy or fallback engine. Native signed fault
 * acceptance remains mandatory before root connects this to compiled capability. */
const SetupWorkerEngine* setup_remote_executor_engine(void);
