#include "KindroidBot.h"
#define SECURITY_WIN32
#include <schannel.h>
#include <security.h>
#include <wincrypt.h>
#include <winsock2.h>

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")

// Schannel SSL/TLS wrapper
struct SchannelContext {
    SOCKET sock;
    CredHandle credentials;
    CtxtHandle context;
    SecPkgContext_StreamSizes sizes;
    std::vector<BYTE> recvBuffer;
    std::vector<BYTE> extraBuffer;
    size_t extraData;
    bool connected;
    
    // Per-connection receive buffers (NOT static - thread safe)
    std::vector<BYTE> ioBuffer;
    DWORD ioLen;
    std::vector<BYTE> decryptedBuffer;
    DWORD decryptedLen;
    DWORD decryptedOffset;
};

static bool InitializeSchannel(SchannelContext* ctx) {
    SCHANNEL_CRED cred = {0};
    cred.dwVersion = SCHANNEL_CRED_VERSION;
    cred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | 
                   SCH_CRED_MANUAL_CRED_VALIDATION | 
                   SCH_USE_STRONG_CRYPTO;
    cred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
    
    SECURITY_STATUS status = AcquireCredentialsHandleA(
        NULL, (LPSTR)UNISP_NAME_A, SECPKG_CRED_OUTBOUND,
        NULL, &cred, NULL, NULL, &ctx->credentials, NULL);
    
    return status == SEC_E_OK;
}

static bool PerformHandshake(SchannelContext* ctx, const char* hostname) {
    if (!InitializeSchannel(ctx)) return false;
    
    DWORD flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                  ISC_REQ_CONFIDENTIALITY | ISC_RET_EXTENDED_ERROR |
                  ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
    
    SecBuffer outBuffer = {0};
    outBuffer.BufferType = SECBUFFER_TOKEN;
    
    SecBufferDesc outDesc = {0};
    outDesc.ulVersion = SECBUFFER_VERSION;
    outDesc.cBuffers = 1;
    outDesc.pBuffers = &outBuffer;
    
    DWORD outFlags;
    SECURITY_STATUS status = InitializeSecurityContextA(
        &ctx->credentials, NULL, (SEC_CHAR*)hostname, flags,
        0, 0, NULL, 0, &ctx->context, &outDesc, &outFlags, NULL);
    
    if (status != SEC_I_CONTINUE_NEEDED) return false;
    
    if (outBuffer.cbBuffer > 0) {
        ::send(ctx->sock, (char*)outBuffer.pvBuffer, outBuffer.cbBuffer, 0);
        FreeContextBuffer(outBuffer.pvBuffer);
    }
    
    // Handshake loop
    std::vector<BYTE> ioBuffer(0x10000);
    DWORD ioLength = 0;
    
    while (true) {
        if (ioLength == 0 || status == SEC_E_INCOMPLETE_MESSAGE) {
            int rcv = ::recv(ctx->sock, (char*)ioBuffer.data() + ioLength,
                           (int)(ioBuffer.size() - ioLength), 0);
            if (rcv <= 0) return false;
            ioLength += rcv;
        }
        
        SecBuffer inBuffers[2] = {0};
        inBuffers[0].BufferType = SECBUFFER_TOKEN;
        inBuffers[0].pvBuffer = ioBuffer.data();
        inBuffers[0].cbBuffer = ioLength;
        inBuffers[1].BufferType = SECBUFFER_EMPTY;
        
        SecBufferDesc inDesc = {0};
        inDesc.ulVersion = SECBUFFER_VERSION;
        inDesc.cBuffers = 2;
        inDesc.pBuffers = inBuffers;
        
        outBuffer.pvBuffer = NULL;
        outBuffer.cbBuffer = 0;
        outBuffer.BufferType = SECBUFFER_TOKEN;
        
        status = InitializeSecurityContextA(
            &ctx->credentials, &ctx->context, (SEC_CHAR*)hostname, flags,
            0, 0, &inDesc, 0, NULL, &outDesc, &outFlags, NULL);
        
        if (outBuffer.cbBuffer > 0) {
            ::send(ctx->sock, (char*)outBuffer.pvBuffer, outBuffer.cbBuffer, 0);
            FreeContextBuffer(outBuffer.pvBuffer);
        }
        
        if (status == SEC_E_OK) {
            if (inBuffers[1].BufferType == SECBUFFER_EXTRA) {
                ctx->extraData = inBuffers[1].cbBuffer;
                ctx->extraBuffer.resize(ctx->extraData);
                memcpy(ctx->extraBuffer.data(),
                      ioBuffer.data() + (ioLength - ctx->extraData),
                      ctx->extraData);
            }
            QueryContextAttributes(&ctx->context, SECPKG_ATTR_STREAM_SIZES, &ctx->sizes);
            ctx->connected = true;
            return true;
        }
        else if (status == SEC_I_CONTINUE_NEEDED) {
            if (inBuffers[1].BufferType == SECBUFFER_EXTRA) {
                memmove(ioBuffer.data(),
                       ioBuffer.data() + (ioLength - inBuffers[1].cbBuffer),
                       inBuffers[1].cbBuffer);
                ioLength = inBuffers[1].cbBuffer;
            } else {
                ioLength = 0;
            }
        }
        else if (status != SEC_E_INCOMPLETE_MESSAGE) {
            return false;
        }
    }
}

static int SecureSend(SchannelContext* ctx, const void* data, int len) {
    if (!ctx->connected) return -1;
    
    std::vector<BYTE> msg(ctx->sizes.cbHeader + len + ctx->sizes.cbTrailer);
    memcpy(msg.data() + ctx->sizes.cbHeader, data, len);
    
    SecBuffer bufs[4] = {0};
    bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
    bufs[0].pvBuffer = msg.data();
    bufs[0].cbBuffer = ctx->sizes.cbHeader;
    bufs[1].BufferType = SECBUFFER_DATA;
    bufs[1].pvBuffer = msg.data() + ctx->sizes.cbHeader;
    bufs[1].cbBuffer = len;
    bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
    bufs[2].pvBuffer = msg.data() + ctx->sizes.cbHeader + len;
    bufs[2].cbBuffer = ctx->sizes.cbTrailer;
    bufs[3].BufferType = SECBUFFER_EMPTY;
    
    SecBufferDesc desc = {0};
    desc.ulVersion = SECBUFFER_VERSION;
    desc.cBuffers = 4;
    desc.pBuffers = bufs;
    
    if (EncryptMessage(&ctx->context, 0, &desc, 0) != SEC_E_OK) return -1;
    
    int total = bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer;
    return (::send(ctx->sock, (char*)msg.data(), total, 0) == total) ? len : -1;
}

static int SecureRecv(SchannelContext* ctx, void* buffer, int len) {
    if (!ctx->connected) return -1;
    
    // Use per-context buffers (thread safe)
    std::vector<BYTE>& ioBuffer = ctx->ioBuffer;
    DWORD& ioLen = ctx->ioLen;
    std::vector<BYTE>& decryptedBuffer = ctx->decryptedBuffer;
    DWORD& decryptedLen = ctx->decryptedLen;
    DWORD& decryptedOffset = ctx->decryptedOffset;
    
    // Return buffered decrypted data first
    if (decryptedLen > 0) {
        int copy = (len < (int)decryptedLen) ? len : (int)decryptedLen;
        memcpy(buffer, decryptedBuffer.data() + decryptedOffset, copy);
        decryptedOffset += copy;
        decryptedLen -= copy;
        if (decryptedLen == 0) {
            decryptedOffset = 0;
        }
        return copy;
    }
    
    // Use extra data first
    if (ctx->extraData > 0) {
        memcpy(ioBuffer.data(), ctx->extraBuffer.data(), ctx->extraData);
        ioLen = (DWORD)ctx->extraData;
        ctx->extraData = 0;
    }
    
    while (true) {
        if (ioLen == 0) {
            int rcv = ::recv(ctx->sock, (char*)ioBuffer.data(), (int)ioBuffer.size(), 0);
            if (rcv <= 0) return -1;
            ioLen = rcv;
        }
        
        SecBuffer bufs[4] = {0};
        bufs[0].BufferType = SECBUFFER_DATA;
        bufs[0].pvBuffer = ioBuffer.data();
        bufs[0].cbBuffer = ioLen;
        bufs[1].BufferType = SECBUFFER_EMPTY;
        bufs[2].BufferType = SECBUFFER_EMPTY;
        bufs[3].BufferType = SECBUFFER_EMPTY;
        
        SecBufferDesc desc = {0};
        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers = 4;
        desc.pBuffers = bufs;
        
        SECURITY_STATUS status = DecryptMessage(&ctx->context, &desc, 0, NULL);
        
        if (status == SEC_E_OK) {
            for (int i = 1; i < 4; i++) {
                if (bufs[i].BufferType == SECBUFFER_DATA) {
                    // Found decrypted data
                    int dataSize = bufs[i].cbBuffer;
                    int copy = (len < dataSize) ? len : dataSize;
                    memcpy(buffer, bufs[i].pvBuffer, copy);
                    
                    // Save any leftover decrypted data
                    if (dataSize > copy) {
                        decryptedLen = dataSize - copy;
                        memcpy(decryptedBuffer.data(), (BYTE*)bufs[i].pvBuffer + copy, decryptedLen);
                        decryptedOffset = 0;
                    }
                    
                    // Save extra encrypted data
                    for (int j = 1; j < 4; j++) {
                        if (bufs[j].BufferType == SECBUFFER_EXTRA) {
                            memmove(ioBuffer.data(), bufs[j].pvBuffer, bufs[j].cbBuffer);
                            ioLen = bufs[j].cbBuffer;
                            return copy;
                        }
                    }
                    ioLen = 0;
                    return copy;
                }
            }
        }
        else if (status == SEC_E_INCOMPLETE_MESSAGE) {
            int rcv = ::recv(ctx->sock, (char*)ioBuffer.data() + ioLen,
                           (int)(ioBuffer.size() - ioLen), 0);
            if (rcv <= 0) return -1;
            ioLen += rcv;
        }
        else {
            return -1;
        }
    }
}

// Public C-style interface
SchannelContext* SchannelCreate(SOCKET sock) {
    SchannelContext* ctx = new SchannelContext();
    ctx->sock = sock;
    ctx->extraData = 0;
    ctx->connected = false;
    ctx->recvBuffer.resize(0x10000);
    ctx->extraBuffer.resize(0x10000);
    
    // Initialize per-context receive buffers
    ctx->ioBuffer.resize(0x11000);
    ctx->ioLen = 0;
    ctx->decryptedBuffer.resize(0x11000);
    ctx->decryptedLen = 0;
    ctx->decryptedOffset = 0;
    
    SecInvalidateHandle(&ctx->credentials);
    SecInvalidateHandle(&ctx->context);
    return ctx;
}

bool SchannelHandshake(SchannelContext* ctx, const char* hostname) {
    return PerformHandshake(ctx, hostname);
}

int SchannelSend(SchannelContext* ctx, const void* data, int len) {
    return SecureSend(ctx, data, len);
}

int SchannelRecv(SchannelContext* ctx, void* buffer, int len) {
    return SecureRecv(ctx, buffer, len);
}

void SchannelDestroy(SchannelContext* ctx) {
    if (ctx) {
        if (ctx->connected) {
            DWORD type = SCHANNEL_SHUTDOWN;
            SecBuffer buf = {0};
            buf.BufferType = SECBUFFER_TOKEN;
            buf.pvBuffer = &type;
            buf.cbBuffer = sizeof(type);
            SecBufferDesc desc = {0};
            desc.ulVersion = SECBUFFER_VERSION;
            desc.cBuffers = 1;
            desc.pBuffers = &buf;
            ApplyControlToken(&ctx->context, &desc);
        }
        if (SecIsValidHandle(&ctx->context)) DeleteSecurityContext(&ctx->context);
        if (SecIsValidHandle(&ctx->credentials)) FreeCredentialsHandle(&ctx->credentials);
        delete ctx;
    }
}
