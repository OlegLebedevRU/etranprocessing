#ifndef LEO4_SVC_SERVICE_MGR_H
#define LEO4_SVC_SERVICE_MGR_H

#include "config.h"

bool service_install_or_update(const AppConfig* config, int argc, char* argv[]);
bool service_uninstall(void);
bool service_start(void);
bool service_stop(void);
bool service_restart(void);
void service_query_status(void);
bool service_run_dispatcher(const AppConfig* config);

#endif /* LEO4_SVC_SERVICE_MGR_H */
