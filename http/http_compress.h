#ifndef HV_HTTP_COMPRESS_H_
#define HV_HTTP_COMPRESS_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "HttpCompression.h"
#include "HttpMessage.h"

namespace hv {

std::string BuildAcceptEncodingHeader(unsigned mask);
std::string BuildClientAcceptEncodingHeader(const HttpCompressionOptions& options);
bool ParseContentEncodingHeader(const std::string& header, std::vector<http_content_encoding>* encodings);
http_content_encoding SelectContentEncoding(
        const std::string& accept_encoding,
        unsigned enabled_mask,
        http_content_encoding preferred_encoding);
http_content_encoding SelectResponseContentEncoding(
        const HttpRequest* req,
        const HttpResponse* resp,
        const HttpCompressionOptions& options,
        bool streaming);
std::string BuildContentEncodingETag(const std::string& etag, http_content_encoding encoding);
bool SupportsEncoding(const HttpCompressionOptions& options, http_content_encoding encoding);
bool ShouldCompressRequest(const HttpRequest* req, const HttpCompressionOptions& options);
bool ShouldCompressResponse(const HttpRequest* req, const HttpResponse* resp, const HttpCompressionOptions& options, bool streaming);
bool ShouldRejectIdentityResponse(const HttpRequest* req, const HttpResponse* resp);
bool IsAlreadyCompressedContentType(const HttpResponse* resp);
int ApplyResponseCompression(
        const HttpRequest* req,
        HttpResponse* resp,
        const HttpCompressionOptions& options,
        bool streaming,
        http_content_encoding* applied_encoding = NULL);
void EnsureVaryAcceptEncoding(HttpResponse* resp);
void AdvertiseAcceptEncoding(HttpResponse* resp, const HttpCompressionOptions& options, bool force = false);
void PrepareDecodedMessageHeaders(HttpMessage* msg);
void NormalizeDecodedMessage(HttpMessage* msg);
void SetUnacceptableEncodingResponse(HttpResponse* resp);

int CompressData(http_content_encoding encoding, const void* data, size_t len, std::string& out);

class HttpStreamingDecompressor {
public:
    HttpStreamingDecompressor();
    ~HttpStreamingDecompressor();

    int Init(http_content_encoding encoding, size_t max_output_size);
    int Init(const std::vector<http_content_encoding>& encodings, size_t max_output_size);
    int Update(const char* data, size_t len, std::string& out);
    int Finish(std::string& out);
    void Reset();

    bool active() const {
        return !decoders_.empty();
    }

    http_content_encoding encoding() const {
        return decoders_.empty() ? HTTP_CONTENT_ENCODING_IDENTITY : decoders_.front().encoding;
    }

private:
    struct DecoderState {
        http_content_encoding encoding;
        bool finished;
        void* gzip_stream;
        void* zstd_stream;

        DecoderState()
            : encoding(HTTP_CONTENT_ENCODING_IDENTITY)
            , finished(false)
            , gzip_stream(NULL)
            , zstd_stream(NULL) {}
    };

    int AppendOutput(const char* data, size_t len, std::string& out);
    int AppendStageOutput(const char* data, size_t len, std::string& out);
    int InitDecoder(DecoderState* decoder, http_content_encoding encoding);
    int UpdateDecoder(DecoderState* decoder, const char* data, size_t len, std::string& out);
    int FinishDecoder(DecoderState* decoder, std::string& out);
    void ResetDecoder(DecoderState* decoder);

private:
    size_t max_output_size_;
    size_t total_output_size_;
    std::vector<DecoderState> decoders_;
};

class HttpResponseDecoderAdapter : public std::enable_shared_from_this<HttpResponseDecoderAdapter> {
public:
    HttpResponseDecoderAdapter(const HttpCompressionOptions& options, http_method request_method);

    void Install(HttpResponse* resp);
    int error() const {
        return error_;
    }

private:
    void Handle(HttpMessage* msg, http_parser_state state, const char* data, size_t size);
    void Forward(HttpMessage* msg, http_parser_state state, const char* data, size_t size);

private:
    HttpCompressionOptions options_;
    HttpResponse* response_;
    std::function<void(HttpMessage*, http_parser_state, const char*, size_t)> user_cb_;
    HttpStreamingDecompressor decoder_;
    std::string original_encoding_;
    int error_;
    bool saw_body_;
    http_method request_method_;
};

bool WebSocketCompressionAvailable();

struct WebSocketCompressionHandshake {
    bool enabled;
    bool client_no_context_takeover;
    bool server_no_context_takeover;
    bool client_max_window_bits_requested;
    bool server_max_window_bits_requested;
    int  client_max_window_bits;
    int  server_max_window_bits;

    WebSocketCompressionHandshake()
        : enabled(false)
        , client_no_context_takeover(false)
        , server_no_context_takeover(false)
        , client_max_window_bits_requested(false)
        , server_max_window_bits_requested(false)
        , client_max_window_bits(15)
        , server_max_window_bits(15) {}
};

std::string BuildWebSocketCompressionOffer(const WebSocketCompressionOptions& options);
bool ParseWebSocketCompressionExtensions(const std::string& header, WebSocketCompressionHandshake* handshake);
bool NegotiateWebSocketCompression(
        const std::string& request_header,
        const WebSocketCompressionOptions& local_options,
        WebSocketCompressionHandshake* negotiated,
        std::string* response_header);
bool ConfirmWebSocketCompression(
        const std::string& response_header,
        const WebSocketCompressionHandshake& offer,
        WebSocketCompressionHandshake* negotiated);
bool ConfirmWebSocketCompression(
        const std::string& response_header,
        const std::string& request_header,
        WebSocketCompressionHandshake* negotiated);
bool ConfirmWebSocketCompression(
        const std::string& response_header,
        const WebSocketCompressionOptions& local_options,
        WebSocketCompressionHandshake* negotiated);

class WebSocketDeflater {
public:
    WebSocketDeflater();
    ~WebSocketDeflater();

    int Init(int window_bits, bool no_context_takeover);
    int CompressMessage(const char* data, size_t len, std::string& out);
    void Reset();
    bool ready() const { return stream_ != NULL; }

private:
    void Destroy();

private:
    void* stream_;
    int window_bits_;
    bool no_context_takeover_;
};

class WebSocketInflater {
public:
    WebSocketInflater();
    ~WebSocketInflater();

    int Init(int window_bits, bool no_context_takeover, size_t max_output_size);
    int DecompressMessage(const std::string& data, std::string& out);
    void Reset();
    bool ready() const { return stream_ != NULL; }

private:
    void Destroy();

private:
    void* stream_;
    int window_bits_;
    bool no_context_takeover_;
    size_t max_output_size_;
};

}

#endif // HV_HTTP_COMPRESS_H_
