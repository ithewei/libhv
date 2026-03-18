#include "http_compress.h"

#include <algorithm>
#include <ctype.h>
#include <limits>
#include <stdlib.h>
#include <string.h>
#include <sstream>
#include <vector>

#include "herr.h"
#include "hlog.h"
#include "hstring.h"

#ifdef WITH_ZLIB
#include <zlib.h>
#endif

#ifdef WITH_ZSTD
#include <zstd.h>
#if !defined(ZSTD_VERSION_NUMBER) || ZSTD_VERSION_NUMBER < 10500
#error "WITH_ZSTD requires libzstd >= 1.5.0"
#endif
#endif

static const char* s_identity_encoding = "identity";
static const char* s_gzip_encoding = "gzip";
static const char* s_zstd_encoding = "zstd";

#ifdef WITH_ZSTD
static const int s_http_zstd_window_log_max = 23;
#endif

static char* MutableStringData(std::string& str) {
    return str.empty() ? NULL : &str[0];
}

#ifdef WITH_ZLIB
static int CheckedSizeToUInt(size_t value, uInt* out) {
    if (value > (size_t)std::numeric_limits<uInt>::max()) {
        return ERR_OVER_LIMIT;
    }
    *out = (uInt)value;
    return 0;
}

static int CheckedSizeToULong(size_t value, uLong* out) {
    if (value > (size_t)std::numeric_limits<uLong>::max()) {
        return ERR_OVER_LIMIT;
    }
    *out = (uLong)value;
    return 0;
}
#endif

static http_content_encoding PreferredAvailableEncoding(unsigned enabled_mask) {
    if ((enabled_mask & HTTP_CONTENT_ENCODING_ZSTD_MASK) != 0) {
        return HTTP_CONTENT_ENCODING_ZSTD;
    }
    if ((enabled_mask & HTTP_CONTENT_ENCODING_GZIP_MASK) != 0) {
        return HTTP_CONTENT_ENCODING_GZIP;
    }
    return HTTP_CONTENT_ENCODING_IDENTITY;
}

const char* http_content_encoding_str(http_content_encoding encoding) {
    switch (encoding) {
    case HTTP_CONTENT_ENCODING_IDENTITY: return s_identity_encoding;
    case HTTP_CONTENT_ENCODING_GZIP:     return s_gzip_encoding;
    case HTTP_CONTENT_ENCODING_ZSTD:     return s_zstd_encoding;
    default:                             return "";
    }
}

http_content_encoding http_content_encoding_enum(const char* encoding) {
    if (encoding == NULL || *encoding == '\0') {
        return HTTP_CONTENT_ENCODING_IDENTITY;
    }
    if (stricmp(encoding, s_identity_encoding) == 0) {
        return HTTP_CONTENT_ENCODING_IDENTITY;
    }
    if (stricmp(encoding, s_gzip_encoding) == 0) {
        return HTTP_CONTENT_ENCODING_GZIP;
    }
    if (stricmp(encoding, s_zstd_encoding) == 0) {
        return HTTP_CONTENT_ENCODING_ZSTD;
    }
    return HTTP_CONTENT_ENCODING_UNKNOWN;
}

unsigned http_content_encoding_supported_mask() {
    unsigned mask = HTTP_CONTENT_ENCODING_IDENTITY_MASK;
#ifdef WITH_ZLIB
    mask |= HTTP_CONTENT_ENCODING_GZIP_MASK;
#endif
#ifdef WITH_ZSTD
    mask |= HTTP_CONTENT_ENCODING_ZSTD_MASK;
#endif
    return mask;
}

int http_content_encoding_is_available(http_content_encoding encoding) {
    switch (encoding) {
    case HTTP_CONTENT_ENCODING_IDENTITY:
        return 1;
    case HTTP_CONTENT_ENCODING_GZIP:
#ifdef WITH_ZLIB
        return 1;
#else
        return 0;
#endif
    case HTTP_CONTENT_ENCODING_ZSTD:
#ifdef WITH_ZSTD
        return 1;
#else
        return 0;
#endif
    default:
        return 0;
    }
}

WebSocketCompressionOptions::WebSocketCompressionOptions()
    : enabled(hv::WebSocketCompressionAvailable())
    , client_no_context_takeover(true)
    , server_no_context_takeover(true)
    , client_max_window_bits(15)
    , server_max_window_bits(15)
    , min_length(0)
    , max_decoded_size(64u << 20) {}

namespace hv {

static std::string ToLower(const std::string& value) {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return result;
}

static bool FindHeaderValue(const HttpMessage* msg, const char* key, std::string* value) {
    if (msg == NULL || key == NULL) {
        if (value) value->clear();
        return false;
    }
    auto iter = msg->headers.find(key);
    if (iter == msg->headers.end()) {
        if (value) value->clear();
        return false;
    }
    if (value) {
        *value = iter->second;
    }
    return true;
}

static double ParseQValue(const std::string& value) {
    char* end = NULL;
    double q = strtod(value.c_str(), &end);
    if (end == value.c_str()) {
        return 0.0;
    }
    if (q < 0.0) q = 0.0;
    if (q > 1.0) q = 1.0;
    return q;
}

static bool CompressionAllowsIdentity(const HttpCompressionOptions& options) {
    if (!options.enabled) {
        return true;
    }
    return (options.enabled_encodings & HTTP_CONTENT_ENCODING_IDENTITY_MASK) != 0;
}

HttpCompressionOptions DefaultServerCompressionOptions() {
    HttpCompressionOptions options;
    options.enabled = true;
    options.decompress_request = true;
    options.compress_request = false;
    options.decompress_response = false;
    options.compress_response = true;
    options.advertise_accept_encoding = false;
    options.enabled_encodings = http_content_encoding_supported_mask();
    options.preferred_encoding = PreferredAvailableEncoding(options.enabled_encodings);
    return options;
}

HttpCompressionOptions DefaultClientCompressionOptions() {
    HttpCompressionOptions options;
    options.enabled = true;
    options.decompress_request = false;
    options.compress_request = true;
    options.decompress_response = true;
    options.compress_response = false;
    options.advertise_accept_encoding = true;
    options.enabled_encodings = http_content_encoding_supported_mask();
    options.preferred_encoding = PreferredAvailableEncoding(options.enabled_encodings);
    return options;
}

WebSocketCompressionOptions DefaultWebSocketCompressionOptions() {
    return WebSocketCompressionOptions();
}

std::string BuildAcceptEncodingHeader(unsigned mask) {
    std::string header;
    if (mask & HTTP_CONTENT_ENCODING_GZIP_MASK) {
        header += s_gzip_encoding;
    }
    if (mask & HTTP_CONTENT_ENCODING_ZSTD_MASK) {
        if (!header.empty()) header += ", ";
        header += s_zstd_encoding;
    }
    if (header.empty()) {
        header = s_identity_encoding;
    }
    return header;
}

std::string BuildClientAcceptEncodingHeader(const HttpCompressionOptions& options) {
    unsigned mask = options.enabled_encodings & http_content_encoding_supported_mask();
    bool allow_identity = (options.enabled_encodings & HTTP_CONTENT_ENCODING_IDENTITY_MASK) != 0;
    std::vector<http_content_encoding> encodings;
    auto add_encoding = [&encodings, mask](http_content_encoding encoding) {
        if ((mask & (1u << encoding)) != 0) {
            encodings.push_back(encoding);
        }
    };

    if (options.preferred_encoding != HTTP_CONTENT_ENCODING_UNKNOWN &&
        options.preferred_encoding != HTTP_CONTENT_ENCODING_IDENTITY) {
        add_encoding(options.preferred_encoding);
    }
    if (options.preferred_encoding != HTTP_CONTENT_ENCODING_ZSTD) {
        add_encoding(HTTP_CONTENT_ENCODING_ZSTD);
    }
    if (options.preferred_encoding != HTTP_CONTENT_ENCODING_GZIP) {
        add_encoding(HTTP_CONTENT_ENCODING_GZIP);
    }

    std::string header;
    double qvalue = 1.0;
    for (size_t i = 0; i < encodings.size(); ++i) {
        http_content_encoding encoding = encodings[i];
        const char* name = http_content_encoding_str(encoding);
        if (name == NULL || *name == '\0') {
            continue;
        }
        if (!header.empty()) {
            header += ", ";
        }
        header += name;
        if (i != 0) {
            if (qvalue < 0.95) {
                header += hv::asprintf(";q=%.1f", qvalue);
            }
        }
        qvalue -= 0.1;
    }

    if (header.empty() && allow_identity) {
        header = s_identity_encoding;
    }
    if (!allow_identity) {
        if (!header.empty()) {
            header += ", ";
        }
        header += "identity;q=0";
    }
    return header;
}

bool ParseContentEncodingHeader(const std::string& header, std::vector<http_content_encoding>* encodings) {
    if (encodings == NULL) return false;
    encodings->clear();
    if (header.empty()) {
        return true;
    }
    auto items = hv::split(header, ',');
    for (size_t i = 0; i < items.size(); ++i) {
        std::string token = ToLower(trim(items[i]));
        if (token.empty()) {
            continue;
        }
        http_content_encoding encoding = http_content_encoding_enum(token.c_str());
        if (encoding == HTTP_CONTENT_ENCODING_UNKNOWN) {
            return false;
        }
        if (encoding == HTTP_CONTENT_ENCODING_IDENTITY) {
            continue;
        }
        encodings->push_back(encoding);
    }
    return true;
}

struct AcceptEncodingQuality {
    double identity;
    double gzip;
    double zstd;
    double wildcard;
    bool header_present;
    bool identity_seen;
    bool gzip_seen;
    bool zstd_seen;
    bool wildcard_seen;

    AcceptEncodingQuality()
        : identity(1.0)
        , gzip(0.0)
        , zstd(0.0)
        , wildcard(0.0)
        , header_present(false)
        , identity_seen(false)
        , gzip_seen(false)
        , zstd_seen(false)
        , wildcard_seen(false) {}
};

static AcceptEncodingQuality ParseAcceptEncoding(const std::string& header, bool header_present) {
    AcceptEncodingQuality quality;
    quality.header_present = header_present;
    if (!header_present) {
        quality.gzip = 1.0;
        quality.zstd = 1.0;
        return quality;
    }
    if (header.empty()) {
        return quality;
    }
    auto items = hv::split(header, ',');
    for (size_t i = 0; i < items.size(); ++i) {
        std::string item = trim(items[i]);
        if (item.empty()) continue;
        auto parts = hv::split(item, ';');
        std::string token = ToLower(trim(parts[0]));
        double q = 1.0;
        for (size_t j = 1; j < parts.size(); ++j) {
            std::string attr = trim(parts[j]);
            auto pos = attr.find('=');
            if (pos == std::string::npos) continue;
            std::string key = ToLower(trim(attr.substr(0, pos)));
            std::string value = trim(attr.substr(pos + 1));
            if (key == "q") {
                q = ParseQValue(value);
            }
        }
        if (token == s_identity_encoding) {
            quality.identity = q;
            quality.identity_seen = true;
        } else if (token == s_gzip_encoding) {
            quality.gzip = q;
            quality.gzip_seen = true;
        } else if (token == s_zstd_encoding) {
            quality.zstd = q;
            quality.zstd_seen = true;
        } else if (token == "*") {
            quality.wildcard = q;
            quality.wildcard_seen = true;
        }
    }
    return quality;
}

static double EncodingQuality(const AcceptEncodingQuality& quality, http_content_encoding encoding) {
    switch (encoding) {
    case HTTP_CONTENT_ENCODING_GZIP:
        if (!quality.header_present) {
            return 1.0;
        }
        if (quality.gzip_seen) {
            return quality.gzip;
        }
        if (quality.wildcard_seen) {
            return quality.wildcard;
        }
        return 0.0;
    case HTTP_CONTENT_ENCODING_ZSTD:
        if (!quality.header_present) {
            return 1.0;
        }
        if (quality.zstd_seen) {
            return quality.zstd;
        }
        if (quality.wildcard_seen) {
            return quality.wildcard;
        }
        return 0.0;
    case HTTP_CONTENT_ENCODING_IDENTITY:
        if (quality.identity_seen) {
            return quality.identity;
        }
        if (!quality.header_present) {
            return 1.0;
        }
        if (quality.wildcard_seen && quality.wildcard <= 0.0) {
            return 0.0;
        }
        return 1.0;
    default:
        return 0.0;
    }
}

static http_content_encoding SelectContentEncodingImpl(
        const std::string& accept_encoding,
        bool accept_encoding_present,
        unsigned enabled_mask,
        http_content_encoding preferred_encoding) {
    AcceptEncodingQuality quality = ParseAcceptEncoding(accept_encoding, accept_encoding_present);
    std::vector<http_content_encoding> candidates;
    if (preferred_encoding != HTTP_CONTENT_ENCODING_UNKNOWN) {
        candidates.push_back(preferred_encoding);
    }
    if (preferred_encoding != HTTP_CONTENT_ENCODING_ZSTD) {
        candidates.push_back(HTTP_CONTENT_ENCODING_ZSTD);
    }
    if (preferred_encoding != HTTP_CONTENT_ENCODING_GZIP) {
        candidates.push_back(HTTP_CONTENT_ENCODING_GZIP);
    }
    candidates.push_back(HTTP_CONTENT_ENCODING_IDENTITY);

    http_content_encoding best = HTTP_CONTENT_ENCODING_IDENTITY;
    double best_q = EncodingQuality(quality, HTTP_CONTENT_ENCODING_IDENTITY);
    for (size_t i = 0; i < candidates.size(); ++i) {
        http_content_encoding candidate = candidates[i];
        unsigned mask = 1u << candidate;
        if ((enabled_mask & mask) == 0) {
            continue;
        }
        double q = EncodingQuality(quality, candidate);
        if (q > best_q ||
            (q == best_q &&
             q > 0.0 &&
             best == HTTP_CONTENT_ENCODING_IDENTITY &&
             candidate != HTTP_CONTENT_ENCODING_IDENTITY)) {
            best = candidate;
            best_q = q;
        }
    }
    if (best_q <= 0.0) {
        return HTTP_CONTENT_ENCODING_UNKNOWN;
    }
    return best;
}

http_content_encoding SelectContentEncoding(
        const std::string& accept_encoding,
        unsigned enabled_mask,
        http_content_encoding preferred_encoding) {
    return SelectContentEncodingImpl(accept_encoding, true, enabled_mask, preferred_encoding);
}

http_content_encoding SelectResponseContentEncoding(
        const HttpRequest* req,
        const HttpResponse* resp,
        const HttpCompressionOptions& options,
        bool streaming) {
    if (req == NULL || resp == NULL) {
        return HTTP_CONTENT_ENCODING_IDENTITY;
    }
    if (!ShouldCompressResponse(req, resp, options, streaming)) {
        if (ShouldRejectIdentityResponse(req, resp)) {
            return HTTP_CONTENT_ENCODING_UNKNOWN;
        }
        return HTTP_CONTENT_ENCODING_IDENTITY;
    }
    unsigned enabled_mask = options.enabled_encodings & http_content_encoding_supported_mask();
    std::string accept_encoding;
    bool has_accept_encoding = FindHeaderValue(req, "Accept-Encoding", &accept_encoding);
    return SelectContentEncodingImpl(accept_encoding, has_accept_encoding, enabled_mask, options.preferred_encoding);
}

std::string BuildContentEncodingETag(const std::string& etag, http_content_encoding encoding) {
    if (encoding != HTTP_CONTENT_ENCODING_GZIP &&
        encoding != HTTP_CONTENT_ENCODING_ZSTD) {
        return etag;
    }
    if (etag.empty()) {
        return etag;
    }
    std::string suffix = "-";
    suffix += http_content_encoding_str(encoding);
    if (etag.size() >= 2 &&
        etag.front() == '"' &&
        etag.back() == '"') {
        std::string encoded = etag;
        encoded.insert(encoded.size() - 1, suffix);
        return encoded;
    }
    if (etag.size() >= 4 &&
        etag[0] == 'W' &&
        etag[1] == '/' &&
        etag[2] == '"' &&
        etag.back() == '"') {
        std::string encoded = etag;
        encoded.insert(encoded.size() - 1, suffix);
        return encoded;
    }
    return etag + suffix;
}

static bool ResponseStatusDisallowsBody(http_method request_method, const HttpResponse* resp) {
    if (resp == NULL) {
        return false;
    }
    if (resp->status_code / 100 == 1 ||
        resp->status_code == HTTP_STATUS_NO_CONTENT ||
        resp->status_code == HTTP_STATUS_RESET_CONTENT ||
        resp->status_code == HTTP_STATUS_NOT_MODIFIED) {
        return true;
    }
    return request_method == HTTP_CONNECT &&
           resp->status_code / 100 == 2;
}

bool SupportsEncoding(const HttpCompressionOptions& options, http_content_encoding encoding) {
    if (!options.enabled) {
        return encoding == HTTP_CONTENT_ENCODING_IDENTITY;
    }
    unsigned encoding_mask = 1u << encoding;
    return http_content_encoding_is_available(encoding) != 0 &&
           (options.enabled_encodings & encoding_mask) != 0;
}

bool IsAlreadyCompressedContentType(const HttpResponse* resp) {
    switch (const_cast<HttpResponse*>(resp)->ContentType()) {
    case APPLICATION_ZIP:
    case APPLICATION_GZIP:
    case APPLICATION_7Z:
    case APPLICATION_RAR:
    case IMAGE_JPEG:
    case IMAGE_PNG:
    case IMAGE_GIF:
    case IMAGE_ICO:
    case IMAGE_BMP:
    case IMAGE_WEBP:
    case VIDEO_MP4:
    case VIDEO_WEBM:
    case VIDEO_MPEG:
    case APPLICATION_PDF:
        return true;
    default:
        return false;
    }
}

bool ShouldCompressRequest(const HttpRequest* req, const HttpCompressionOptions& options) {
    if (!options.enabled || !options.compress_request) return false;
    if (req->headers.find("Content-Encoding") != req->headers.end()) return false;
    if (const_cast<HttpRequest*>(req)->IsPrecompressedContentType()) return false;
    if (const_cast<HttpRequest*>(req)->ContentLength() < options.min_length) return false;
    return true;
}

bool ShouldCompressResponse(const HttpRequest* req, const HttpResponse* resp, const HttpCompressionOptions& options, bool streaming) {
    if (!options.enabled || !options.compress_response) return false;
    if (streaming) return false;
    if (req == NULL) return false;
    if (ResponseStatusDisallowsBody(req->method, resp)) {
        return false;
    }
    if (resp->status_code == HTTP_STATUS_PARTIAL_CONTENT) return false;
    if (const_cast<HttpResponse*>(resp)->IsUpgrade()) return false;
    if (resp->headers.find("Content-Encoding") != resp->headers.end()) return false;
    if (const_cast<HttpResponse*>(resp)->ContentType() == TEXT_EVENT_STREAM) return false;
    if (IsAlreadyCompressedContentType(resp)) return false;
    if (!const_cast<HttpResponse*>(resp)->IsCompressibleContentType()) return false;
    if (const_cast<HttpResponse*>(resp)->ContentLength() < options.min_length) return false;
    return true;
}

bool ShouldRejectIdentityResponse(const HttpRequest* req, const HttpResponse* resp) {
    if (req == NULL || resp == NULL) return false;
    if (ResponseStatusDisallowsBody(req->method, resp)) {
        return false;
    }
    if (resp->status_code == HTTP_STATUS_PARTIAL_CONTENT) return false;
    if (const_cast<HttpResponse*>(resp)->IsUpgrade()) return false;
    if (resp->headers.find("Content-Encoding") != resp->headers.end()) return false;
    std::string accept_encoding;
    bool has_accept_encoding = FindHeaderValue(req, "Accept-Encoding", &accept_encoding);
    return SelectContentEncodingImpl(accept_encoding,
                                     has_accept_encoding,
                                     HTTP_CONTENT_ENCODING_IDENTITY_MASK,
                                     HTTP_CONTENT_ENCODING_IDENTITY) == HTTP_CONTENT_ENCODING_UNKNOWN;
}

int ApplyResponseCompression(
        const HttpRequest* req,
        HttpResponse* resp,
        const HttpCompressionOptions& options,
        bool streaming,
        http_content_encoding* applied_encoding) {
    if (applied_encoding) {
        *applied_encoding = HTTP_CONTENT_ENCODING_IDENTITY;
    }
    if (req == NULL || resp == NULL) {
        return 0;
    }
    http_content_encoding encoding = SelectResponseContentEncoding(req, resp, options, streaming);
    if (encoding == HTTP_CONTENT_ENCODING_UNKNOWN) {
        SetUnacceptableEncodingResponse(resp);
        if (applied_encoding) {
            *applied_encoding = HTTP_CONTENT_ENCODING_UNKNOWN;
        }
        return 0;
    }
    if (encoding == HTTP_CONTENT_ENCODING_IDENTITY) {
        EnsureVaryAcceptEncoding(resp);
        return 0;
    }

    const void* content = resp->Content();
    size_t content_length = resp->ContentLength();
    std::string compressed;
    int ret = CompressData(encoding, content, content_length, compressed);
    if (ret != 0) {
        return ret;
    }
    resp->body.swap(compressed);
    resp->content = NULL;
    resp->content_length = resp->body.size();
    resp->SetContentEncoding(encoding);
    std::string etag = resp->GetHeader("Etag");
    if (!etag.empty()) {
        resp->headers["Etag"] = BuildContentEncodingETag(etag, encoding);
    }
    resp->headers["Content-Length"] = hv::to_string(resp->content_length);
    EnsureVaryAcceptEncoding(resp);
    if (applied_encoding) {
        *applied_encoding = encoding;
    }
    return 0;
}

void EnsureVaryAcceptEncoding(HttpResponse* resp) {
    std::string vary = resp->GetHeader("Vary");
    if (vary.empty()) {
        resp->headers["Vary"] = "Accept-Encoding";
        return;
    }
    auto items = hv::split(vary, ',');
    for (size_t i = 0; i < items.size(); ++i) {
        if (stricmp(trim(items[i]).c_str(), "Accept-Encoding") == 0) {
            return;
        }
    }
    vary += ", Accept-Encoding";
    resp->headers["Vary"] = vary;
}

void AdvertiseAcceptEncoding(HttpResponse* resp, const HttpCompressionOptions& options, bool force) {
    if (resp == NULL) return;
    if (!options.enabled ||
        !options.decompress_request) {
        return;
    }
    if (!force && !options.advertise_accept_encoding) {
        return;
    }
    if (resp->headers.find("Accept-Encoding") != resp->headers.end()) {
        return;
    }
    resp->headers["Accept-Encoding"] = BuildClientAcceptEncodingHeader(options);
}

void PrepareDecodedMessageHeaders(HttpMessage* msg) {
    msg->headers.erase("Content-Encoding");
    msg->headers.erase("Content-Length");
    msg->content = NULL;
    msg->content_length = 0;
}

void NormalizeDecodedMessage(HttpMessage* msg) {
    PrepareDecodedMessageHeaders(msg);
    msg->content_length = msg->body.size();
    if (msg->body.empty()) {
        return;
    } else {
        msg->headers["Content-Length"] = hv::to_string(msg->body.size());
    }
}

void SetUnacceptableEncodingResponse(HttpResponse* resp) {
    resp->status_code = HTTP_STATUS_NOT_ACCEPTABLE;
    resp->body.clear();
    resp->content = NULL;
    resp->content_length = 0;
    resp->headers.erase("Content-Encoding");
    resp->headers.erase("Transfer-Encoding");
    resp->headers["Content-Length"] = "0";
    EnsureVaryAcceptEncoding(resp);
}

#ifdef WITH_ZLIB
static int CompressGzip(const void* data, size_t len, std::string& out) {
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return ERR_COMPRESS;
    }
    uLong input_len = 0;
    int cast_ret = CheckedSizeToULong(len, &input_len);
    if (cast_ret != 0) {
        deflateEnd(&stream);
        return cast_ret;
    }
    out.resize((size_t)deflateBound(&stream, input_len));
    uInt avail_in = 0;
    cast_ret = CheckedSizeToUInt(len, &avail_in);
    if (cast_ret != 0) {
        deflateEnd(&stream);
        return cast_ret;
    }
    uInt avail_out = 0;
    cast_ret = CheckedSizeToUInt(out.size(), &avail_out);
    if (cast_ret != 0) {
        deflateEnd(&stream);
        return cast_ret;
    }
    stream.next_in = (Bytef*)data;
    stream.avail_in = avail_in;
    stream.next_out = (Bytef*)MutableStringData(out);
    stream.avail_out = avail_out;
    int ret = deflate(&stream, Z_FINISH);
    if (ret != Z_STREAM_END) {
        deflateEnd(&stream);
        return ERR_COMPRESS;
    }
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return 0;
}
#endif

#ifdef WITH_ZSTD
static size_t InitHttpZstdDStream(ZSTD_DStream* stream) {
    size_t ret = ZSTD_DCtx_setParameter((ZSTD_DCtx*)stream, ZSTD_d_windowLogMax, s_http_zstd_window_log_max);
    if (ZSTD_isError(ret)) {
        return ret;
    }
    return ZSTD_initDStream(stream);
}

static int CompressZstd(const void* data, size_t len, std::string& out) {
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    if (cctx == NULL) {
        return ERR_COMPRESS;
    }
    size_t bound = ZSTD_compressBound(len);
    out.resize(bound);
    size_t ret = ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, 3);
    if (!ZSTD_isError(ret)) {
        ret = ZSTD_CCtx_setParameter(cctx, ZSTD_c_windowLog, s_http_zstd_window_log_max);
    }
    if (!ZSTD_isError(ret)) {
        ret = ZSTD_compress2(cctx, (void*)MutableStringData(out), bound, data, len);
    }
    ZSTD_freeCCtx(cctx);
    if (ZSTD_isError(ret)) {
        return ERR_COMPRESS;
    }
    out.resize(ret);
    return 0;
}
#endif

int CompressData(http_content_encoding encoding, const void* data, size_t len, std::string& out) {
    out.clear();
    switch (encoding) {
    case HTTP_CONTENT_ENCODING_IDENTITY:
        if (data != NULL && len != 0) {
            out.assign((const char*)data, len);
        }
        return 0;
    case HTTP_CONTENT_ENCODING_GZIP:
#ifdef WITH_ZLIB
        return CompressGzip(data, len, out);
#else
        return ERR_UNSUPPORTED_CONTENT_ENCODING;
#endif
    case HTTP_CONTENT_ENCODING_ZSTD:
#ifdef WITH_ZSTD
        return CompressZstd(data, len, out);
#else
        return ERR_UNSUPPORTED_CONTENT_ENCODING;
#endif
    default:
        return ERR_UNSUPPORTED_CONTENT_ENCODING;
    }
}

HttpStreamingDecompressor::HttpStreamingDecompressor()
    : max_output_size_(0)
    , total_output_size_(0) {}

HttpStreamingDecompressor::~HttpStreamingDecompressor() {
    Reset();
}

int HttpStreamingDecompressor::AppendOutput(const char* data, size_t len, std::string& out) {
    if (len == 0) return 0;
    total_output_size_ += len;
    if (max_output_size_ != 0 && total_output_size_ > max_output_size_) {
        return ERR_OVER_LIMIT;
    }
    out.append(data, len);
    return 0;
}

int HttpStreamingDecompressor::AppendStageOutput(const char* data, size_t len, std::string& out) {
    if (len == 0) return 0;
    if (max_output_size_ != 0 && out.size() + len > max_output_size_) {
        return ERR_OVER_LIMIT;
    }
    out.append(data, len);
    return 0;
}

int HttpStreamingDecompressor::Init(http_content_encoding encoding, size_t max_output_size) {
    std::vector<http_content_encoding> encodings;
    if (encoding != HTTP_CONTENT_ENCODING_IDENTITY) {
        encodings.push_back(encoding);
    }
    return Init(encodings, max_output_size);
}

int HttpStreamingDecompressor::Init(const std::vector<http_content_encoding>& encodings, size_t max_output_size) {
    Reset();
    max_output_size_ = max_output_size;
    total_output_size_ = 0;
    for (size_t i = encodings.size(); i > 0; --i) {
        http_content_encoding encoding = encodings[i - 1];
        if (encoding == HTTP_CONTENT_ENCODING_IDENTITY) {
            continue;
        }
        DecoderState decoder;
        int ret = InitDecoder(&decoder, encoding);
        if (ret != 0) {
            Reset();
            return ret;
        }
        decoders_.push_back(decoder);
    }
    return 0;
}

int HttpStreamingDecompressor::InitDecoder(DecoderState* decoder, http_content_encoding encoding) {
    if (decoder == NULL) return ERR_NULL_POINTER;
    decoder->encoding = encoding;
    decoder->finished = false;
    switch (encoding) {
    case HTTP_CONTENT_ENCODING_IDENTITY:
        return 0;
    case HTTP_CONTENT_ENCODING_GZIP:
#ifdef WITH_ZLIB
    {
        z_stream* stream = new z_stream;
        memset(stream, 0, sizeof(*stream));
        if (inflateInit2(stream, MAX_WBITS + 16) != Z_OK) {
            delete stream;
            return ERR_DECOMPRESS;
        }
        decoder->gzip_stream = stream;
        return 0;
    }
#else
        return ERR_UNSUPPORTED_CONTENT_ENCODING;
#endif
    case HTTP_CONTENT_ENCODING_ZSTD:
#ifdef WITH_ZSTD
    {
        ZSTD_DStream* stream = ZSTD_createDStream();
        if (stream == NULL) {
            return ERR_DECOMPRESS;
        }
        size_t ret = InitHttpZstdDStream(stream);
        if (ZSTD_isError(ret)) {
            ZSTD_freeDStream(stream);
            return ERR_DECOMPRESS;
        }
        decoder->zstd_stream = stream;
        return 0;
    }
#else
        return ERR_UNSUPPORTED_CONTENT_ENCODING;
#endif
    default:
        return ERR_UNSUPPORTED_CONTENT_ENCODING;
    }
}

int HttpStreamingDecompressor::UpdateDecoder(DecoderState* decoder, const char* data, size_t len, std::string& out) {
    out.clear();
    if (decoder == NULL) return ERR_NULL_POINTER;
    if (decoder->encoding == HTTP_CONTENT_ENCODING_IDENTITY) {
        if (data != NULL && len != 0) {
            out.assign(data, len);
        }
        return 0;
    }
    if (decoder->finished) {
        if (len == 0) {
            return 0;
        }
    }
    switch (decoder->encoding) {
    case HTTP_CONTENT_ENCODING_GZIP:
#ifdef WITH_ZLIB
    {
        z_stream* stream = (z_stream*)decoder->gzip_stream;
        if (decoder->finished) {
            if (inflateReset(stream) != Z_OK) {
                return ERR_DECOMPRESS;
            }
            decoder->finished = false;
        }
        unsigned char buffer[16384];
        stream->next_in = (Bytef*)data;
        int cast_ret = CheckedSizeToUInt(len, &stream->avail_in);
        if (cast_ret != 0) {
            return cast_ret;
        }
        while (stream->avail_in > 0) {
            stream->next_out = buffer;
            stream->avail_out = sizeof(buffer);
            int ret = inflate(stream, Z_NO_FLUSH);
            size_t produced = sizeof(buffer) - stream->avail_out;
            if (produced != 0) {
                int append_ret = AppendStageOutput((const char*)buffer, produced, out);
                if (append_ret != 0) {
                    return append_ret;
                }
            }
            if (ret == Z_STREAM_END) {
                decoder->finished = true;
                if (stream->avail_in != 0) {
                    if (inflateReset(stream) != Z_OK) {
                        return ERR_DECOMPRESS;
                    }
                    decoder->finished = false;
                    continue;
                }
                break;
            }
            if (ret != Z_OK) {
                return ERR_DECOMPRESS;
            }
            if (produced == 0 && stream->avail_in == 0) {
                break;
            }
        }
        return 0;
    }
#else
        return ERR_UNSUPPORTED_CONTENT_ENCODING;
#endif
    case HTTP_CONTENT_ENCODING_ZSTD:
#ifdef WITH_ZSTD
    {
        ZSTD_DStream* stream = (ZSTD_DStream*)decoder->zstd_stream;
        if (decoder->finished) {
            size_t reset_ret = InitHttpZstdDStream(stream);
            if (ZSTD_isError(reset_ret)) {
                return ERR_DECOMPRESS;
            }
            decoder->finished = false;
        }
        unsigned char buffer[16384];
        ZSTD_inBuffer input = { data, len, 0 };
        while (input.pos < input.size) {
            ZSTD_outBuffer output = { buffer, sizeof(buffer), 0 };
            size_t ret = ZSTD_decompressStream(stream, &output, &input);
            if (ZSTD_isError(ret)) {
                return ERR_DECOMPRESS;
            }
            if (output.pos != 0) {
                int append_ret = AppendStageOutput((const char*)buffer, output.pos, out);
                if (append_ret != 0) {
                    return append_ret;
                }
            }
            if (ret == 0) {
                decoder->finished = true;
                if (input.pos < input.size) {
                    size_t reset_ret = InitHttpZstdDStream(stream);
                    if (ZSTD_isError(reset_ret)) {
                        return ERR_DECOMPRESS;
                    }
                    decoder->finished = false;
                }
            }
        }
        return 0;
    }
#else
        return ERR_UNSUPPORTED_CONTENT_ENCODING;
#endif
    default:
        return ERR_UNSUPPORTED_CONTENT_ENCODING;
    }
}

int HttpStreamingDecompressor::FinishDecoder(DecoderState* decoder, std::string& out) {
    out.clear();
    if (decoder == NULL) return ERR_NULL_POINTER;
    if (decoder->encoding == HTTP_CONTENT_ENCODING_IDENTITY) {
        return 0;
    }
    if (decoder->finished) {
        return 0;
    }
    switch (decoder->encoding) {
    case HTTP_CONTENT_ENCODING_GZIP:
#ifdef WITH_ZLIB
    {
        z_stream* stream = (z_stream*)decoder->gzip_stream;
        unsigned char buffer[16384];
        for (;;) {
            stream->next_out = buffer;
            stream->avail_out = sizeof(buffer);
            int ret = inflate(stream, Z_FINISH);
            size_t produced = sizeof(buffer) - stream->avail_out;
            if (produced != 0) {
                int append_ret = AppendStageOutput((const char*)buffer, produced, out);
                if (append_ret != 0) {
                    return append_ret;
                }
            }
            if (ret == Z_STREAM_END) {
                decoder->finished = true;
                return 0;
            }
            if (ret != Z_BUF_ERROR && ret != Z_OK) {
                return ERR_DECOMPRESS;
            }
            if (produced == 0) {
                return ERR_DECOMPRESS;
            }
        }
    }
#else
        return ERR_UNSUPPORTED_CONTENT_ENCODING;
#endif
    case HTTP_CONTENT_ENCODING_ZSTD:
#ifdef WITH_ZSTD
    {
        ZSTD_DStream* stream = (ZSTD_DStream*)decoder->zstd_stream;
        unsigned char buffer[16384];
        for (;;) {
            ZSTD_inBuffer input = { NULL, 0, 0 };
            ZSTD_outBuffer output = { buffer, sizeof(buffer), 0 };
            size_t ret = ZSTD_decompressStream(stream, &output, &input);
            if (ZSTD_isError(ret)) {
                return ERR_DECOMPRESS;
            }
            if (output.pos != 0) {
                int append_ret = AppendStageOutput((const char*)buffer, output.pos, out);
                if (append_ret != 0) {
                    return append_ret;
                }
            }
            if (ret == 0) {
                decoder->finished = true;
                return 0;
            }
            if (output.pos == 0) {
                return ERR_DECOMPRESS;
            }
        }
    }
#else
        return ERR_UNSUPPORTED_CONTENT_ENCODING;
#endif
    default:
        return ERR_UNSUPPORTED_CONTENT_ENCODING;
    }
}

int HttpStreamingDecompressor::Update(const char* data, size_t len, std::string& out) {
    out.clear();
    if (decoders_.empty()) {
        return AppendOutput(data, len, out);
    }

    std::string current;
    if (data != NULL && len != 0) {
        current.assign(data, len);
    }
    for (size_t i = 0; i < decoders_.size(); ++i) {
        std::string stage_out;
        int ret = UpdateDecoder(&decoders_[i], current.data(), current.size(), stage_out);
        if (ret != 0) {
            return ret;
        }
        current.swap(stage_out);
    }
    if (!current.empty()) {
        return AppendOutput(current.data(), current.size(), out);
    }
    return 0;
}

int HttpStreamingDecompressor::Finish(std::string& out) {
    out.clear();
    if (decoders_.empty()) {
        return 0;
    }

    std::string current;
    for (size_t i = 0; i < decoders_.size(); ++i) {
        std::string stage_out;
        if (!current.empty()) {
            int update_ret = UpdateDecoder(&decoders_[i], current.data(), current.size(), stage_out);
            if (update_ret != 0) {
                return update_ret;
            }
        }
        std::string tail;
        int finish_ret = FinishDecoder(&decoders_[i], tail);
        if (finish_ret != 0) {
            return finish_ret;
        }
        if (!tail.empty()) {
            stage_out.append(tail);
        }
        current.swap(stage_out);
    }
    if (!current.empty()) {
        return AppendOutput(current.data(), current.size(), out);
    }
    return 0;
}

void HttpStreamingDecompressor::Reset() {
    for (size_t i = 0; i < decoders_.size(); ++i) {
        ResetDecoder(&decoders_[i]);
    }
    decoders_.clear();
    max_output_size_ = 0;
    total_output_size_ = 0;
}

void HttpStreamingDecompressor::ResetDecoder(DecoderState* decoder) {
    if (decoder == NULL) return;
#ifdef WITH_ZLIB
    if (decoder->gzip_stream != NULL) {
        z_stream* stream = (z_stream*)decoder->gzip_stream;
        inflateEnd(stream);
        delete stream;
        decoder->gzip_stream = NULL;
    }
#endif
#ifdef WITH_ZSTD
    if (decoder->zstd_stream != NULL) {
        ZSTD_freeDStream((ZSTD_DStream*)decoder->zstd_stream);
        decoder->zstd_stream = NULL;
    }
#endif
    decoder->encoding = HTTP_CONTENT_ENCODING_IDENTITY;
    decoder->finished = false;
}

static bool ResponseHasNoBody(http_method request_method, const HttpResponse* resp) {
    if (request_method == HTTP_HEAD) {
        return true;
    }
    return ResponseStatusDisallowsBody(request_method, resp);
}

static bool ShouldAutoDecodeResponse(const HttpCompressionOptions& options, http_method request_method, const HttpResponse* resp) {
    if (!options.enabled || !options.decompress_response) {
        return false;
    }
    if (ResponseHasNoBody(request_method, resp)) {
        return false;
    }
    if (resp != NULL && resp->status_code == HTTP_STATUS_PARTIAL_CONTENT) {
        return false;
    }
    return true;
}

HttpResponseDecoderAdapter::HttpResponseDecoderAdapter(const HttpCompressionOptions& options, http_method request_method)
    : options_(options)
    , response_(NULL)
    , error_(0)
    , saw_body_(false)
    , request_method_(request_method) {}

void HttpResponseDecoderAdapter::Install(HttpResponse* resp) {
    response_ = resp;
    user_cb_ = resp->http_cb;
    std::weak_ptr<HttpResponseDecoderAdapter> weak = shared_from_this();
    resp->http_cb = [weak](HttpMessage* msg, http_parser_state state, const char* data, size_t size) {
        std::shared_ptr<HttpResponseDecoderAdapter> self = weak.lock();
        if (self) {
            self->Handle(msg, state, data, size);
        }
    };
}

void HttpResponseDecoderAdapter::Forward(HttpMessage* msg, http_parser_state state, const char* data, size_t size) {
    if (user_cb_) {
        user_cb_(msg, state, data, size);
    }
}

void HttpResponseDecoderAdapter::Handle(HttpMessage* msg, http_parser_state state, const char* data, size_t size) {
    HttpResponse* resp = (HttpResponse*)msg;
    if (state == HP_MESSAGE_BEGIN) {
        error_ = 0;
        saw_body_ = false;
        original_encoding_.clear();
        decoder_.Reset();
        resp->body.clear();
        Forward(msg, state, data, size);
        return;
    }
    if (error_ != 0) {
        return;
    }
    switch (state) {
    case HP_HEADERS_COMPLETE:
    {
        if (ShouldAutoDecodeResponse(options_, request_method_, resp)) {
            original_encoding_ = resp->GetHeader("Content-Encoding");
            std::vector<http_content_encoding> encodings;
            if (original_encoding_.empty()) {
                if (!CompressionAllowsIdentity(options_)) {
                    error_ = ERR_UNSUPPORTED_CONTENT_ENCODING;
                }
            } else if (!ParseContentEncodingHeader(original_encoding_, &encodings)) {
                if (!original_encoding_.empty()) {
                    error_ = ERR_UNSUPPORTED_CONTENT_ENCODING;
                }
            } else if (!encodings.empty()) {
                for (size_t i = 0; i < encodings.size(); ++i) {
                    if (!SupportsEncoding(options_, encodings[i])) {
                        error_ = ERR_UNSUPPORTED_CONTENT_ENCODING;
                        break;
                    }
                }
                if (error_ == 0) {
                    error_ = decoder_.Init(encodings, options_.max_decoded_size);
                }
            }
        }
        if (decoder_.active()) {
            PrepareDecodedMessageHeaders(resp);
        }
        Forward(msg, state, data, size);
        return;
    }
    case HP_BODY:
    {
        saw_body_ = saw_body_ || size != 0;
        if (decoder_.active()) {
            std::string out;
            error_ = decoder_.Update(data, size, out);
            if (error_ != 0) {
                return;
            }
            if (!out.empty()) {
                resp->body.append(out);
                Forward(msg, HP_BODY, out.data(), out.size());
            }
        } else {
            resp->body.append(data, size);
            Forward(msg, state, data, size);
        }
        return;
    }
    case HP_MESSAGE_COMPLETE:
    {
        if (decoder_.active()) {
            if (!saw_body_) {
                NormalizeDecodedMessage(resp);
                Forward(msg, state, data, size);
                return;
            }
            std::string out;
            error_ = decoder_.Finish(out);
            if (error_ != 0) {
                return;
            }
            if (!out.empty()) {
                resp->body.append(out);
                Forward(msg, HP_BODY, out.data(), out.size());
            }
            NormalizeDecodedMessage(resp);
        } else {
            resp->content = NULL;
            resp->content_length = resp->body.size();
        }
        Forward(msg, state, data, size);
        return;
    }
    default:
        Forward(msg, state, data, size);
        return;
    }
}

bool WebSocketCompressionAvailable() {
#ifdef WITH_ZLIB
    return true;
#else
    return false;
#endif
}

std::string BuildWebSocketCompressionOffer(const WebSocketCompressionOptions& options) {
    if (!options.enabled || !WebSocketCompressionAvailable()) {
        return "";
    }
    std::string offer = "permessage-deflate";
    if (options.client_no_context_takeover) {
        offer += "; client_no_context_takeover";
    }
    if (options.server_no_context_takeover) {
        offer += "; server_no_context_takeover";
    }
    if (options.client_max_window_bits >= 8 && options.client_max_window_bits < 15) {
        offer += "; client_max_window_bits=" + hv::to_string(options.client_max_window_bits);
    } else {
        offer += "; client_max_window_bits";
    }
    if (options.server_max_window_bits >= 8 && options.server_max_window_bits < 15) {
        offer += "; server_max_window_bits=" + hv::to_string(options.server_max_window_bits);
    }
    return offer;
}

static bool ParseWindowBits(const std::string& value, int* bits) {
    if (value.empty()) {
        return false;
    }
    if (value.size() > 1 && value[0] == '0') {
        return false;
    }
    for (size_t i = 0; i < value.size(); ++i) {
        if (!isdigit((unsigned char)value[i])) {
            return false;
        }
    }
    char* end = NULL;
    long parsed = strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || (end != NULL && *end != '\0')) {
        return false;
    }
    if (parsed < 8 || parsed > 15) {
        return false;
    }
    *bits = (int)parsed;
    return true;
}

static int NormalizeWindowBits(int bits) {
    if (bits >= 8 && bits <= 15) {
        return bits;
    }
    return 15;
}

static bool IsHttpTokenChar(char c) {
    switch (c) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~':
        return true;
    default:
        return isalnum((unsigned char)c) != 0;
    }
}

static bool IsHttpToken(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    for (size_t i = 0; i < value.size(); ++i) {
        if (!IsHttpTokenChar(value[i])) {
            return false;
        }
    }
    return true;
}

static bool IsHttpQuotedString(const std::string& value) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        return false;
    }
    bool escaped = false;
    for (size_t i = 1; i + 1 < value.size(); ++i) {
        unsigned char c = (unsigned char)value[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"' || c == 0x7f || c < 0x20) {
            return false;
        }
    }
    return !escaped;
}

static bool NormalizeWebSocketExtensionValue(const std::string& value, std::string* normalized) {
    if (normalized == NULL) {
        return false;
    }
    if (IsHttpToken(value)) {
        *normalized = value;
        return true;
    }
    if (!IsHttpQuotedString(value)) {
        return false;
    }
    normalized->clear();
    bool escaped = false;
    for (size_t i = 1; i + 1 < value.size(); ++i) {
        char c = value[i];
        if (escaped) {
            normalized->push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        normalized->push_back(c);
    }
    return !escaped && IsHttpToken(*normalized);
}

static bool ValidateWebSocketExtensionParameter(const std::string& param) {
    if (param.empty()) {
        return false;
    }
    size_t pos = param.find('=');
    std::string key;
    if (pos == std::string::npos) {
        key = trim(param);
    } else {
        key = trim(param.substr(0, pos));
    }
    if (!IsHttpToken(key)) {
        return false;
    }
    if (pos == std::string::npos) {
        return true;
    }
    std::string value = trim(param.substr(pos + 1));
    if (value.empty()) {
        return false;
    }
    std::string normalized;
    return NormalizeWebSocketExtensionValue(value, &normalized);
}

static bool ValidateWebSocketExtensionSyntax(const std::string& extension) {
    if (extension.empty()) {
        return false;
    }
    auto parts = hv::split(extension, ';');
    if (parts.empty()) {
        return false;
    }
    if (!IsHttpToken(trim(parts[0]))) {
        return false;
    }
    for (size_t i = 1; i < parts.size(); ++i) {
        if (!ValidateWebSocketExtensionParameter(trim(parts[i]))) {
            return false;
        }
    }
    return true;
}

static bool ParseWebSocketCompressionExtensionsImpl(
        const std::string& extension,
        WebSocketCompressionHandshake* handshake,
        bool allow_client_max_window_bits_without_value) {
    if (handshake == NULL) return false;
    *handshake = WebSocketCompressionHandshake();
    auto parts = hv::split(extension, ';');
    if (parts.empty()) {
        return false;
    }
    if (stricmp(trim(parts[0]).c_str(), "permessage-deflate") != 0) {
        return false;
    }
    handshake->enabled = true;
    for (size_t j = 1; j < parts.size(); ++j) {
        std::string param = trim(parts[j]);
        auto pos = param.find('=');
        std::string key;
        std::string value;
        if (pos == std::string::npos) {
            key = ToLower(trim(param));
        } else {
            key = ToLower(trim(param.substr(0, pos)));
            value = trim(param.substr(pos + 1));
        }
        if (pos != std::string::npos) {
            std::string normalized_value;
            if (!NormalizeWebSocketExtensionValue(value, &normalized_value)) {
                return false;
            }
            value = normalized_value;
        }
        if (key == "client_no_context_takeover") {
            if (handshake->client_no_context_takeover || !value.empty()) {
                return false;
            }
            handshake->client_no_context_takeover = true;
        } else if (key == "server_no_context_takeover") {
            if (handshake->server_no_context_takeover || !value.empty()) {
                return false;
            }
            handshake->server_no_context_takeover = true;
        } else if (key == "client_max_window_bits") {
            if (handshake->client_max_window_bits_requested) {
                return false;
            }
            handshake->client_max_window_bits_requested = true;
            if (value.empty()) {
                if (!allow_client_max_window_bits_without_value) {
                    return false;
                }
            } else if (!ParseWindowBits(value, &handshake->client_max_window_bits)) {
                return false;
            }
        } else if (key == "server_max_window_bits") {
            if (handshake->server_max_window_bits_requested || value.empty()) {
                return false;
            }
            handshake->server_max_window_bits_requested = true;
            if (!ParseWindowBits(value, &handshake->server_max_window_bits)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

static bool ParseWebSocketCompressionOfferList(
        const std::string& header,
        std::vector<WebSocketCompressionHandshake>* offers) {
    if (offers == NULL) return false;
    offers->clear();
    if (header.empty()) {
        return true;
    }
    auto extensions = hv::split(header, ',');
    for (size_t i = 0; i < extensions.size(); ++i) {
        std::string extension = trim(extensions[i]);
        if (!ValidateWebSocketExtensionSyntax(extension)) {
            return false;
        }
        auto parts = hv::split(extension, ';');
        if (parts.empty()) {
            return false;
        }
        if (stricmp(trim(parts[0]).c_str(), "permessage-deflate") != 0) {
            continue;
        }
        WebSocketCompressionHandshake candidate;
        if (!ParseWebSocketCompressionExtensionsImpl(extension, &candidate, true) ||
            !candidate.enabled) {
            return false;
        }
        offers->push_back(candidate);
    }
    return true;
}

bool ParseWebSocketCompressionExtensions(const std::string& header, WebSocketCompressionHandshake* handshake) {
    if (handshake == NULL) return false;
    *handshake = WebSocketCompressionHandshake();
    std::vector<WebSocketCompressionHandshake> offers;
    if (!ParseWebSocketCompressionOfferList(header, &offers)) {
        return false;
    }
    if (!offers.empty()) {
        *handshake = offers.front();
    }
    return true;
}

static bool ParseWebSocketCompressionResponse(
        const std::string& header,
        WebSocketCompressionHandshake* handshake) {
    if (handshake == NULL) return false;
    *handshake = WebSocketCompressionHandshake();
    if (header.empty()) {
        return false;
    }
    auto extensions = hv::split(header, ',');
    for (size_t i = 0; i < extensions.size(); ++i) {
        std::string extension = trim(extensions[i]);
        WebSocketCompressionHandshake candidate;
        if (!ParseWebSocketCompressionExtensionsImpl(extension, &candidate, false) ||
            !candidate.enabled) {
            return false;
        }
        if (handshake->enabled) {
            return false;
        }
        *handshake = candidate;
    }
    return handshake->enabled;
}

bool NegotiateWebSocketCompression(
        const std::string& request_header,
        const WebSocketCompressionOptions& local_options,
        WebSocketCompressionHandshake* negotiated,
        std::string* response_header) {
    if (negotiated == NULL) return false;
    *negotiated = WebSocketCompressionHandshake();
    if (response_header) response_header->clear();
    if (!local_options.enabled || !WebSocketCompressionAvailable()) {
        return false;
    }
    std::vector<WebSocketCompressionHandshake> offers;
    if (!ParseWebSocketCompressionOfferList(request_header, &offers) || offers.empty()) {
        return false;
    }
    // RFC 7692 allows multiple permessage-deflate offers. Walk them in order and
    // accept the first one that matches the local server policy.
    for (size_t i = 0; i < offers.size(); ++i) {
        const WebSocketCompressionHandshake& offer = offers[i];
        if (offer.server_no_context_takeover &&
            !local_options.server_no_context_takeover) {
            continue;
        }
        negotiated->enabled = true;
        negotiated->client_no_context_takeover = local_options.client_no_context_takeover;
        negotiated->server_no_context_takeover = local_options.server_no_context_takeover;
        // The server may still tighten its own compression settings in the
        // response even if the client did not explicitly request them.
        negotiated->client_max_window_bits_requested = offer.client_max_window_bits_requested;
        negotiated->server_max_window_bits_requested =
                offer.server_max_window_bits_requested ||
                NormalizeWindowBits(local_options.server_max_window_bits) < 15;
        negotiated->client_max_window_bits = 15;
        if (offer.client_max_window_bits_requested) {
            negotiated->client_max_window_bits =
                    (std::min)(NormalizeWindowBits(local_options.client_max_window_bits),
                               NormalizeWindowBits(offer.client_max_window_bits));
        }
        negotiated->server_max_window_bits = NormalizeWindowBits(local_options.server_max_window_bits);
        if (offer.server_max_window_bits_requested) {
            negotiated->server_max_window_bits =
                    (std::min)(negotiated->server_max_window_bits,
                               NormalizeWindowBits(offer.server_max_window_bits));
        }

        std::string header = "permessage-deflate";
        if (negotiated->client_no_context_takeover) {
            header += "; client_no_context_takeover";
        }
        if (negotiated->server_no_context_takeover) {
            header += "; server_no_context_takeover";
        }
        if (negotiated->server_max_window_bits_requested) {
            header += "; server_max_window_bits=" + hv::to_string(negotiated->server_max_window_bits);
        }
        if (negotiated->client_max_window_bits_requested) {
            header += "; client_max_window_bits=" + hv::to_string(negotiated->client_max_window_bits);
        }
        if (response_header) {
            *response_header = header;
        }
        return true;
    }
    return false;
}

static bool BuildWebSocketCompressionHandshakeOffer(
        const WebSocketCompressionOptions& local_options,
        WebSocketCompressionHandshake* offer) {
    if (offer == NULL) return false;
    *offer = WebSocketCompressionHandshake();
    std::string request_header = BuildWebSocketCompressionOffer(local_options);
    if (request_header.empty()) {
        return false;
    }
    return ParseWebSocketCompressionExtensions(request_header, offer) && offer->enabled;
}

bool ConfirmWebSocketCompression(
        const std::string& response_header,
        const WebSocketCompressionHandshake& offer,
        WebSocketCompressionHandshake* negotiated) {
    if (negotiated == NULL) return false;
    *negotiated = WebSocketCompressionHandshake();
    if (!offer.enabled || !WebSocketCompressionAvailable()) {
        return false;
    }
    WebSocketCompressionHandshake response;
    if (!ParseWebSocketCompressionResponse(response_header, &response) || !response.enabled) {
        return false;
    }
    if (offer.server_no_context_takeover &&
        !response.server_no_context_takeover) {
        return false;
    }
    if (response.client_max_window_bits_requested) {
        if (!offer.client_max_window_bits_requested) {
            return false;
        }
        if (response.client_max_window_bits > NormalizeWindowBits(offer.client_max_window_bits)) {
            return false;
        }
    }
    if (offer.server_max_window_bits_requested) {
        if (!response.server_max_window_bits_requested) {
            return false;
        }
    }
    if (response.server_max_window_bits_requested &&
        offer.server_max_window_bits_requested) {
        if (response.server_max_window_bits > NormalizeWindowBits(offer.server_max_window_bits)) {
            return false;
        }
    }
    negotiated->enabled = true;
    negotiated->client_no_context_takeover = response.client_no_context_takeover;
    negotiated->server_no_context_takeover = response.server_no_context_takeover;
    negotiated->client_max_window_bits_requested = response.client_max_window_bits_requested;
    negotiated->server_max_window_bits_requested = response.server_max_window_bits_requested;
    negotiated->client_max_window_bits = response.client_max_window_bits;
    negotiated->server_max_window_bits = response.server_max_window_bits;
    return true;
}

bool ConfirmWebSocketCompression(
        const std::string& response_header,
        const std::string& request_header,
        WebSocketCompressionHandshake* negotiated) {
    if (negotiated == NULL) return false;
    *negotiated = WebSocketCompressionHandshake();
    std::vector<WebSocketCompressionHandshake> offers;
    if (!ParseWebSocketCompressionOfferList(request_header, &offers) || offers.empty()) {
        return false;
    }
    // Validate the server response against each original offer so fallback
    // negotiation remains valid when the client sent multiple candidates.
    for (size_t i = 0; i < offers.size(); ++i) {
        WebSocketCompressionHandshake candidate;
        if (ConfirmWebSocketCompression(response_header, offers[i], &candidate)) {
            *negotiated = candidate;
            return true;
        }
    }
    return false;
}

bool ConfirmWebSocketCompression(
        const std::string& response_header,
        const WebSocketCompressionOptions& local_options,
        WebSocketCompressionHandshake* negotiated) {
    WebSocketCompressionHandshake offer;
    if (!BuildWebSocketCompressionHandshakeOffer(local_options, &offer)) {
        return false;
    }
    return ConfirmWebSocketCompression(response_header, offer, negotiated);
}

WebSocketDeflater::WebSocketDeflater()
    : stream_(NULL)
    , window_bits_(15)
    , no_context_takeover_(true) {}

WebSocketDeflater::~WebSocketDeflater() {
    Destroy();
}

void WebSocketDeflater::Destroy() {
#ifdef WITH_ZLIB
    if (stream_ != NULL) {
        deflateEnd((z_stream*)stream_);
        delete (z_stream*)stream_;
        stream_ = NULL;
    }
#endif
}

int WebSocketDeflater::Init(int window_bits, bool no_context_takeover) {
    window_bits_ = window_bits;
    no_context_takeover_ = no_context_takeover;
    Destroy();
#ifdef WITH_ZLIB
    z_stream* stream = new z_stream;
    memset(stream, 0, sizeof(*stream));
    if (deflateInit2(stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -window_bits_, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        delete stream;
        return ERR_COMPRESS;
    }
    stream_ = stream;
    return 0;
#else
    return ERR_UNSUPPORTED_CONTENT_ENCODING;
#endif
}

int WebSocketDeflater::CompressMessage(const char* data, size_t len, std::string& out) {
    out.clear();
#ifdef WITH_ZLIB
    if (stream_ == NULL) {
        int ret = Init(window_bits_, no_context_takeover_);
        if (ret != 0) return ret;
    }
    z_stream* stream = (z_stream*)stream_;
    unsigned char buffer[16384];
    stream->next_in = (Bytef*)data;
    int cast_ret = CheckedSizeToUInt(len, &stream->avail_in);
    if (cast_ret != 0) {
        return cast_ret;
    }
    for (;;) {
        stream->next_out = buffer;
        stream->avail_out = sizeof(buffer);
        int ret = deflate(stream, Z_SYNC_FLUSH);
        if (ret != Z_OK) {
            return ERR_COMPRESS;
        }
        size_t produced = sizeof(buffer) - stream->avail_out;
        if (produced != 0) {
            out.append((const char*)buffer, produced);
        }
        if (stream->avail_in == 0 && produced < sizeof(buffer)) {
            break;
        }
    }
    if (out.size() >= 4) {
        out.resize(out.size() - 4);
    }
    if (no_context_takeover_) {
        deflateReset(stream);
    }
    return 0;
#else
    (void)data;
    (void)len;
    return ERR_UNSUPPORTED_CONTENT_ENCODING;
#endif
}

void WebSocketDeflater::Reset() {
#ifdef WITH_ZLIB
    if (stream_ != NULL) {
        deflateReset((z_stream*)stream_);
    }
#endif
}

WebSocketInflater::WebSocketInflater()
    : stream_(NULL)
    , window_bits_(15)
    , no_context_takeover_(true)
    , max_output_size_(64u << 20) {}

WebSocketInflater::~WebSocketInflater() {
    Destroy();
}

void WebSocketInflater::Destroy() {
#ifdef WITH_ZLIB
    if (stream_ != NULL) {
        inflateEnd((z_stream*)stream_);
        delete (z_stream*)stream_;
        stream_ = NULL;
    }
#endif
}

int WebSocketInflater::Init(int window_bits, bool no_context_takeover, size_t max_output_size) {
    window_bits_ = window_bits;
    no_context_takeover_ = no_context_takeover;
    max_output_size_ = max_output_size;
    Destroy();
#ifdef WITH_ZLIB
    z_stream* stream = new z_stream;
    memset(stream, 0, sizeof(*stream));
    if (inflateInit2(stream, -window_bits_) != Z_OK) {
        delete stream;
        return ERR_DECOMPRESS;
    }
    stream_ = stream;
    return 0;
#else
    return ERR_UNSUPPORTED_CONTENT_ENCODING;
#endif
}

int WebSocketInflater::DecompressMessage(const std::string& data, std::string& out) {
    out.clear();
#ifdef WITH_ZLIB
    if (stream_ == NULL) {
        int ret = Init(window_bits_, no_context_takeover_, max_output_size_);
        if (ret != 0) return ret;
    }
    static const unsigned char tail[4] = {0x00, 0x00, 0xff, 0xff};
    std::string payload(data);
    payload.append((const char*)tail, sizeof(tail));
    z_stream* stream = (z_stream*)stream_;
    unsigned char buffer[16384];
    size_t total = 0;
    stream->next_in = (Bytef*)MutableStringData(payload);
    int cast_ret = CheckedSizeToUInt(payload.size(), &stream->avail_in);
    if (cast_ret != 0) {
        return cast_ret;
    }
    for (;;) {
        stream->next_out = buffer;
        stream->avail_out = sizeof(buffer);
        int ret = inflate(stream, Z_SYNC_FLUSH);
        size_t produced = sizeof(buffer) - stream->avail_out;
        if (produced != 0) {
            total += produced;
            if (max_output_size_ != 0 && total > max_output_size_) {
                return ERR_OVER_LIMIT;
            }
            out.append((const char*)buffer, produced);
        }
        if (ret == Z_STREAM_END) {
            break;
        }
        if (ret == Z_BUF_ERROR && produced == 0) {
            return ERR_DECOMPRESS;
        }
        if (ret != Z_OK) {
            return ERR_DECOMPRESS;
        }
        if (stream->avail_in == 0 && produced < sizeof(buffer)) {
            break;
        }
    }
    if ((stream->data_type & 128) == 0) {
        return ERR_DECOMPRESS;
    }
    if (no_context_takeover_) {
        inflateReset(stream);
    }
    return 0;
#else
    (void)data;
    return ERR_UNSUPPORTED_CONTENT_ENCODING;
#endif
}

void WebSocketInflater::Reset() {
#ifdef WITH_ZLIB
    if (stream_ != NULL) {
        inflateReset((z_stream*)stream_);
    }
#endif
}

}
