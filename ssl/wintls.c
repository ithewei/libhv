#include "hssl.h"

#ifdef WITH_WINTLS

#include <schannel.h>
#include <wincrypt.h>
#include <windows.h>
#include <wintrust.h>

#define SECURITY_WIN32
#include <security.h>
#include <sspi.h>

#pragma comment(lib, "Secur32.lib")
#pragma comment(lib, "crypt32.lib")

#define TLS_SOCKET_BUFFER_SIZE 16384

const char* hssl_backend()
{
    return "schannel";
}

static PCCERT_CONTEXT getservercert(const char* path)
{
    /*
    根据我从网络上搜索到的信息，无法使用CertCreateCertificateContext等接口指定x509私钥和证书,我们必须先将它们导出为pkcs#12格式的文件，然后再导入到Windows证书存储区中。
    这是因为Windows证书存储区是一个集成的系统位置，它不支持直接使用单独的私钥文件和证书文件。pkcs#12格式是一种可以存储和保护密钥和证书的复杂格式1。
    您可以使用openssl工具来将私钥文件和证书文件合并成一个pkcs#12格式的文件，
    例如：openssl pkcs12 -export -out cert.pfx -inkey private.key -in cert.cer2。
    然后，您可以使用certutil工具或者图形界面来导入这个文件到本地计算机的个人存储区（Personal store）中。
    导入后，您就可以使用CertFindCertificateInStore等接口来创建和操作证书上下文了。
    */
    return NULL;
}

hssl_ctx_t hssl_ctx_new(hssl_ctx_opt_t* opt)
{
    SECURITY_STATUS SecStatus;
    TimeStamp Lifetime;
    CredHandle* hCred = malloc(sizeof(CredHandle));
    SCHANNEL_CRED credData = { 0 };
    TCHAR unisp_name[] = UNISP_NAME;
    unsigned long credflag;
    SecPkgCred_SupportedAlgs algs;

    if (opt && opt->endpoint == HSSL_SERVER) {
        PCCERT_CONTEXT serverCert = NULL; // server-side certificate
#if 1 // create ceart from store

        //-------------------------------------------------------
        // Get the server certificate.
        //-------------------------------------------------------
        // Open the My store(personal store).
        HCERTSTORE hMyCertStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, X509_ASN_ENCODING, 0, CERT_SYSTEM_STORE_LOCAL_MACHINE, L"MY");
        if (hMyCertStore == NULL) {
            printf("Error opening MY store for server.\n");
            return NULL;
        }
        //-------------------------------------------------------
        // Search for a certificate match its subject string to opt->crt_file.
        serverCert = CertFindCertificateInStore(hMyCertStore, X509_ASN_ENCODING, 0, CERT_FIND_SUBJECT_STR_A, opt->crt_file, NULL);
        CertCloseStore(hMyCertStore, 0);
        if (serverCert == NULL) {
            printf("Error retrieving server certificate. %x\n", GetLastError());
            return NULL;
        }
#else
        serverCert = getservercert(opt->ca_file);
#endif
        credData.cCreds = 1; // 数量
        credData.paCred = &serverCert;
        // credData.dwCredFormat = SCH_CRED_FORMAT_CERT_HASH;
#if 0 // just use the system defalut algs
        ALG_ID rgbSupportedAlgs[4];
        rgbSupportedAlgs[0] = CALG_DH_EPHEM;
        rgbSupportedAlgs[1] = CALG_RSA_KEYX;
        rgbSupportedAlgs[2] = CALG_AES_128;
        rgbSupportedAlgs[3] = CALG_SHA_256;
        credData.cSupportedAlgs = 4;
        credData.palgSupportedAlgs = rgbSupportedAlgs;
#endif
        credData.grbitEnabledProtocols = SP_PROT_TLS1_2_SERVER | SP_PROT_TLS1_3_SERVER;
        credflag = SECPKG_CRED_INBOUND;
    } else {
        credData.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT;
        credflag = SECPKG_CRED_OUTBOUND;
    }

    credData.dwVersion = SCHANNEL_CRED_VERSION;
    // credData.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_NO_SERVERNAME_CHECK | SCH_USE_STRONG_CRYPTO;
    // credData.dwMinimumCipherStrength = -1;
    // credData.dwMaximumCipherStrength = -1;

    //-------------------------------------------------------
    SecStatus = AcquireCredentialsHandle(NULL, unisp_name, credflag, NULL, &credData, NULL, NULL, hCred, &Lifetime);
    if (SecStatus != SEC_E_OK) {
        printf("ERROR: AcquireCredentialsHandle: 0x%x\n", SecStatus);
        abort();
    }
    // Return the handle to the caller.
    SecStatus = QueryCredentialsAttributesA(hCred, SECPKG_ATTR_SUPPORTED_ALGS, &algs);
    if (SecStatus == SEC_E_OK) {
        for (int i = 0; i < algs.cSupportedAlgs; i++) {
            fprintf(stderr, "alg: 0x%08x\n", algs.palgSupportedAlgs[i]);
        }
    }

    return hCred;
}

void hssl_ctx_free(hssl_ctx_t ssl_ctx)
{
    SECURITY_STATUS sec_status = FreeCredentialsHandle(ssl_ctx);
    if (sec_status != SEC_E_OK) {
        printf("free_cred_handle FreeCredentialsHandle %d\n", sec_status);
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

struct wintls_s {
    hssl_ctx_t ssl_ctx; // CredHandle
    int fd;
    bool first_iteration;
    SecHandle sechandle;
    SecPkgContext_StreamSizes stream_sizes_;
    char encrypted_buffer_[TLS_SOCKET_BUFFER_SIZE];
    char buffer_to_decrypt_[TLS_SOCKET_BUFFER_SIZE];
    char* sni;
};

hssl_t hssl_new(hssl_ctx_t ssl_ctx, int fd)
{
    struct wintls_s* ret = malloc(sizeof(*ret));
    memset(ret, 0, sizeof(*ret));
    ret->ssl_ctx = ssl_ctx;
    ret->fd = fd;
    ret->first_iteration = 1;
    ret->sechandle.dwLower = 0;
    ret->sechandle.dwUpper = 0;
    return ret;
}

void hssl_free(hssl_t _ssl)
{
    struct wintls_s* ssl = _ssl;
    SECURITY_STATUS sec_status = DeleteSecurityContext(&ssl->sechandle);
    if (sec_status != SEC_E_OK) {
        printf("hssl_free DeleteSecurityContext %d", sec_status);
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

int hssl_accept(hssl_t ssl)
{
    int ret = 0;
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

    secure_buffer_in[0].cbBuffer = recv(winssl->fd, (char*)secure_buffer_in[0].pvBuffer, TLS_SOCKET_BUFFER_SIZE, 0);
    printf("%s recv %d %d\n", __func__, secure_buffer_in[0].cbBuffer, WSAGetLastError());
    if (secure_buffer_in[0].cbBuffer == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
        ret = HSSL_WANT_READ;
        goto END;
    }

    SECURITY_STATUS sec_status = AcceptSecurityContext(winssl->ssl_ctx,
        winssl->first_iteration ? NULL : &winssl->sechandle,
        &secure_buffer_desc_in, context_requirements, 0,
        &winssl->sechandle, &secure_buffer_desc_out,
        &context_attributes, &life_time);

    winssl->first_iteration = false;
    printf("establish_server_security_context AcceptSecurityContext %x\n", sec_status);

    if (secure_buffer_out[0].cbBuffer > 0) {
        int rc = send(winssl->fd, (const char*)secure_buffer_out[0].pvBuffer, secure_buffer_out[0].cbBuffer, 0);
        if (rc != secure_buffer_out[0].cbBuffer) {
            goto END;
        }
    }

    switch (sec_status) {
    case SEC_E_OK:
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
            printf("establish_server_security_context CompleteAuthToken %x\n", complete_sec_status);
            ret = -1;
            goto END;
        }

        if (sec_status == SEC_I_COMPLETE_NEEDED) {
            authn_completed = true;
        } else {
            ret = HSSL_WANT_READ;
        }
        break;
    }
    default:
        ret = -1;
    }
END:
    free_all_buffers(&secure_buffer_desc_out);

    if (authn_completed) {
        SECURITY_STATUS sec_status = QueryContextAttributes(&winssl->sechandle, SECPKG_ATTR_STREAM_SIZES, &winssl->stream_sizes_);
        if (sec_status != SEC_E_OK) {
            printf("get_stream_sizes QueryContextAttributes %d\n", sec_status);
        }
    }
    return ret;
}

static void establish_client_security_context_first_stage(struct wintls_s* ssl)
{
    ULONG context_attributes = 0;
    unsigned long context_requirements = ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_CONFIDENTIALITY;

    TimeStamp life_time = { 0 };

    SecBuffer secure_buffer_out[1] = { 0 };
    init_sec_buffer(&secure_buffer_out[0], SECBUFFER_EMPTY, 0, NULL);

    SecBufferDesc secure_buffer_desc_out = { 0 };
    init_sec_buffer_desc(&secure_buffer_desc_out, SECBUFFER_VERSION, 1, secure_buffer_out);

    // ssl->sni = strdup("localhost");
    SECURITY_STATUS sec_status = InitializeSecurityContext(ssl->ssl_ctx, NULL, ssl->sni,
        context_requirements, 0, 0, NULL, 0, &ssl->sechandle,
        &secure_buffer_desc_out, &context_attributes, &life_time);

    if (sec_status != SEC_I_CONTINUE_NEEDED) {
        printf("InitializeSecurityContext: %x\n", sec_status);
    }

    if (secure_buffer_out[0].cbBuffer > 0) {
        int rc = send(ssl->fd, (const char*)secure_buffer_out[0].pvBuffer, secure_buffer_out[0].cbBuffer, 0);
        if (rc != secure_buffer_out[0].cbBuffer) {
            // TODO: Handle the error
            printf("%s :send failed\n", __func__);
        }
    }
    free_all_buffers(&secure_buffer_desc_out);
}

static int establish_client_security_context_second_stage(struct wintls_s* ssl)
{
    int ret = 0;
    ULONG context_attributes = 0;
    bool verify_server_cert = 0;

    unsigned long context_requirements = ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_CONFIDENTIALITY;
    if (!verify_server_cert) {
        context_requirements |= ISC_REQ_MANUAL_CRED_VALIDATION;
    }

    TimeStamp life_time = { 0 };

    // Allocate a temporary buffer for input
    char* buffer_in = malloc(TLS_SOCKET_BUFFER_SIZE);
    if (buffer_in == NULL) {
        printf("establish_client_security_context_second_stage: Memory allocation failed\n");
        return -1;
    }

    int offset = 0;
    bool skip_recv = false;

    bool authn_complete = false;
    while (!authn_complete) {
        int in_buffer_size = 0;

        if (!skip_recv) {
            int received = recv(ssl->fd, buffer_in + offset, TLS_SOCKET_BUFFER_SIZE, 0);
            if (received == SOCKET_ERROR) {
                if (WSAGetLastError() == WSAEWOULDBLOCK) {
                    ret = HSSL_WANT_READ;
                    goto END;
                } else {
                    printf("establish_client_security_context_second_stage: Receive failed\n");
                    ret = -1;
                    goto END;
                }
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
        init_sec_buffer(&secure_buffer_in[2], SECBUFFER_EMPTY, 0, NULL);
        init_sec_buffer(&secure_buffer_in[3], SECBUFFER_EMPTY, 0, NULL);

        SecBufferDesc secure_buffer_desc_in = { 0 };
        init_sec_buffer_desc(&secure_buffer_desc_in, SECBUFFER_VERSION, 4, secure_buffer_in);

        // Output buffer
        SecBuffer secure_buffer_out[3] = { 0 };
        init_sec_buffer(&secure_buffer_out[0], SECBUFFER_TOKEN, 0, NULL);
        init_sec_buffer(&secure_buffer_out[1], SECBUFFER_ALERT, 0, NULL);
        init_sec_buffer(&secure_buffer_out[2], SECBUFFER_EMPTY, 0, NULL);

        SecBufferDesc secure_buffer_desc_out = { 0 };
        init_sec_buffer_desc(&secure_buffer_desc_out, SECBUFFER_VERSION, 3, secure_buffer_out);

        SECURITY_STATUS sec_status = InitializeSecurityContext(ssl->ssl_ctx, &ssl->sechandle, ssl->sni, context_requirements, 0, 0,
            &secure_buffer_desc_in, 0, &ssl->sechandle,
            &secure_buffer_desc_out, &context_attributes, &life_time);

        if (sec_status == SEC_E_OK || sec_status == SEC_I_CONTINUE_NEEDED) {
            if (secure_buffer_out[0].cbBuffer > 0) {
                int rc = send(ssl->fd, (const char*)secure_buffer_out[0].pvBuffer, secure_buffer_out[0].cbBuffer, 0);
                if (rc != secure_buffer_out[0].cbBuffer) {
                    printf("establish_client_security_context_second_stage: Send failed\n");
                    // TODO: Handle the error
                    ret = -1;
                    goto END;
                }
                printf("%s :send ok\n", __func__);
            }

            if (sec_status == SEC_I_CONTINUE_NEEDED) {
                for (int i = 1; i < 3; ++i) {
                    if (secure_buffer_in[i].BufferType == SECBUFFER_EXTRA && secure_buffer_in[i].cbBuffer > 0) {
                        offset = secure_buffer_in[0].cbBuffer - secure_buffer_in[i].cbBuffer;
                        memmove(buffer_in, buffer_in + offset, secure_buffer_in[i].cbBuffer);
                        offset = secure_buffer_in[i].cbBuffer;

                        skip_recv = true;
                        break;
                    }
                }
            }

            if (sec_status == SEC_E_OK) {
                authn_complete = true;
            }
        } else if (sec_status == SEC_E_INCOMPLETE_MESSAGE) {
            offset = secure_buffer_in[0].cbBuffer;
        } else {
            printf("InitializeSecurityContext: 0x%x\n", sec_status);
            ret = -1;
            goto END;
        }
        free_all_buffers(&secure_buffer_desc_out);

        if (!authn_complete && !skip_recv) {
            ret = HSSL_WANT_READ;
            break;
        }
    }
END:
    free(buffer_in); // Free the temporary buffer
    return ret;
}

int hssl_connect(hssl_t _ssl)
{
    int ret = 0;
    struct wintls_s* ssl = _ssl;
    if (ssl->first_iteration) {
        ssl->first_iteration = false;
        establish_client_security_context_first_stage(ssl);
    }

    ret = establish_client_security_context_second_stage(ssl);
    printf("%s %x\n", __func__, ret);
    if (!ret) {
        SECURITY_STATUS sec_status = QueryContextAttributes(&ssl->sechandle, SECPKG_ATTR_STREAM_SIZES, &ssl->stream_sizes_);
        if (sec_status != SEC_E_OK) {
            printf("get_stream_sizes QueryContextAttributes %d\n", sec_status);
        }
    }
    return ret;
}

static int decrypt_message(SecHandle security_context, SecPkgContext_StreamSizes stream_sizes, const char* in_buf, int in_len, char* out_buf, int out_len)
{
    int msg_size = in_len - stream_sizes.cbHeader - stream_sizes.cbTrailer;
    if (msg_size > (int)stream_sizes.cbMaximumMessage) {
        printf("decrypt_message: Message to is too long\n");
        return -1;
    }

    if (msg_size > out_len) {
        printf("decrypt_message: Output buffer is too small\n");
        return -1;
    }

    // Allocate a temporary buffer for decryption
    char* decrypt_buf = malloc(in_len);
    if (decrypt_buf == NULL) {
        printf("decrypt_message: Memory allocation failed\n");
        return -1;
    }

    // Initialize the secure buffers
    SecBuffer secure_buffers[4] = { 0 };
    init_sec_buffer(&secure_buffers[0], SECBUFFER_DATA, in_len, decrypt_buf);
    init_sec_buffer(&secure_buffers[1], SECBUFFER_EMPTY, 0, NULL);
    init_sec_buffer(&secure_buffers[2], SECBUFFER_EMPTY, 0, NULL);
    init_sec_buffer(&secure_buffers[3], SECBUFFER_EMPTY, 0, NULL);

    // Initialize the secure buffer descriptor
    SecBufferDesc secure_buffer_desc = { 0 };
    init_sec_buffer_desc(&secure_buffer_desc, SECBUFFER_VERSION, 4, secure_buffers);

    // Copy encrypted message to in-out temp buffer
    memcpy(decrypt_buf, in_buf, in_len);

    // Decrypt the message using the security context
    SECURITY_STATUS sec_status = DecryptMessage(&security_context, &secure_buffer_desc, 0, NULL);

    if (sec_status == SEC_E_INCOMPLETE_MESSAGE) {
        free(decrypt_buf);
        return -1;
    }

    if (sec_status != SEC_E_OK) {
        printf("decrypt_message DecryptMessage: %d\n", sec_status);
        free(decrypt_buf);
        return -1;
    }
    if (secure_buffers[1].cbBuffer > (unsigned int)msg_size) {
        printf("decrypt_message: Data buffer is too large\n");
        free(decrypt_buf);
        return -1;
    }

    memcpy(out_buf, secure_buffers[1].pvBuffer, secure_buffers[1].cbBuffer);
    free(decrypt_buf);
    return secure_buffers[1].cbBuffer;
}

int hssl_read(hssl_t _ssl, void* buf, int len)
{
    struct wintls_s* ssl = _ssl;

    int total_decrypted_len = 0;

    size_t buffer_to_decrypt_offset_ = 0;

    // We might have leftovers, an incomplete message from a previous call.
    // Calculate the available buffer length for tcp recv.
    int recv_max_len = TLS_SOCKET_BUFFER_SIZE - buffer_to_decrypt_offset_;
    int bytes_received = recv(ssl->fd, ssl->buffer_to_decrypt_ + buffer_to_decrypt_offset_, recv_max_len, 0);
    printf("%s recv %d %d\n", __func__, bytes_received, WSAGetLastError());
    if (bytes_received == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            bytes_received = 0;
            return 0;
        } else {
            return -1;
        }
    }

    int decrypted_buffer_offset = 0;
    int encrypted_buffer_len = buffer_to_decrypt_offset_ + bytes_received;
    buffer_to_decrypt_offset_ = 0;
    char* decrypted_buffer_ = buf;
    while (true) {
        if (buffer_to_decrypt_offset_ >= encrypted_buffer_len) {
            // Reached the encrypted buffer length, we decrypted everything so we can stop.
            break;
        }

        int decrypted_len = decrypt_message(ssl->sechandle, ssl->stream_sizes_, ssl->buffer_to_decrypt_ + buffer_to_decrypt_offset_,
            encrypted_buffer_len - buffer_to_decrypt_offset_,
            decrypted_buffer_ + decrypted_buffer_offset,
            len - decrypted_buffer_offset);

        if (decrypted_len == -1) {
            // Incomplete message, we shuold keep it so it will be decrypted on the next call to recv().
            // Shift the remaining buffer to the beginning and break the loop.

            memmove(ssl->buffer_to_decrypt_, ssl->buffer_to_decrypt_ + buffer_to_decrypt_offset_, encrypted_buffer_len - buffer_to_decrypt_offset_);

            break;
        }

        total_decrypted_len += decrypted_len;
        decrypted_buffer_offset += decrypted_len;
        buffer_to_decrypt_offset_ += ssl->stream_sizes_.cbHeader + decrypted_len + ssl->stream_sizes_.cbTrailer;
    }

    buffer_to_decrypt_offset_ = encrypted_buffer_len - buffer_to_decrypt_offset_;

    return total_decrypted_len;
}

static int encrypt_message(SecHandle security_context, SecPkgContext_StreamSizes stream_sizes, const char* in_buf, int in_len, char* out_buf, int out_len)
{
    if (in_len > (int)stream_sizes.cbMaximumMessage) {
        printf("encrypt_message: Message is too long\n");
        return -1;
    }

    // Calculate the minimum output buffer length
    int min_out_len = stream_sizes.cbHeader + in_len + stream_sizes.cbTrailer;
    if (min_out_len > out_len) {
        printf("encrypt_message: Output buffer is too small");
        return -1;
    }

    // Initialize the secure buffers
    SecBuffer secure_buffers[4] = { 0 };
    init_sec_buffer(&secure_buffers[0], SECBUFFER_STREAM_HEADER, stream_sizes.cbHeader, out_buf);
    init_sec_buffer(&secure_buffers[1], SECBUFFER_DATA, in_len, out_buf + stream_sizes.cbHeader);
    init_sec_buffer(&secure_buffers[2], SECBUFFER_STREAM_TRAILER, stream_sizes.cbTrailer, out_buf + stream_sizes.cbHeader + in_len);
    init_sec_buffer(&secure_buffers[3], SECBUFFER_EMPTY, 0, NULL);

    // Initialize the secure buffer descriptor
    SecBufferDesc secure_buffer_desc = { 0 };
    init_sec_buffer_desc(&secure_buffer_desc, SECBUFFER_VERSION, 4, secure_buffers);

    // Copy the input buffer to the data buffer
    memcpy(secure_buffers[1].pvBuffer, in_buf, in_len);

    // Encrypt the message using the security context
    SECURITY_STATUS sec_status = EncryptMessage(&security_context, 0, &secure_buffer_desc, 0);

    // Check the encryption status and the data buffer length
    if (sec_status != SEC_E_OK) {
        printf("encrypt_message EncryptMessage %d\n", sec_status);
        return -1;
    }
    if (secure_buffers[1].cbBuffer > (unsigned int)in_len) {
        printf("encrypt_message: Data buffer is too large\n");
        return -1;
    }

    // Adjust the minimum output buffer length
    min_out_len = stream_sizes.cbHeader + secure_buffers[1].cbBuffer + stream_sizes.cbTrailer;

    return min_out_len;
}

int hssl_write(hssl_t _ssl, const void* buf, int len)
{
    struct wintls_s* ssl = _ssl;
    int out_len_result = encrypt_message(ssl->sechandle, ssl->stream_sizes_, buf, len, ssl->encrypted_buffer_, TLS_SOCKET_BUFFER_SIZE);

    // Check the encryption result
    if (out_len_result < 0) {
        printf("hssl_write: Encryption failed\n");
        return -1;
    }

    // Send the encrypted message to the socket
    int bytes_sent = send(ssl->fd, ssl->encrypted_buffer_, out_len_result, 0);

    // Check the send result
    if (bytes_sent != out_len_result) {
        printf("hssl_write: Send failed\n");
        return -1;
    }

    // Return the number of bytes sent excluding the header and trailer
    return bytes_sent - ssl->stream_sizes_.cbHeader - ssl->stream_sizes_.cbTrailer;
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
