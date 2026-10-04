#include "upstream_probe.h"
#include <process.h>
#include <string.h>

static unsigned __stdcall probe_channel(void* value) {
    UpstreamProbe* probe=(UpstreamProbe*)value;
    ULONGLONG start=GetTickCount64(), deadline=start+UPSTREAM_PROBE_BUDGET_MS;
    const char* verdict="probe_failed";
    if (!probe->enabled) verdict="skipped";
    else if (probe->channel!=ENDPOINT_HTTPS && !probe->admission) verdict="policy_blocked";
    else if (probe->acquired) {
        bool rejected=false;
        Leo4Endpoint targets[ENDPOINT_MAX];
        int count=endpoints_candidates_timed(probe->config,probe->channel,probe->channel==ENDPOINT_HTTPS,targets,UPSTREAM_PROBE_BUDGET_MS);
        for (int n=0;n<count;n++) {
            ULONGLONG now=GetTickCount64();
            if (now>=deadline) { verdict="timeout"; break; }
            int share=endpoint_attempt_timeout(targets,count,n,(int)(deadline-now));
            if (share<1) continue;
            SChannelSession session; probe->selected=targets[n]; ++probe->attempts;
            /* TLS only: no admission runtime state, MQTT CONNECT or media data. */
            bool connected=schannel_connect_endpoint(&session,probe->creds,targets[n].host,
                endpoint_logical_name(probe->config,probe->channel),targets[n].port,share,false);
            rejected=rejected || session.certificate_rejected;
            if (connected) { schannel_close(&session); verdict="valid"; break; }
            verdict=rejected?"cert_invalid":GetTickCount64()>=deadline?"timeout":"probe_failed";
        }
    }
    strcpy_s(probe->verdict,sizeof(probe->verdict),verdict);
    probe->elapsed_ms=(DWORD)(GetTickCount64()-start);
    return 0;
}
void upstream_probe_all(UpstreamProbe probes[ENDPOINT_CHANNELS]) {
    HANDLE threads[ENDPOINT_CHANNELS]={0};
    for (int c=0;c<ENDPOINT_CHANNELS;c++) {
        strcpy_s(probes[c].verdict,sizeof(probes[c].verdict),"probe_failed");
        if (!probes[c].enabled || !probes[c].acquired || (c!=ENDPOINT_HTTPS && !probes[c].admission)) probe_channel(&probes[c]);
        else threads[c]=(HANDLE)_beginthreadex(NULL,0,probe_channel,&probes[c],0,NULL);
    }
    for (int c=0;c<ENDPOINT_CHANNELS;c++) if (threads[c]) {
        WaitForSingleObject(threads[c],INFINITE); CloseHandle(threads[c]);
    }
}
