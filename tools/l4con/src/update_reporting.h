#pragma once
#include "update_status.h"
typedef struct L4UpdateReporting L4UpdateReporting;
typedef bool (*L4ReportingReady)(void* context);
typedef void (*L4ReportingReply)(void* context,const char* task,int code,const char* json);
typedef bool (*L4ReportingEvent)(void* context,const char* json,unsigned id,const char* correlation);
/* Read-only asynchronous snapshots, existing client only. No outbox or ACK gate. */
bool update_reporting_start(const L4Layout* roots,HANDLE stop,L4ReportingReady ready,
    L4ReportingReply reply,L4ReportingEvent event,void* context,L4UpdateReporting** output);
bool update_reporting_status(L4UpdateReporting* reporting,const char* task,const char* operation);
bool update_reporting_close(L4UpdateReporting** reporting,DWORD timeout);
