/**
 * @file schannel_tls.h
 * @brief Windows SChannel SSPI mTLS client implementation for Leo4Proxy.
 */

#ifndef LEO4_SCHANNEL_TLS_H
#define LEO4_SCHANNEL_TLS_H

#include "config.h"
#define SECURITY_WIN32
#include <security.h>
#include <schnlsp.h>
#include <stdbool.h>

typedef struct {
    CredHandle hCred;
    CtxtHandle hCtx;
    SOCKET sock;
    SecPkgContext_StreamSizes streamSizes;
    char targetHost[MAX_HOST_LEN];
    int targetPort;
    bool isConnected;
    bool isHandshakeComplete;

    /* Decryption buffer state */
    BYTE* recvBuf;
    DWORD recvBufAlloc;
    DWORD recvBufLen;

    /* Extra plaintext leftover buffer */
    BYTE* plainBuf;
    DWORD plainBufAlloc;
    DWORD plainBufLen;
    DWORD plainBufOffset;
} SChannelSession;

/**
 * @brief Initializes credentials handle with the client certificate from Windows Store.
 */
bool schannel_init_client_creds(PCCERT_CONTEXT pCert, int insecure_server, CredHandle* out_hCred);

/**
 * @brief Frees credentials handle.
 */
void schannel_free_creds(CredHandle* hCred);

/**
 * @brief Connects to remote host over TCP and establishes SChannel TLS / mTLS session.
 */
bool schannel_connect(SChannelSession* session, CredHandle* hCred, const char* host, int port, int timeout_ms, int insecure_server);

/**
 * @brief Encrypts and sends application data over SChannel TLS connection.
 * @return Number of bytes sent, or <= 0 on error.
 */
int schannel_send(SChannelSession* session, const void* data, int len);

/**
 * @brief Receives and decrypts application data from SChannel TLS connection.
 * @return Number of plaintext bytes received, 0 on graceful close, or < 0 on error.
 */
int schannel_recv(SChannelSession* session, void* out_data, int max_len);

/**
 * @brief Shuts down TLS session and closes underlying socket.
 */
void schannel_close(SChannelSession* session);

#endif /* LEO4_SCHANNEL_TLS_H */
