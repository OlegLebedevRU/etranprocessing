#pragma once
#include <windows.h>
#include <stdbool.h>
#include <string.h>
/* Actual completed orphan exchange, not an externally supplied PASS assertion.
 * UTC values are Windows FILETIME ticks; IPC uses explicit LE offsets. */
#define L4_LINK_EVIDENCE_BYTES 244u
typedef struct {
    char request_nonce[40],rsp_correlation[40],event_nonce[40],eva_correlation[40],eva_request_nonce[40];
    ULONGLONG req_sent_utc,rsp_received_utc,evt_sent_utc,eva_received_utc;
    DWORD event_id,echo_event_id;
} L4LinkProbeEvidence;
static inline bool l4_link_nonce_valid(const char nonce[40]){
    bool nonzero=false;for(unsigned i=0;i<36;i++){char c=nonce[i];
        if(i==8 || i==13 || i==18 || i==23){if(c!='-')return false;}
        else if((c>='0' && c<='9') || (c>='a' && c<='f')){if(c!='0')nonzero=true;}else return false;
    }
    return nonzero && !nonce[36] && !nonce[37] && !nonce[38] && !nonce[39];
}
static inline bool l4_link_evidence_valid(const L4LinkProbeEvidence* e){
    return e && l4_link_nonce_valid(e->request_nonce) && l4_link_nonce_valid(e->event_nonce) &&
        !memcmp(e->request_nonce,e->rsp_correlation,40) && !memcmp(e->event_nonce,e->eva_correlation,40) &&
        !memcmp(e->request_nonce,e->eva_request_nonce,40) && strcmp(e->request_nonce,e->event_nonce) &&
        e->event_id && e->event_id<=0x7fffffffU && e->echo_event_id==e->event_id && e->req_sent_utc &&
        e->req_sent_utc<e->rsp_received_utc && e->rsp_received_utc<=e->evt_sent_utc &&
        e->evt_sent_utc<e->eva_received_utc && e->eva_received_utc-e->req_sent_utc<=3000000000ull;
}
static inline bool l4_link_evidence_encode(const L4LinkProbeEvidence* e,BYTE bytes[L4_LINK_EVIDENCE_BYTES]){
    if(!bytes || !l4_link_evidence_valid(e))return false;memset(bytes,0,L4_LINK_EVIDENCE_BYTES);DWORD version=1;
    memcpy(bytes,&version,4);memcpy(bytes+4,e->request_nonce,40);memcpy(bytes+44,e->rsp_correlation,40);
    memcpy(bytes+84,e->event_nonce,40);memcpy(bytes+124,e->eva_correlation,40);memcpy(bytes+164,e->eva_request_nonce,40);
    memcpy(bytes+204,&e->req_sent_utc,8);memcpy(bytes+212,&e->rsp_received_utc,8);
    memcpy(bytes+220,&e->evt_sent_utc,8);memcpy(bytes+228,&e->eva_received_utc,8);memcpy(bytes+236,&e->event_id,4);memcpy(bytes+240,&e->echo_event_id,4);return true;
}
static inline bool l4_link_evidence_decode(const BYTE bytes[L4_LINK_EVIDENCE_BYTES],L4LinkProbeEvidence* e){
    if(!e)return false;memset(e,0,sizeof(*e));if(!bytes)return false;DWORD version=0;memcpy(&version,bytes,4);if(version!=1)return false;
    memcpy(e->request_nonce,bytes+4,40);memcpy(e->rsp_correlation,bytes+44,40);memcpy(e->event_nonce,bytes+84,40);
    memcpy(e->eva_correlation,bytes+124,40);memcpy(e->eva_request_nonce,bytes+164,40);
    memcpy(&e->req_sent_utc,bytes+204,8);memcpy(&e->rsp_received_utc,bytes+212,8);
    memcpy(&e->evt_sent_utc,bytes+220,8);memcpy(&e->eva_received_utc,bytes+228,8);memcpy(&e->event_id,bytes+236,4);memcpy(&e->echo_event_id,bytes+240,4);
    if(l4_link_evidence_valid(e))return true;memset(e,0,sizeof(*e));return false;
}
