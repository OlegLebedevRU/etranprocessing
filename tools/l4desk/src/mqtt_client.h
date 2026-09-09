#ifndef L4DESK_MQTT_CLIENT_H
#define L4DESK_MQTT_CLIENT_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include "config.h"

int mqtt_client_run(const L4DeskConfig* config, HANDLE hStopEvent);

#endif /* L4DESK_MQTT_CLIENT_H */
