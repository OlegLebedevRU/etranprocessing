#ifndef L4DESK_SN_DISCOVERY_H
#define L4DESK_SN_DISCOVERY_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdbool.h>
#include <stddef.h>

int sn_discovery_query_once(int proxy_port, char* out_sn, size_t out_sn_size);
bool sn_discovery_wait_for_sn(int proxy_port, char* out_sn, size_t out_sn_size, HANDLE hStopEvent);

#endif /* L4DESK_SN_DISCOVERY_H */
