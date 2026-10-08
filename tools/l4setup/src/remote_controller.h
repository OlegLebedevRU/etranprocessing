#pragma once
#include "remote_entry.h"
/* Compile-time controller, no runtime enable switch. Callers must keep worker
 * admission closed until its real fault-acceptance gate has passed. */
const SetupRemoteEngine* setup_remote_controller_engine(void);
