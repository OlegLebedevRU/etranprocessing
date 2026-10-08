#pragma once
#include "../../l4common/bootstrap.h"
bool setup_broker_environment_prepare(L4Journal* journal,ULONGLONG bootstrap);
bool setup_broker_environment_verify(const L4BootstrapPlan* plan);
