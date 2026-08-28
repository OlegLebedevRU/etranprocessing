#ifndef L4CON_MQTT_CLIENT_H
#define L4CON_MQTT_CLIENT_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <windows.h>
#include <stdbool.h>
#include "config.h"
#include "command_runner.h"

int mqtt_client_run(const AppConfig* config, HANDLE hStopEvent);

#endif /* L4CON_MQTT_CLIENT_H */
