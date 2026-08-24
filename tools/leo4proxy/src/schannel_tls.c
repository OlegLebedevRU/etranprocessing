/**
 * @file schannel_tls.c
 * @brief Windows SChannel SSPI mTLS client implementation for Leo4Proxy.
 */

#include "schannel_tls.h"
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "secur32.lib")

bool schannel_init_client_creds(PCCERT_CONTEXT pCert, int insecure_server, CredHandle* out_hCred) {
    if (!out_hCred) return false;
    SecInvalidateHandle(out_hCred);

    SCHANNEL_CRED cred = { 0 };
    cred.dwVersion = SCHANNEL_CRED_VERSION;
    cred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT;
    cred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS;

    if (pCert) {
        cred.cCreds = 1;
        cred.paCred = &pCert;
    }

    if (insecure_server) {
        cred.dwFlags |= SCH_CRED_MANUAL_CRED_VALIDATION |
                        SCH_CRED_IGNORE_NO_REVOCATION_CHECK |
                        SCH_CRED_IGNORE_REVOCATION_OFFLINE;
    }

    TimeStamp tsExpiry;
    SECURITY_STATUS ss = AcquireCredentialsHandleA(
        NULL,
        UNISP_NAME_A,
        SECPKG_CRED_OUTBOUND,
        NULL,
        &cred,
        NULL,
        NULL,
        out_hCred,
        &tsExpiry
    );

    if (ss != SEC_E_OK) {
        fprintf(stderr, "[SCHANNEL] AcquireCredentialsHandle failed: 0x%08lX\n", ss);
        return false;
    }

    return true;
}

bool schannel_init_server_creds(PCCERT_CONTEXT pCert, CredHandle* out_hCred) {
    if (!pCert || !out_hCred) return false;

    SCHANNEL_CRED schannelCred = { 0 };
    schannelCred.dwVersion = SCHANNEL_CRED_VERSION;
    schannelCred.cCreds = 1;
    schannelCred.paCred = &pCert;
    schannelCred.grbitEnabledProtocols = SP_PROT_TLS1_2_SERVER | SP_PROT_TLS1_3_SERVER;
    // Local server accepts client connection and tolerates client certificates if offered without failing
    schannelCred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_IGNORE_NO_REVOCATION_CHECK;

    TimeStamp tsExpiry;
    SECURITY_STATUS ss = AcquireCredentialsHandleA(
        NULL,
        UNISP_NAME_A,
        SECPKG_CRED_INBOUND,
        NULL,
        &schannelCred,
        NULL,
        NULL,
        out_hCred,
        &tsExpiry
    );

    if (ss != SEC_E_OK) {
        fprintf(stderr, "[SCHANNEL] AcquireCredentialsHandle(INBOUND) failed: 0x%08lX\n", ss);
        return false;
    }

    return true;
}

void schannel_free_creds(CredHandle* hCred) {
    if (hCred && SecIsValidHandle(hCred)) {
        FreeCredentialsHandle(hCred);
        SecInvalidateHandle(hCred);
    }
}

static SOCKET tcp_connect(const char* host, int port, int timeout_ms) {
    struct addrinfo hints = { 0 };
    struct addrinfo* res = NULL;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        fprintf(stderr, "[TCP] DNS lookup failed for '%s'\n", host);
        return INVALID_SOCKET;
    }

    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) {
        freeaddrinfo(res);
        return INVALID_SOCKET;
    }

    // Set non-blocking for connect timeout
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);

    int rc = connect(s, res->ai_addr, (int)res->ai_addrlen);
    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
            fd_set write_fds, err_fds;
            FD_ZERO(&write_fds);
            FD_ZERO(&err_fds);
            FD_SET(s, &write_fds);
            FD_SET(s, &err_fds);

            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;

            int sel = select((int)s + 1, NULL, &write_fds, &err_fds, &tv);
            if (sel <= 0 || FD_ISSET(s, &err_fds)) {
                closesocket(s);
                freeaddrinfo(res);
                return INVALID_SOCKET;
            }
        } else {
            closesocket(s);
            freeaddrinfo(res);
            return INVALID_SOCKET;
        }
    }

    // Switch back to blocking mode
    mode = 0;
    ioctlsocket(s, FIONBIO, &mode);

    // Set receive/send timeouts
    DWORD sockTimeout = (DWORD)timeout_ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&sockTimeout, sizeof(sockTimeout));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&sockTimeout, sizeof(sockTimeout));

    // Disable Nagle algorithm
    BOOL nodelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

    freeaddrinfo(res);
    return s;
}

static int send_all(SOCKET s, const BYTE* buf, int len) {
    int total = 0;
    while (total < len) {
        int sent = send(s, (const char*)(buf + total), len - total, 0);
        if (sent <= 0) return sent;
        total += sent;
    }
    return total;
}

static bool perform_handshake(SChannelSession* session, CredHandle* hCred, const char* host, int insecure_server) {
    SecBuffer outBuffer;
    outBuffer.BufferType = SECBUFFER_TOKEN;
    outBuffer.cbBuffer = 0;
    outBuffer.pvBuffer = NULL;

    SecBufferDesc outDesc;
    outDesc.ulVersion = SECBUFFER_VERSION;
    outDesc.cBuffers = 1;
    outDesc.pBuffers = &outBuffer;

    DWORD reqFlags = ISC_REQ_SEQUENCE_DETECT |
                     ISC_REQ_REPLAY_DETECT |
                     ISC_REQ_CONFIDENTIALITY |
                     ISC_REQ_EXTENDED_ERROR |
                     ISC_REQ_ALLOCATE_MEMORY |
                     ISC_REQ_STREAM;

    if (insecure_server) {
        reqFlags |= ISC_REQ_MANUAL_CRED_VALIDATION;
    }

    DWORD ctxFlags = 0;
    TimeStamp tsExpiry;

    // Step 1: Initial client token
    SECURITY_STATUS ss = InitializeSecurityContextA(
        hCred,
        NULL,
        (SEC_CHAR*)host,
        reqFlags,
        0,
        0,
        NULL,
        0,
        &session->hCtx,
        &outDesc,
        &ctxFlags,
        &tsExpiry
    );

    if (ss != SEC_I_CONTINUE_NEEDED && ss != SEC_E_OK) {
        fprintf(stderr, "[SCHANNEL] InitializeSecurityContext (initial) failed: 0x%08lX\n", ss);
        return false;
    }

    if (outBuffer.cbBuffer > 0 && outBuffer.pvBuffer) {
        int sent = send_all(session->sock, (const BYTE*)outBuffer.pvBuffer, (int)outBuffer.cbBuffer);
        FreeContextBuffer(outBuffer.pvBuffer);
        outBuffer.pvBuffer = NULL;
        if (sent <= 0) {
            fprintf(stderr, "[SCHANNEL] Failed to send initial handshake token\n");
            return false;
        }
    }

    if (ss == SEC_E_OK) {
        return true;
    }

    // Step 2: Handshake loop
    BYTE inTokenBuf[16384];
    DWORD inTokenLen = 0;

    while (ss == SEC_I_CONTINUE_NEEDED || ss == SEC_E_INCOMPLETE_MESSAGE || ss == SEC_I_INCOMPLETE_CREDENTIALS) {
        if (inTokenLen == 0 || ss == SEC_E_INCOMPLETE_MESSAGE) {
            int received = recv(session->sock, (char*)(inTokenBuf + inTokenLen), (int)(sizeof(inTokenBuf) - inTokenLen), 0);
            if (received <= 0) {
                fprintf(stderr, "[SCHANNEL] Handshake recv failed or server disconnected (rc=%d, wsa=%d)\n", received, WSAGetLastError());
                return false;
            }
            inTokenLen += (DWORD)received;
        }

        SecBuffer inBuffers[2];
        inBuffers[0].BufferType = SECBUFFER_TOKEN;
        inBuffers[0].cbBuffer = inTokenLen;
        inBuffers[0].pvBuffer = inTokenBuf;

        inBuffers[1].BufferType = SECBUFFER_EMPTY;
        inBuffers[1].cbBuffer = 0;
        inBuffers[1].pvBuffer = NULL;

        SecBufferDesc inDesc;
        inDesc.ulVersion = SECBUFFER_VERSION;
        inDesc.cBuffers = 2;
        inDesc.pBuffers = inBuffers;

        outBuffer.BufferType = SECBUFFER_TOKEN;
        outBuffer.cbBuffer = 0;
        outBuffer.pvBuffer = NULL;

        outDesc.cBuffers = 1;
        outDesc.pBuffers = &outBuffer;

        ss = InitializeSecurityContextA(
            hCred,
            &session->hCtx,
            (SEC_CHAR*)host,
            reqFlags,
            0,
            0,
            &inDesc,
            0,
            NULL,
            &outDesc,
            &ctxFlags,
            &tsExpiry
        );

        if (ss == SEC_E_INCOMPLETE_MESSAGE) {
            continue;
        }

        if (outBuffer.cbBuffer > 0 && outBuffer.pvBuffer) {
            int sent = send_all(session->sock, (const BYTE*)outBuffer.pvBuffer, (int)outBuffer.cbBuffer);
            FreeContextBuffer(outBuffer.pvBuffer);
            outBuffer.pvBuffer = NULL;
            if (sent <= 0) {
                fprintf(stderr, "[SCHANNEL] Failed to send handshake response token\n");
                return false;
            }
        }

        if (ss == SEC_E_OK) {
            // Handle any extra bytes received beyond handshake token
            if (inBuffers[1].BufferType == SECBUFFER_EXTRA && inBuffers[1].cbBuffer > 0) {
                DWORD extraLen = inBuffers[1].cbBuffer;
                memmove(session->recvBuf, inTokenBuf + (inTokenLen - extraLen), extraLen);
                session->recvBufLen = extraLen;
            } else {
                session->recvBufLen = 0;
            }
            return true;
        }

        if (ss == SEC_I_CONTINUE_NEEDED) {
            if (inBuffers[1].BufferType == SECBUFFER_EXTRA && inBuffers[1].cbBuffer > 0) {
                DWORD extraLen = inBuffers[1].cbBuffer;
                memmove(inTokenBuf, inTokenBuf + (inTokenLen - extraLen), extraLen);
                inTokenLen = extraLen;
            } else {
                inTokenLen = 0;
            }
            continue;
        }

        if (FAILED(ss)) {
            fprintf(stderr, "[SCHANNEL] Handshake failed: 0x%08lX\n", ss);
            return false;
        }
    }

    return (ss == SEC_E_OK);
}

bool schannel_connect(SChannelSession* session, CredHandle* hCred, const char* host, int port, int timeout_ms, int insecure_server) {
    if (!session || !hCred || !host) return false;
    memset(session, 0, sizeof(SChannelSession));
    SecInvalidateHandle(&session->hCtx);
    session->sock = INVALID_SOCKET;

    strncpy_s(session->targetHost, sizeof(session->targetHost), host, _TRUNCATE);
    session->targetPort = port;

    // 1. Establish plain TCP connection
    session->sock = tcp_connect(host, port, timeout_ms);
    if (session->sock == INVALID_SOCKET) {
        return false;
    }

    // Allocate receive buffer
    session->recvBufAlloc = PROXY_BUFFER_SIZE;
    session->recvBuf = (BYTE*)malloc(session->recvBufAlloc);
    session->recvBufLen = 0;

    session->plainBufAlloc = PROXY_BUFFER_SIZE;
    session->plainBuf = (BYTE*)malloc(session->plainBufAlloc);
    session->plainBufLen = 0;
    session->plainBufOffset = 0;

    if (!session->recvBuf || !session->plainBuf) {
        schannel_close(session);
        return false;
    }

    // 2. Perform SChannel mTLS Handshake
    if (!perform_handshake(session, hCred, host, insecure_server)) {
        schannel_close(session);
        return false;
    }

    // 3. Query stream sizes
    SECURITY_STATUS ss = QueryContextAttributesA(&session->hCtx, SECPKG_ATTR_STREAM_SIZES, &session->streamSizes);
    if (ss != SEC_E_OK) {
        fprintf(stderr, "[SCHANNEL] QueryContextAttributes(STREAM_SIZES) failed: 0x%08lX\n", ss);
        schannel_close(session);
        return false;
    }

    // Reset socket timeouts to 0 (infinite) after handshake so select() multiplexes safely without WSAETIMEDOUT
    DWORD zeroTimeout = 0;
    setsockopt(session->sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&zeroTimeout, sizeof(zeroTimeout));
    setsockopt(session->sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&zeroTimeout, sizeof(zeroTimeout));

    BOOL keepAlive = TRUE;
    setsockopt(session->sock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&keepAlive, sizeof(keepAlive));

    session->isConnected = true;
    session->isHandshakeComplete = true;
    return true;
}

bool schannel_accept(SChannelSession* session, const CredHandle* hServerCred, SOCKET clientSock) {
    if (!session || !hServerCred || clientSock == INVALID_SOCKET) return false;
    memset(session, 0, sizeof(SChannelSession));
    session->sock = clientSock;
    SecInvalidateHandle(&session->hCtx);

    session->recvBufAlloc = 65536;
    session->plainBufAlloc = 65536;
    session->recvBuf = (BYTE*)malloc(session->recvBufAlloc);
    session->plainBuf = (BYTE*)malloc(session->plainBufAlloc);
    if (!session->recvBuf || !session->plainBuf) {
        schannel_close(session);
        return false;
    }

    DWORD fContextReq = ASC_REQ_SEQUENCE_DETECT |
                        ASC_REQ_REPLAY_DETECT |
                        ASC_REQ_CONFIDENTIALITY |
                        ASC_REQ_EXTENDED_ERROR |
                        ASC_REQ_ALLOCATE_MEMORY |
                        ASC_REQ_STREAM;

    bool firstCall = true;
    SECURITY_STATUS ss = SEC_I_CONTINUE_NEEDED;

    while (ss == SEC_I_CONTINUE_NEEDED || ss == SEC_E_INCOMPLETE_MESSAGE) {
        if (session->recvBufLen == 0 || ss == SEC_E_INCOMPLETE_MESSAGE) {
            int received = recv(session->sock, (char*)(session->recvBuf + session->recvBufLen),
                                (int)(session->recvBufAlloc - session->recvBufLen), 0);
            if (received <= 0) {
                fprintf(stderr, "[SCHANNEL-SERVER] recv failed during handshake: %d\n", WSAGetLastError());
                schannel_close(session);
                return false;
            }
            session->recvBufLen += (DWORD)received;
        }

        SecBuffer inBuffers[2];
        inBuffers[0].BufferType = SECBUFFER_TOKEN;
        inBuffers[0].cbBuffer = session->recvBufLen;
        inBuffers[0].pvBuffer = session->recvBuf;

        inBuffers[1].BufferType = SECBUFFER_EMPTY;
        inBuffers[1].cbBuffer = 0;
        inBuffers[1].pvBuffer = NULL;

        SecBufferDesc inDesc;
        inDesc.ulVersion = SECBUFFER_VERSION;
        inDesc.cBuffers = 2;
        inDesc.pBuffers = inBuffers;

        SecBuffer outBuffers[1];
        outBuffers[0].BufferType = SECBUFFER_TOKEN;
        outBuffers[0].cbBuffer = 0;
        outBuffers[0].pvBuffer = NULL;

        SecBufferDesc outDesc;
        outDesc.ulVersion = SECBUFFER_VERSION;
        outDesc.cBuffers = 1;
        outDesc.pBuffers = outBuffers;

        DWORD fContextAttr = 0;
        TimeStamp tsExpiry;

        ss = AcceptSecurityContext(
            (PCredHandle)hServerCred,
            firstCall ? NULL : &session->hCtx,
            &inDesc,
            fContextReq,
            SECURITY_NATIVE_DREP,
            &session->hCtx,
            &outDesc,
            &fContextAttr,
            &tsExpiry
        );

        firstCall = false;

        // Send output token if generated by AcceptSecurityContext
        if (outBuffers[0].cbBuffer > 0 && outBuffers[0].pvBuffer) {
            int s = send_all(session->sock, (const BYTE*)outBuffers[0].pvBuffer, (int)outBuffers[0].cbBuffer);
            FreeContextBuffer(outBuffers[0].pvBuffer);
            outBuffers[0].pvBuffer = NULL;
            if (s <= 0) {
                fprintf(stderr, "[SCHANNEL-SERVER] send_all token failed: %d\n", WSAGetLastError());
                schannel_close(session);
                return false;
            }
        }

        if (ss == SEC_E_INCOMPLETE_MESSAGE) {
            continue;
        }

        if (ss == SEC_E_OK || ss == SEC_I_CONTINUE_NEEDED) {
            if (inBuffers[1].BufferType == SECBUFFER_EXTRA && inBuffers[1].cbBuffer > 0) {
                DWORD extraLen = inBuffers[1].cbBuffer;
                memmove(session->recvBuf, (BYTE*)session->recvBuf + (session->recvBufLen - extraLen), extraLen);
                session->recvBufLen = extraLen;
            } else {
                session->recvBufLen = 0;
            }

            if (ss == SEC_E_OK) {
                break; // Inbound handshake complete!
            }
        } else {
            fprintf(stderr, "[SCHANNEL-SERVER] AcceptSecurityContext failed: 0x%08lX\n", ss);
            schannel_close(session);
            return false;
        }
    }

    // Query stream sizes
    SECURITY_STATUS ssSizes = QueryContextAttributesA(&session->hCtx, SECPKG_ATTR_STREAM_SIZES, &session->streamSizes);
    if (ssSizes != SEC_E_OK) {
        fprintf(stderr, "[SCHANNEL-SERVER] QueryContextAttributes(STREAM_SIZES) failed: 0x%08lX\n", ssSizes);
        schannel_close(session);
        return false;
    }

    // Reset socket timeouts
    DWORD zeroTimeout = 0;
    setsockopt(session->sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&zeroTimeout, sizeof(zeroTimeout));
    setsockopt(session->sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&zeroTimeout, sizeof(zeroTimeout));

    BOOL keepAlive = TRUE;
    setsockopt(session->sock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&keepAlive, sizeof(keepAlive));

    session->isConnected = true;
    session->isHandshakeComplete = true;
    return true;
}

int schannel_send(SChannelSession* session, const void* data, int len) {
    if (!session || !session->isConnected || session->sock == INVALID_SOCKET || len <= 0) {
        return -1;
    }

    const BYTE* src = (const BYTE*)data;
    int remaining = len;
    DWORD maxMsg = session->streamSizes.cbMaximumMessage;
    if (maxMsg == 0 || maxMsg > 16384) maxMsg = 16384;

    DWORD allocNeeded = session->streamSizes.cbHeader + maxMsg + session->streamSizes.cbTrailer;
    BYTE* sendBuffer = (BYTE*)malloc(allocNeeded);
    if (!sendBuffer) return -1;

    int totalSent = 0;

    while (remaining > 0) {
        DWORD chunk = (DWORD)remaining;
        if (chunk > maxMsg) chunk = maxMsg;

        SecBuffer buffers[4];
        buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
        buffers[0].cbBuffer = session->streamSizes.cbHeader;
        buffers[0].pvBuffer = sendBuffer;

        buffers[1].BufferType = SECBUFFER_DATA;
        buffers[1].cbBuffer = chunk;
        buffers[1].pvBuffer = sendBuffer + session->streamSizes.cbHeader;
        memcpy(buffers[1].pvBuffer, src + totalSent, chunk);

        buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
        buffers[2].cbBuffer = session->streamSizes.cbTrailer;
        buffers[2].pvBuffer = sendBuffer + session->streamSizes.cbHeader + chunk;

        buffers[3].BufferType = SECBUFFER_EMPTY;
        buffers[3].cbBuffer = 0;
        buffers[3].pvBuffer = NULL;

        SecBufferDesc desc;
        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers = 4;
        desc.pBuffers = buffers;

        SECURITY_STATUS ss = EncryptMessage(&session->hCtx, 0, &desc, 0);
        if (ss != SEC_E_OK) {
            fprintf(stderr, "[SCHANNEL] EncryptMessage failed: 0x%08lX\n", ss);
            free(sendBuffer);
            return -1;
        }

        int frameLen = (int)(buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer);
        int sent = send_all(session->sock, sendBuffer, frameLen);
        if (sent <= 0) {
            free(sendBuffer);
            return sent;
        }

        totalSent += (int)chunk;
        remaining -= (int)chunk;
    }

    free(sendBuffer);
    return totalSent;
}

int schannel_recv(SChannelSession* session, void* out_data, int max_len) {
    if (!session || !session->isConnected || session->sock == INVALID_SOCKET || max_len <= 0) {
        return -1;
    }

    BYTE* dst = (BYTE*)out_data;

    // 1. If leftover decrypted plaintext is cached, return it first
    if (session->plainBufLen > session->plainBufOffset) {
        DWORD avail = session->plainBufLen - session->plainBufOffset;
        DWORD toCopy = (DWORD)max_len;
        if (toCopy > avail) toCopy = avail;

        memcpy(dst, session->plainBuf + session->plainBufOffset, toCopy);
        session->plainBufOffset += toCopy;

        if (session->plainBufOffset >= session->plainBufLen) {
            session->plainBufLen = 0;
            session->plainBufOffset = 0;
        }
        return (int)toCopy;
    }

    while (true) {
        // If receive buffer is empty, read from network
        if (session->recvBufLen == 0) {
            int received = recv(session->sock, (char*)session->recvBuf, (int)session->recvBufAlloc, 0);
            if (received <= 0) {
                if (received == 0) {
                    session->isConnected = false;
                    return 0; // Graceful close
                }
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK) {
                    return 0;
                }
                session->isConnected = false;
                return -1; // Error
            }
            session->recvBufLen = (DWORD)received;
        }

        SecBuffer buffers[4];
        buffers[0].BufferType = SECBUFFER_DATA;
        buffers[0].cbBuffer = session->recvBufLen;
        buffers[0].pvBuffer = session->recvBuf;

        buffers[1].BufferType = SECBUFFER_EMPTY;
        buffers[1].cbBuffer = 0;
        buffers[1].pvBuffer = NULL;

        buffers[2].BufferType = SECBUFFER_EMPTY;
        buffers[2].cbBuffer = 0;
        buffers[2].pvBuffer = NULL;

        buffers[3].BufferType = SECBUFFER_EMPTY;
        buffers[3].cbBuffer = 0;
        buffers[3].pvBuffer = NULL;

        SecBufferDesc desc;
        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers = 4;
        desc.pBuffers = buffers;

        ULONG qop = 0;
        SECURITY_STATUS ss = DecryptMessage(&session->hCtx, &desc, 0, &qop);

        if (ss == SEC_E_INCOMPLETE_MESSAGE) {
            // Need more data from socket
            if (session->recvBufAlloc - session->recvBufLen < 4096) {
                session->recvBufAlloc += 16384;
                BYTE* newBuf = (BYTE*)realloc(session->recvBuf, session->recvBufAlloc);
                if (!newBuf) return -1;
                session->recvBuf = newBuf;
            }

            int received = recv(
                session->sock,
                (char*)(session->recvBuf + session->recvBufLen),
                (int)(session->recvBufAlloc - session->recvBufLen),
                0
            );

            if (received <= 0) {
                if (received == 0) {
                    session->isConnected = false;
                    return 0;
                }
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK) {
                    return 0;
                }
                session->isConnected = false;
                return -1;
            }
            session->recvBufLen += (DWORD)received;
            continue;
        }

        if (ss == SEC_I_CONTEXT_EXPIRED) {
            session->isConnected = false;
            return 0;
        }

        if (ss != SEC_E_OK && ss != SEC_I_RENEGOTIATE) {
            fprintf(stderr, "[SCHANNEL] DecryptMessage failed: 0x%08lX\n", ss);
            session->isConnected = false;
            return -1;
        }

        // Locate data buffer (decrypted payload) and extra buffer
        SecBuffer* pDataBuffer = NULL;
        SecBuffer* pExtraBuffer = NULL;

        for (int i = 0; i < 4; i++) {
            if (buffers[i].BufferType == SECBUFFER_DATA) {
                pDataBuffer = &buffers[i];
            } else if (buffers[i].BufferType == SECBUFFER_EXTRA) {
                pExtraBuffer = &buffers[i];
            }
        }

        DWORD plainLen = pDataBuffer ? pDataBuffer->cbBuffer : 0;
        BYTE* plainData = pDataBuffer ? (BYTE*)pDataBuffer->pvBuffer : NULL;

        DWORD returnedBytes = 0;
        if (plainLen > 0 && plainData) {
            DWORD toReturn = (DWORD)max_len;
            if (toReturn > plainLen) toReturn = plainLen;

            memcpy(dst, plainData, toReturn);
            returnedBytes = toReturn;

            // If decrypted data exceeds caller buffer, stash remainder in plainBuf
            if (plainLen > toReturn) {
                DWORD remainder = plainLen - toReturn;
                if (remainder > session->plainBufAlloc) {
                    session->plainBufAlloc = remainder + 4096;
                    session->plainBuf = (BYTE*)realloc(session->plainBuf, session->plainBufAlloc);
                }
                if (session->plainBuf) {
                    memcpy(session->plainBuf, plainData + toReturn, remainder);
                    session->plainBufLen = remainder;
                    session->plainBufOffset = 0;
                }
            }
        }

        // Handle extra ciphertext remaining in stream (MUST be done AFTER saving plainData!)
        if (pExtraBuffer && pExtraBuffer->cbBuffer > 0 && pExtraBuffer->pvBuffer) {
            DWORD extraLen = pExtraBuffer->cbBuffer;
            memmove(session->recvBuf, pExtraBuffer->pvBuffer, extraLen);
            session->recvBufLen = extraLen;
        } else {
            session->recvBufLen = 0;
        }

        if (returnedBytes > 0) {
            return (int)returnedBytes;
        }

        if (ss == SEC_I_RENEGOTIATE) {
            continue;
        }
    }
}

void schannel_close(SChannelSession* session) {
    if (!session) return;

    if (SecIsValidHandle(&session->hCtx)) {
        DeleteSecurityContext(&session->hCtx);
        SecInvalidateHandle(&session->hCtx);
    }

    if (session->sock != INVALID_SOCKET) {
        shutdown(session->sock, SD_BOTH);
        closesocket(session->sock);
        session->sock = INVALID_SOCKET;
    }

    if (session->recvBuf) {
        free(session->recvBuf);
        session->recvBuf = NULL;
    }
    if (session->plainBuf) {
        free(session->plainBuf);
        session->plainBuf = NULL;
    }

    session->recvBufAlloc = 0;
    session->recvBufLen = 0;
    session->plainBufAlloc = 0;
    session->plainBufLen = 0;
    session->plainBufOffset = 0;
    session->isConnected = false;
    session->isHandshakeComplete = false;
}
