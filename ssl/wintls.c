#include "hssl.h"

#ifdef WITH_WINTLS

// #define PRINT_DEBUG
// #define PRINT_ERROR
#include "hdef.h"
#include <schannel.h>
#include <wincrypt.h>
#include <windows.h>
#include <wintrust.h>

#define SECURITY_WIN32
#include <security.h>
#include <sspi.h>

#define TLS_SOCKET_BUFFER_SIZE 17000

#ifndef SP_PROT_SSL2_SERVER
#define SP_PROT_SSL2_SERVER             0x00000004
#endif

#ifndef SP_PROT_SSL2_CLIENT
#define SP_PROT_SSL2_CLIENT             0x00000008
#endif

#ifndef SP_PROT_SSL3_SERVER
#define SP_PROT_SSL3_SERVER             0x00000010
#endif

#ifndef SP_PROT_SSL3_CLIENT
#define SP_PROT_SSL3_CLIENT             0x00000020
#endif

#ifndef SP_PROT_TLS1_SERVER
#define SP_PROT_TLS1_SERVER             0x00000040
#endif

#ifndef SP_PROT_TLS1_CLIENT
#define SP_PROT_TLS1_CLIENT             0x00000080
#endif

#ifndef SP_PROT_TLS1_0_SERVER
#define SP_PROT_TLS1_0_SERVER           SP_PROT_TLS1_SERVER
#endif

#ifndef SP_PROT_TLS1_0_CLIENT
#define SP_PROT_TLS1_0_CLIENT           SP_PROT_TLS1_CLIENT
#endif

#ifndef SP_PROT_TLS1_1_SERVER
#define SP_PROT_TLS1_1_SERVER           0x00000100
#endif

#ifndef SP_PROT_TLS1_1_CLIENT
#define SP_PROT_TLS1_1_CLIENT           0x00000200
#endif

#ifndef SP_PROT_TLS1_2_SERVER
#define SP_PROT_TLS1_2_SERVER           0x00000400
#endif

#ifndef SP_PROT_TLS1_2_CLIENT
#define SP_PROT_TLS1_2_CLIENT           0x00000800
#endif

#ifndef SP_PROT_TLS1_3_SERVER
#define SP_PROT_TLS1_3_SERVER           0x00001000
#endif

#ifndef SP_PROT_TLS1_3_CLIENT
#define SP_PROT_TLS1_3_CLIENT           0x00002000
#endif

#ifndef SCH_USE_STRONG_CRYPTO
#define SCH_USE_STRONG_CRYPTO           0x00400000
#endif

#ifndef SECBUFFER_ALERT
#define SECBUFFER_ALERT                 17
#endif

const char* hssl_backend()
{
    return "schannel";
}

static PCCERT_CONTEXT getservercert(const char* path)
{
    /*
    According to the information I searched from the internet, it is not possible to specify an x509 private key and certificate using the
    CertCreateCertificateContext interface. We must first export them as a pkcs#12 formatted file, and then import them into the Windows certificate store. This
    is because the Windows certificate store is an integrated system location that does not support the direct use of separate private key files and certificate
    files. The pkcs#12 format is a complex format that can store and protect keys and certificates. You can use the OpenSSL tool to combine the private key file
    and certificate file into a pkcs#12 formatted file, For example: OpenSSL pkcs12 -export -out cert.pfx -inkey private.key -in cert.cer Then, you can use the
    certutil tool or a graphical interface to import this file into the personal store of your local computer. After importing, you can use the
    CertFindCertificateInStore interface to create and manipulate certificate contexts.
    */
    return NULL;
}

hssl_ctx_t hssl_ctx_new(hssl_ctx_opt_t* opt)
{
    SECURITY_STATUS SecStatus;
    TimeStamp Lifetime;
    CredHandle* hCred = NULL;
    SCHANNEL_CRED credData = { 0 };
    TCHAR unisp_name[] = UNISP_NAME;
    unsigned long credflag;

    if (opt && opt->endpoint == HSSL_SERVER) {
        PCCERT_CONTEXT serverCert = NULL; // server-side certificate
#if 1 // create cert from store

        //-------------------------------------------------------
        // Get the server certificate.
        //-------------------------------------------------------
        // Open the My store(personal store).
        HCERTSTORE hMyCertStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, X509_ASN_ENCODING, 0, CERT_SYSTEM_STORE_LOCAL_MACHINE, L"MY");
        if (hMyCertStore == NULL) {
            printe("Error opening MY store for server.\n");
            return NULL;
        }
        //-------------------------------------------------------
        // Search for a certificate match its subject string to opt->crt_file.
        serverCert = CertFindCertificateInStore(hMyCertStore, X509_ASN_ENCODING, 0, CERT_FIND_SUBJECT_STR_A, opt->crt_file, NULL);
        CertCloseStore(hMyCertStore, 0);
        if (serverCert == NULL) {
            printe("Error retrieving server certificate. %x\n", GetLastError());
            return NULL;
        }
#else
        serverCert = getservercert(opt->ca_file);
#endif
        credData.cCreds = 1; // 数量
        credData.paCred = &serverCert;
        // credData.dwCredFormat = SCH_CRED_FORMAT_CERT_HASH;
        credData.grbitEnabledProtocols = SP_PROT_TLS1_2_SERVER | SP_PROT_TLS1_3_SERVER;
        credflag = SECPKG_CRED_INBOUND;
    } else {
        credData.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT;
        credflag = SECPKG_CRED_OUTBOUND;
    }
#if 0 // just use the system defalut algs
        ALG_ID rgbSupportedAlgs[4];
        rgbSupportedAlgs[0] = CALG_DH_EPHEM;
        rgbSupportedAlgs[1] = CALG_RSA_KEYX;
        rgbSupportedAlgs[2] = CALG_AES_128;
        rgbSupportedAlgs[3] = CALG_SHA_256;
        credData.cSupportedAlgs = 4;
        credData.palgSupportedAlgs = rgbSupportedAlgs;
#endif
    credData.dwVersion = SCHANNEL_CRED_VERSION;
    // credData.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_NO_SERVERNAME_CHECK | SCH_USE_STRONG_CRYPTO | SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_IGNORE_NO_REVOCATION_CHECK | SCH_CRED_IGNORE_REVOCATION_OFFLINE;
    // credData.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_REVOCATION_CHECK_CHAIN | SCH_CRED_IGNORE_REVOCATION_OFFLINE;
    // credData.dwMinimumCipherStrength = -1;
    // credData.dwMaximumCipherStrength = -1;

    //-------------------------------------------------------
    hCred = (CredHandle*)malloc(sizeof(CredHandle));
    if (hCred == NULL) {
        return NULL;
    }

    SecStatus = AcquireCredentialsHandle(NULL, unisp_name, credflag, NULL, &credData, NULL, NULL, hCred, &Lifetime);
    if (SecStatus == SEC_E_OK) {
#ifndef NDEBUG
        SecPkgCred_SupportedAlgs algs;
        if (QueryCredentialsAttributesA(hCred, SECPKG_ATTR_SUPPORTED_ALGS, &algs) == SEC_E_OK) {
            for (int i = 0; i < algs.cSupportedAlgs; i++) {
                printd("alg: 0x%08x\n", algs.palgSupportedAlgs[i]);
            }
        }
#endif
    } else {
        printe("ERROR: AcquireCredentialsHandle: 0x%x\n", SecStatus);
        free(hCred);
        hCred = NULL;
    }
    return hCred;
}

void hssl_ctx_free(hssl_ctx_t ssl_ctx)
{
    SECURITY_STATUS sec_status = FreeCredentialsHandle(ssl_ctx);
    if (sec_status != SEC_E_OK) {
        printe("free_cred_handle FreeCredentialsHandle %d\n", sec_status);
    }
}

static void init_sec_buffer(SecBuffer* secure_buffer, unsigned long type, unsigned long len, void* buffer)
{
    secure_buffer->BufferType = type;
    secure_buffer->cbBuffer = len;
    secure_buffer->pvBuffer = buffer;
}

static void init_sec_buffer_desc(SecBufferDesc* secure_buffer_desc, unsigned long version, unsigned long num_buffers, SecBuffer* buffers)
{
    secure_buffer_desc->ulVersion = version;
    secure_buffer_desc->cBuffers = num_buffers;
    secure_buffer_desc->pBuffers = buffers;
}

/* enum for the nonblocking SSL connection state machine */
typedef enum {
    ssl_connect_1,
    ssl_connect_2,
    ssl_connect_2_reading,
    ssl_connect_2_writing,
    ssl_connect_3,
    ssl_connect_done
} ssl_connect_state;

struct wintls_s {
    hssl_ctx_t ssl_ctx; // CredHandle
    int fd;
    union {
        ssl_connect_state state2;
        ssl_connect_state connecting_state;
    };
    SecHandle sechandle;
    SecPkgContext_StreamSizes stream_sizes_;
    size_t buffer_to_decrypt_offset_;
    size_t dec_len_;
    char encrypted_buffer_[TLS_SOCKET_BUFFER_SIZE];
    char buffer_to_decrypt_[TLS_SOCKET_BUFFER_SIZE];
    char decrypted_buffer_[TLS_SOCKET_BUFFER_SIZE + TLS_SOCKET_BUFFER_SIZE];
    char* sni;
};

hssl_t hssl_new(hssl_ctx_t ssl_ctx, int fd)
{
    struct wintls_s* ret = malloc(sizeof(*ret));
    if (ret) {
        memset(ret, 0, sizeof(*ret));
        ret->ssl_ctx = ssl_ctx;
        ret->fd = fd;
        ret->sechandle.dwLower = 0;
        ret->sechandle.dwUpper = 0;
    }
    return ret;
}

void hssl_free(hssl_t _ssl)
{
    struct wintls_s* ssl = _ssl;
    SECURITY_STATUS sec_status = DeleteSecurityContext(&ssl->sechandle);
    if (sec_status != SEC_E_OK) {
        printe("hssl_free DeleteSecurityContext %d", sec_status);
    }
    if (ssl->sni) {
        free(ssl->sni);
    }
    free(ssl);
}

static void free_all_buffers(SecBufferDesc* secure_buffer_desc)
{
    for (unsigned long i = 0; i < secure_buffer_desc->cBuffers; ++i) {
        void* buffer = secure_buffer_desc->pBuffers[i].pvBuffer;
        if (buffer != NULL) {
            FreeContextBuffer(buffer);
        }
    }
}

static int __sendwrapper(SOCKET fd, const char* buf, size_t len, int flags)
{
    int left = len;
    int offset = 0;
    while (left > 0) {
        int bytes_sent = send(fd, buf + offset, left, flags);
        if (bytes_sent == 0 || (bytes_sent == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAEINTR)) {
            break;
        }
        if (bytes_sent > 0) {
            offset += bytes_sent;
            left -= bytes_sent;
        }
    }
    return offset;
}

static int __recvwrapper(SOCKET fd, char* buf, int len, int flags)
{
    int ret = 0;
    do {
        ret = recv(fd, buf, len, flags);
    } while (ret == SOCKET_ERROR && WSAGetLastError() == WSAEINTR);
    return ret;
}

int hssl_accept(hssl_t ssl)
{
    int ret = HSSL_ERROR;
    struct wintls_s* winssl = ssl;
    bool authn_completed = false;

    // Input buffer
    char buffer_in[TLS_SOCKET_BUFFER_SIZE];

    SecBuffer secure_buffer_in[2] = { 0 };
    init_sec_buffer(&secure_buffer_in[0], SECBUFFER_TOKEN, TLS_SOCKET_BUFFER_SIZE, buffer_in);
    init_sec_buffer(&secure_buffer_in[1], SECBUFFER_EMPTY, 0, NULL);

    SecBufferDesc secure_buffer_desc_in = { 0 };
    init_sec_buffer_desc(&secure_buffer_desc_in, SECBUFFER_VERSION, 2, secure_buffer_in);

    // Output buffer
    SecBuffer secure_buffer_out[3] = { 0 };
    init_sec_buffer(&secure_buffer_out[0], SECBUFFER_TOKEN, 0, NULL);
    init_sec_buffer(&secure_buffer_out[1], SECBUFFER_ALERT, 0, NULL);
    init_sec_buffer(&secure_buffer_out[2], SECBUFFER_EMPTY, 0, NULL);

    SecBufferDesc secure_buffer_desc_out = { 0 };
    init_sec_buffer_desc(&secure_buffer_desc_out, SECBUFFER_VERSION, 3, secure_buffer_out);

    unsigned long context_requirements = ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_CONFIDENTIALITY;

    // We use ASC_REQ_ALLOCATE_MEMORY which means the buffers will be allocated for us, we need to make sure we free them.

    ULONG context_attributes = 0;
    TimeStamp life_time = { 0 };

    secure_buffer_in[0].cbBuffer = __recvwrapper(winssl->fd, (char*)secure_buffer_in[0].pvBuffer, TLS_SOCKET_BUFFER_SIZE, 0);
    // printd("%s recv %d %d\n", __func__, secure_buffer_in[0].cbBuffer, WSAGetLastError());
    if (secure_buffer_in[0].cbBuffer == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
        ret = HSSL_WANT_READ;
    } else if (secure_buffer_in[0].cbBuffer > 0) {
        SECURITY_STATUS sec_status = AcceptSecurityContext(winssl->ssl_ctx, winssl->state2 == 0 ? NULL : &winssl->sechandle, &secure_buffer_desc_in,
            context_requirements, 0, &winssl->sechandle, &secure_buffer_desc_out, &context_attributes, &life_time);

        winssl->state2 = 1;
        // printd("establish_server_security_context AcceptSecurityContext %x\n", sec_status);

        if (secure_buffer_out[0].cbBuffer > 0) {
            int rc = __sendwrapper(winssl->fd, (const char*)secure_buffer_out[0].pvBuffer, secure_buffer_out[0].cbBuffer, 0);
            if (rc != secure_buffer_out[0].cbBuffer) {
                goto END;
            }
        }

        switch (sec_status) {
        case SEC_E_OK:
            ret = HSSL_OK;
            authn_completed = true;
            break;
        case SEC_I_CONTINUE_NEEDED:
            ret = HSSL_WANT_READ;
            break;
        case SEC_I_COMPLETE_AND_CONTINUE:
        case SEC_I_COMPLETE_NEEDED: {
            SECURITY_STATUS complete_sec_status = SEC_E_OK;
            complete_sec_status = CompleteAuthToken(&winssl->sechandle, &secure_buffer_desc_out);
            if (complete_sec_status != SEC_E_OK) {
                printe("establish_server_security_context CompleteAuthToken %x\n", complete_sec_status);
                goto END;
            }

            if (sec_status == SEC_I_COMPLETE_NEEDED) {
                authn_completed = true;
                ret = HSSL_OK;
            } else {
                ret = HSSL_WANT_READ;
            }
            break;
        }
        default:
            break;
        }
    }
END:
    free_all_buffers(&secure_buffer_desc_out);

    if (authn_completed) {
        SECURITY_STATUS sec_status = QueryContextAttributes(&winssl->sechandle, SECPKG_ATTR_STREAM_SIZES, &winssl->stream_sizes_);
        if (sec_status != SEC_E_OK) {
            printe("get_stream_sizes QueryContextAttributes %d\n", sec_status);
        }
    }
    return ret;
}

static int schannel_connect_step1(struct wintls_s* ssl)
{
    int ret = 0;
    ULONG context_attributes = 0;
    unsigned long context_requirements = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;

    TimeStamp life_time = { 0 };

    SecBuffer secure_buffer_out[1] = { 0 };
    init_sec_buffer(&secure_buffer_out[0], SECBUFFER_EMPTY, 0, NULL);

    SecBufferDesc secure_buffer_desc_out = { 0 };
    init_sec_buffer_desc(&secure_buffer_desc_out, SECBUFFER_VERSION, 1, secure_buffer_out);

    SECURITY_STATUS sec_status = InitializeSecurityContext(ssl->ssl_ctx, NULL, ssl->sni, context_requirements, 0, 0, NULL, 0, &ssl->sechandle,
        &secure_buffer_desc_out, &context_attributes, &life_time);

    if (sec_status != SEC_I_CONTINUE_NEEDED) {
        printe("1InitializeSecurityContext: %x\n", sec_status);
    }

    if (secure_buffer_out[0].cbBuffer > 0) {
        int rc = __sendwrapper(ssl->fd, (const char*)secure_buffer_out[0].pvBuffer, secure_buffer_out[0].cbBuffer, 0);
        if (rc != secure_buffer_out[0].cbBuffer) {
            // TODO: Handle the error
            printe("%s :send failed\n", __func__);
            ret = -1;
        } else {
            printd("%s :send len=%d\n", __func__, rc);
            ssl->connecting_state = ssl_connect_2;
        }
    }
    free_all_buffers(&secure_buffer_desc_out);
    return ret;
}

static int schannel_connect_step2(struct wintls_s* ssl)
{
    int ret = HSSL_ERROR;
    ULONG context_attributes = 0;
    bool verify_server_cert = 0;

    unsigned long context_requirements = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
    if (!verify_server_cert) {
        context_requirements |= ISC_REQ_MANUAL_CRED_VALIDATION;
    }

    TimeStamp life_time = { 0 };

    // Allocate a temporary buffer for input
    char* buffer_in = malloc(TLS_SOCKET_BUFFER_SIZE);
    if (buffer_in == NULL) {
        printe("schannel_connect_step2: Memory allocation failed\n");
        return HSSL_ERROR;
    }

    int offset = 0;
    bool skip_recv = false;
    bool authn_complete = false;
    while (!authn_complete) {
        int in_buffer_size = 0;

        if (!skip_recv) {
            int received = __recvwrapper(ssl->fd, buffer_in + offset, TLS_SOCKET_BUFFER_SIZE, 0);
            if (received == SOCKET_ERROR) {
                if (WSAGetLastError() == WSAEWOULDBLOCK) {
                    ret = HSSL_WANT_READ;
                } else {
                    printe("schannel_connect_step2: Receive failed\n");
                }
                break;
            } else if (received == 0) {
                printe("schannel_connect_step2: peer closed\n");
                break;
            }
            in_buffer_size = received + offset;
        } else {
            in_buffer_size = offset;
        }

        skip_recv = false;
        offset = 0;

        // Input buffer
        SecBuffer secure_buffer_in[4] = { 0 };
        init_sec_buffer(&secure_buffer_in[0], SECBUFFER_TOKEN, in_buffer_size, buffer_in);
        init_sec_buffer(&secure_buffer_in[1], SECBUFFER_EMPTY, 0, NULL);

        SecBufferDesc secure_buffer_desc_in = { 0 };
        init_sec_buffer_desc(&secure_buffer_desc_in, SECBUFFER_VERSION, 2, secure_buffer_in);

        // Output buffer
        SecBuffer secure_buffer_out[3] = { 0 };
        init_sec_buffer(&secure_buffer_out[0], SECBUFFER_TOKEN, 0, NULL);
        init_sec_buffer(&secure_buffer_out[1], SECBUFFER_ALERT, 0, NULL);
        init_sec_buffer(&secure_buffer_out[2], SECBUFFER_EMPTY, 0, NULL);

        SecBufferDesc secure_buffer_desc_out = { 0 };
        init_sec_buffer_desc(&secure_buffer_desc_out, SECBUFFER_VERSION, 3, secure_buffer_out);
        printd("h2:%d\n", in_buffer_size);
        SECURITY_STATUS sec_status = InitializeSecurityContext(ssl->ssl_ctx, &ssl->sechandle, ssl->sni, context_requirements, 0, 0, &secure_buffer_desc_in, 0,
            &ssl->sechandle, &secure_buffer_desc_out, &context_attributes, &life_time);

        printd("h2 0x%x inbuf[1] type=%d %d inbuf[0]=%d\n", sec_status, secure_buffer_in[1].BufferType, secure_buffer_in[1].cbBuffer, secure_buffer_in[0].cbBuffer);
        if (sec_status == SEC_E_OK || sec_status == SEC_I_CONTINUE_NEEDED) {
            // for (size_t i = 0; i < 3; i++) {
            //     printd("obuf[%zu] type=%d %d\n", i, secure_buffer_out[i].BufferType, secure_buffer_out[i].cbBuffer);
            // }
            if (secure_buffer_out[0].cbBuffer > 0) {
                int rc = __sendwrapper(ssl->fd, (const char*)secure_buffer_out[0].pvBuffer, secure_buffer_out[0].cbBuffer, 0);
                if (rc != secure_buffer_out[0].cbBuffer) {
                    printe("schannel_connect_step2: Send failed\n");
                    // TODO: Handle the error
                    break;
                }
                // printd("%s :send ok\n", __func__);
            }

            if (sec_status == SEC_I_CONTINUE_NEEDED) {
                if (secure_buffer_in[1].BufferType == SECBUFFER_EXTRA && secure_buffer_in[1].cbBuffer > 0) {
                    offset = secure_buffer_in[0].cbBuffer - secure_buffer_in[1].cbBuffer;
                    memmove(buffer_in, buffer_in + offset, secure_buffer_in[1].cbBuffer);
                    offset = secure_buffer_in[1].cbBuffer;
                    skip_recv = true;
                }
            } else if (sec_status == SEC_E_OK) {
                authn_complete = true;
                ret = HSSL_OK;
                ssl->connecting_state = ssl_connect_3;
            }
        } else if (sec_status == SEC_E_INCOMPLETE_MESSAGE) {
            offset = secure_buffer_in[0].cbBuffer;
        } else {
            printe("2InitializeSecurityContext: 0x%x\n", sec_status);
            break;
        }

        free_all_buffers(&secure_buffer_desc_out);
    }
    // END:
    free(buffer_in); // Free the temporary buffer
    return ret;
}

static void dumpconninfo(SecHandle* sechandle)
{
    SECURITY_STATUS Status;
    SecPkgContext_ConnectionInfo ConnectionInfo;

    Status = QueryContextAttributes(sechandle,
        SECPKG_ATTR_CONNECTION_INFO,
        (PVOID)&ConnectionInfo);
    if (Status != SEC_E_OK) {
        printe("Error 0x%x querying connection info\n", Status);
        return;
    }

    printd("\n");

    switch (ConnectionInfo.dwProtocol) {
    case SP_PROT_TLS1_CLIENT:
        printd("Protocol: TLS1\n");
        break;

    case SP_PROT_SSL3_CLIENT:
        printd("Protocol: SSL3\n");
        break;

    case SP_PROT_SSL2_CLIENT:
        printd("Protocol: SSL2\n");
        break;

    case SP_PROT_PCT1_CLIENT:
        printd("Protocol: PCT\n");
        break;

    default:
        printd("Protocol: 0x%x\n", ConnectionInfo.dwProtocol);
    }

    switch (ConnectionInfo.aiCipher) {
    case CALG_RC4:
        printd("Cipher: RC4\n");
        break;

    case CALG_3DES:
        printd("Cipher: Triple DES\n");
        break;

    case CALG_RC2:
        printd("Cipher: RC2\n");
        break;

    case CALG_DES:
    case CALG_CYLINK_MEK:
        printd("Cipher: DES\n");
        break;

    case CALG_SKIPJACK:
        printd("Cipher: Skipjack\n");
        break;

    case CALG_AES_128:
        printd("Cipher: aes128\n");
        break;
    default:
        printd("Cipher: 0x%x\n", ConnectionInfo.aiCipher);
    }

    printd("Cipher strength: %d\n", ConnectionInfo.dwCipherStrength);

    switch (ConnectionInfo.aiHash) {
    case CALG_MD5:
        printd("Hash: MD5\n");
        break;

    case CALG_SHA:
        printd("Hash: SHA\n");
        break;

    default:
        printd("Hash: 0x%x\n", ConnectionInfo.aiHash);
    }

    printd("Hash strength: %d\n", ConnectionInfo.dwHashStrength);

    switch (ConnectionInfo.aiExch) {
    case CALG_RSA_KEYX:
    case CALG_RSA_SIGN:
        printd("Key exchange: RSA\n");
        break;

    case CALG_KEA_KEYX:
        printd("Key exchange: KEA\n");
        break;

    case CALG_DH_EPHEM:
        printd("Key exchange: DH Ephemeral\n");
        break;

    default:
        printd("Key exchange: 0x%x\n", ConnectionInfo.aiExch);
    }

    printd("Key exchange strength: %d\n", ConnectionInfo.dwExchStrength);
}

int hssl_connect(hssl_t _ssl)
{
    int ret = 0;
    struct wintls_s* ssl = _ssl;
    if (ssl->connecting_state == ssl_connect_1) {
        ret = schannel_connect_step1(ssl);
    }
    if (!ret && ssl->connecting_state == ssl_connect_2) {
        ret = schannel_connect_step2(ssl);
    }
    // printd("%s %x\n", __func__, ret);
    if (!ret) {
        if (ssl->connecting_state == ssl_connect_3) {
            // ret = schannel_connect_step3(ssl);
        }
        SECURITY_STATUS sec_status = QueryContextAttributes(&ssl->sechandle, SECPKG_ATTR_STREAM_SIZES, &ssl->stream_sizes_);
        if (sec_status != SEC_E_OK) {
            printe("get_stream_sizes QueryContextAttributes %d\n", sec_status);
        } else {
            printd("stream_sizes bs:%d h:%d t:%d max:%d bfs:%d\n", ssl->stream_sizes_.cbBlockSize, ssl->stream_sizes_.cbHeader, ssl->stream_sizes_.cbTrailer, ssl->stream_sizes_.cbMaximumMessage, ssl->stream_sizes_.cBuffers);
        }
        dumpconninfo(&ssl->sechandle);
    }
    return ret;
}

static int decrypt_message(SecHandle security_context, unsigned long* extra, char* in_buf, int in_len, char* out_buf, int out_len)
{
    printd("%s: inlen=%d\n", __func__, in_len);
    // Initialize the secure buffers
    SecBuffer secure_buffers[4] = { 0 };
    init_sec_buffer(&secure_buffers[0], SECBUFFER_DATA, in_len, in_buf);
    init_sec_buffer(&secure_buffers[1], SECBUFFER_EMPTY, 0, NULL);
    init_sec_buffer(&secure_buffers[2], SECBUFFER_EMPTY, 0, NULL);
    init_sec_buffer(&secure_buffers[3], SECBUFFER_EMPTY, 0, NULL);

    // Initialize the secure buffer descriptor
    SecBufferDesc secure_buffer_desc = { 0 };
    init_sec_buffer_desc(&secure_buffer_desc, SECBUFFER_VERSION, 4, secure_buffers);

    // Decrypt the message using the security context
    SECURITY_STATUS sec_status = DecryptMessage(&security_context, &secure_buffer_desc, 0, NULL);

    for (size_t i = 1; i < 4; i++) {
        printd("%d: %u %u\n", i, secure_buffers[i].BufferType, secure_buffers[i].cbBuffer);
    }
    if (sec_status == SEC_E_INCOMPLETE_MESSAGE) {
        printe("decrypt_message SEC_E_INCOMPLETE_MESSAGE\n");
        return -1;
    } else if (sec_status == SEC_E_DECRYPT_FAILURE) {
        printe("decrypt_message ignore SEC_E_DECRYPT_FAILURE\n");
        return 0;
    } else if (sec_status == SEC_E_UNSUPPORTED_FUNCTION) {
        printe("decrypt_message ignore SEC_E_UNSUPPORTED_FUNCTION\n");
        return 0;
    }

    if (sec_status != SEC_E_OK) {
        printe("decrypt_message DecryptMessage: 0x%x\n", sec_status);
        return -1;
    }
    if (secure_buffers[3].BufferType == SECBUFFER_EXTRA && secure_buffers[3].cbBuffer > 0) {
        *extra = secure_buffers[3].cbBuffer;
    }
    memcpy(out_buf, secure_buffers[1].pvBuffer, secure_buffers[1].cbBuffer);
    // printd("ob:%s\n", out_buf);
    return secure_buffers[1].cbBuffer;
}

int hssl_read(hssl_t _ssl, void* buf, int len)
{
    struct wintls_s* ssl = _ssl;
    printd("%s: dec_len_= %zu\n", __func__, ssl->dec_len_);
    if (ssl->dec_len_ > 0) {
        if (buf == NULL) {
            return 0;
        }
        int decrypted = MIN(ssl->dec_len_, len);
        memcpy(buf, ssl->decrypted_buffer_, (size_t)decrypted);
        ssl->dec_len_ -= decrypted;
        if (ssl->dec_len_) {
            memmove(ssl->decrypted_buffer_, ssl->decrypted_buffer_ + decrypted, (size_t)ssl->dec_len_);
        } else {
            // hssl_read(_ssl, NULL, 0);
        }
        return decrypted;
    }

    // We might have leftovers, an incomplete message from a previous call.
    // Calculate the available buffer length for tcp recv.
    int recv_max_len = TLS_SOCKET_BUFFER_SIZE - ssl->buffer_to_decrypt_offset_;
    int bytes_received = __recvwrapper(ssl->fd, ssl->buffer_to_decrypt_ + ssl->buffer_to_decrypt_offset_, recv_max_len, 0);
    // printd("%s recv %d %d\n", __func__, bytes_received, WSAGetLastError());
    if (bytes_received == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            bytes_received = 0;
            return 0;
        } else {
            return -1;
        }
    } else if (bytes_received == 0) {
        return 0;
    }

    int encrypted_buffer_len = ssl->buffer_to_decrypt_offset_ + bytes_received;
    ssl->buffer_to_decrypt_offset_ = 0;
    while (true) {
        // printd("%s:buffer_to_decrypt_offset_ = %d , encrypted_buffer_len= %d\n", __func__, ssl->buffer_to_decrypt_offset_, encrypted_buffer_len);
        if (ssl->buffer_to_decrypt_offset_ >= encrypted_buffer_len) {
            // Reached the encrypted buffer length, we decrypted everything so we can stop.
            break;
        }
        unsigned long extra = 0;
        int decrypted_len = decrypt_message(ssl->sechandle, &extra, ssl->buffer_to_decrypt_ + ssl->buffer_to_decrypt_offset_,
            encrypted_buffer_len - ssl->buffer_to_decrypt_offset_, ssl->decrypted_buffer_ + ssl->dec_len_,
            TLS_SOCKET_BUFFER_SIZE + TLS_SOCKET_BUFFER_SIZE - ssl->dec_len_);

        if (decrypted_len == -1) {
            // Incomplete message, we shuold keep it so it will be decrypted on the next call to recv().
            // Shift the remaining buffer to the beginning and break the loop.

            memmove(ssl->buffer_to_decrypt_, ssl->buffer_to_decrypt_ + ssl->buffer_to_decrypt_offset_, encrypted_buffer_len - ssl->buffer_to_decrypt_offset_);

            break;
        }

        ssl->dec_len_ += decrypted_len;
        ssl->buffer_to_decrypt_offset_ = encrypted_buffer_len - extra;
    }
    ssl->buffer_to_decrypt_offset_ = encrypted_buffer_len - ssl->buffer_to_decrypt_offset_;
    return hssl_read(_ssl, buf, len);
}

int hssl_write(hssl_t _ssl, const void* buf, int len)
{
    struct wintls_s* ssl = _ssl;
    SecPkgContext_StreamSizes* stream_sizes = &ssl->stream_sizes_;
    if (len > (int)stream_sizes->cbMaximumMessage) {
        len = stream_sizes->cbMaximumMessage;
    }

    // Calculate the minimum output buffer length
    int min_out_len = stream_sizes->cbHeader + len + stream_sizes->cbTrailer;
    if (min_out_len > TLS_SOCKET_BUFFER_SIZE) {
        printe("encrypt_message: Output buffer is too small");
        return -1;
    }

    // Initialize the secure buffers
    SecBuffer secure_buffers[4] = { 0 };
    init_sec_buffer(&secure_buffers[0], SECBUFFER_STREAM_HEADER, stream_sizes->cbHeader, ssl->encrypted_buffer_);
    init_sec_buffer(&secure_buffers[1], SECBUFFER_DATA, len, ssl->encrypted_buffer_ + stream_sizes->cbHeader);
    init_sec_buffer(&secure_buffers[2], SECBUFFER_STREAM_TRAILER, stream_sizes->cbTrailer, ssl->encrypted_buffer_ + stream_sizes->cbHeader + len);
    init_sec_buffer(&secure_buffers[3], SECBUFFER_EMPTY, 0, NULL);

    // Initialize the secure buffer descriptor
    SecBufferDesc secure_buffer_desc = { 0 };
    init_sec_buffer_desc(&secure_buffer_desc, SECBUFFER_VERSION, 4, secure_buffers);

    // Copy the input buffer to the data buffer
    memcpy(secure_buffers[1].pvBuffer, buf, len);

    // Encrypt the message using the security context
    SECURITY_STATUS sec_status = EncryptMessage(&ssl->sechandle, 0, &secure_buffer_desc, 0);

    // Check the encryption status and the data buffer length
    if (sec_status != SEC_E_OK) {
        printe("encrypt_message EncryptMessage %d\n", sec_status);
        return -1;
    }
    if (secure_buffers[1].cbBuffer > (unsigned int)len) {
        printe("encrypt_message: Data buffer is too large\n");
        return -1;
    }

    // Adjust the minimum output buffer length
    min_out_len = secure_buffers[0].cbBuffer + secure_buffers[1].cbBuffer + secure_buffers[2].cbBuffer;
    printd("enc02: %d %d\n", secure_buffers[0].cbBuffer, secure_buffers[2].cbBuffer);

    // Send the encrypted message to the socket
    int offset = __sendwrapper(ssl->fd, ssl->encrypted_buffer_, min_out_len, 0);
    // Check the send result
    if (offset != min_out_len) {
        printe("hssl_write: Send failed\n");
        return -1;
    } else {
        printd("hssl_write: Send %d\n", min_out_len);
    }

    // Return the number of bytes sent excluding the header and trailer
    return offset - secure_buffers[0].cbBuffer - secure_buffers[2].cbBuffer;
}

int hssl_close(hssl_t _ssl)
{
    return 0;
}

int hssl_set_sni_hostname(hssl_t _ssl, const char* hostname)
{
    struct wintls_s* ssl = _ssl;
    ssl->sni = strdup(hostname);
    return 0;
}

#endif // WITH_WINTLS
