#pragma once
#include "installed_source.h"
#include "../../l4common/remote_host.h"
/* Compile-time engine wiring only. Preflight must check supported actual work
 * without changing the journal or waiting on IoT: the console may be waiting for ACK in its MQTT callback.
 * execute owns subsequent durable progress/results and external worker Job; the
 * SYSTEM controller retains the deployment journal until the engine transfers it.
 * NULL or incomplete engine refuses BEFORE93, never accepts a no-op operation. */
typedef struct {
    bool (*preflight)(L4Journal* journal,SetupInstalledSource* source,const L4RemoteRequest* request);
    DWORD (*execute)(L4Journal** journal,SetupInstalledSource* source,const L4RemoteRequest* request,const volatile LONG* cancelled);
} SetupRemoteEngine;
bool setup_remote_entry(int argc,wchar_t** argv,const SetupRemoteEngine* engine,DWORD* result);
