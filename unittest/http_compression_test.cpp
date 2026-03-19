#include <atomic>
#include <thread>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "HttpServer.h"
#include "HttpClient.h"
#include "AsyncHttpClient.h"
#include "http_compress.h"
#include "hbase.h"
#include "herr.h"
#include "hthread.h"

#ifdef WITH_ZLIB
#include <zlib.h>
#endif

#ifdef WITH_ZSTD
#include <zstd.h>
#endif

using namespace hv;

namespace {

static bool Check(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "http_compression_test: %s\n", message);
        return false;
    }
    return true;
}

static std::string MakePayload(char ch, size_t len) {
    return std::string(len, ch);
}

static bool HasEncoding(http_content_encoding encoding) {
    return http_content_encoding_is_available(encoding) != 0;
}

static http_content_encoding PreferredAvailableEncoding() {
    if (HasEncoding(HTTP_CONTENT_ENCODING_ZSTD)) {
        return HTTP_CONTENT_ENCODING_ZSTD;
    }
    if (HasEncoding(HTTP_CONTENT_ENCODING_GZIP)) {
        return HTTP_CONTENT_ENCODING_GZIP;
    }
    return HTTP_CONTENT_ENCODING_IDENTITY;
}

static HttpCompressionOptions MakeOptions(unsigned encodings, http_content_encoding preferred) {
    HttpCompressionOptions options;
    options.enabled = true;
    options.decompress_request = false;
    options.compress_request = false;
    options.decompress_response = false;
    options.compress_response = false;
    options.advertise_accept_encoding = false;
    options.enabled_encodings = encodings | HTTP_CONTENT_ENCODING_IDENTITY_MASK;
    options.preferred_encoding = preferred;
    options.min_length = 1;
    options.max_decoded_size = 8u << 20;
    return options;
}

static bool WaitHttpReady(int port) {
    HttpClient client;
    client.setTimeout(1);
    for (int i = 0; i < 100; ++i) {
        HttpRequest req;
        HttpResponse resp;
        req.method = HTTP_GET;
        req.url = hv::asprintf("http://127.0.0.1:%d/health", port);
        int ret = client.send(&req, &resp);
        if (ret == 0 && resp.status_code == HTTP_STATUS_OK && resp.body == "ok") {
            return true;
        }
        hv_delay(20);
    }
    return false;
}

static bool SendAll(int fd, const std::string& data) {
    size_t offset = 0;
    while (offset < data.size()) {
        ssize_t nwrite = send(fd, data.data() + offset, data.size() - offset, 0);
        if (nwrite <= 0) {
            return false;
        }
        offset += (size_t)nwrite;
    }
    return true;
}

static bool RecvAll(int fd, std::string* data) {
    data->clear();
    char buffer[4096];
    while (true) {
        ssize_t nread = recv(fd, buffer, sizeof(buffer), 0);
        if (nread < 0) {
            return false;
        }
        if (nread == 0) {
            return true;
        }
        data->append(buffer, (size_t)nread);
    }
}

#ifdef WITH_ZLIB
static bool Gunzip(const std::string& compressed, std::string* decoded) {
    decoded->clear();
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, MAX_WBITS + 16) != Z_OK) {
        return false;
    }

    char buffer[4096];
    stream.next_in = (Bytef*)compressed.data();
    stream.avail_in = (uInt)compressed.size();
    int ret = Z_OK;
    while (ret == Z_OK) {
        stream.next_out = (Bytef*)buffer;
        stream.avail_out = sizeof(buffer);
        ret = inflate(&stream, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&stream);
            return false;
        }
        decoded->append(buffer, sizeof(buffer) - stream.avail_out);
    }

    inflateEnd(&stream);
    return ret == Z_STREAM_END;
}
#endif

#ifdef WITH_ZSTD
static bool DecompressZstd(const std::string& compressed, std::string* decoded) {
    decoded->clear();
    ZSTD_DStream* stream = ZSTD_createDStream();
    if (stream == NULL) {
        return false;
    }
    size_t init_ret = ZSTD_initDStream(stream);
    if (ZSTD_isError(init_ret)) {
        ZSTD_freeDStream(stream);
        return false;
    }

    ZSTD_inBuffer input = { compressed.data(), compressed.size(), 0 };
    char buffer[4096];
    while (true) {
        ZSTD_outBuffer output = { buffer, sizeof(buffer), 0 };
        size_t ret = ZSTD_decompressStream(stream, &output, &input);
        if (ZSTD_isError(ret)) {
            ZSTD_freeDStream(stream);
            return false;
        }
        decoded->append(buffer, output.pos);
        if (ret == 0 && input.pos == input.size) {
            break;
        }
        if (input.pos == input.size && output.pos == 0) {
            break;
        }
    }

    ZSTD_freeDStream(stream);
    return true;
}
#endif

static bool CompressSequence(const std::string& input, const std::vector<http_content_encoding>& encodings, std::string* output) {
    if (output == NULL) {
        return false;
    }
    std::string current = input;
    for (size_t i = 0; i < encodings.size(); ++i) {
        std::string compressed;
        if (CompressData(encodings[i], current.data(), current.size(), compressed) != 0) {
            return false;
        }
        current.swap(compressed);
    }
    *output = current;
    return true;
}

static unsigned NextDeterministic(unsigned* state) {
    *state = (*state * 1103515245u) + 12345u;
    return *state;
}

static std::string MutatePayloadDeterministically(const std::string& input, unsigned seed) {
    std::string mutated = input;
    if (mutated.empty()) {
        mutated.push_back((char)(seed & 0xFF));
        return mutated;
    }
    // Build a repeatable malformed corpus so failures can be reproduced
    // without needing an external fuzzing harness.
    unsigned state = seed | 1u;
    int operations = 1 + (int)(NextDeterministic(&state) % 4);
    for (int i = 0; i < operations; ++i) {
        unsigned action = NextDeterministic(&state) % 4;
        if (action == 0 || mutated.size() < 2) {
            size_t pos = NextDeterministic(&state) % mutated.size();
            mutated[pos] = (char)(mutated[pos] ^ (char)(1u << (NextDeterministic(&state) % 7)));
        } else if (action == 1) {
            size_t pos = NextDeterministic(&state) % mutated.size();
            mutated[pos] = (char)NextDeterministic(&state);
        } else if (action == 2 && mutated.size() > 1) {
            size_t shrink_limit = mutated.size() - 1;
            if (shrink_limit > 8) {
                shrink_limit = 8;
            }
            size_t shrink = 1 + (NextDeterministic(&state) % (unsigned)shrink_limit);
            mutated.resize(mutated.size() - shrink);
        } else {
            size_t append = 1 + (NextDeterministic(&state) % 4);
            for (size_t j = 0; j < append; ++j) {
                mutated.push_back((char)NextDeterministic(&state));
            }
        }
    }
    return mutated;
}

static bool TestRequestCompression(int port, http_content_encoding encoding, const char* encoding_name) {
    HttpClient client;
    client.setTimeout(3);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_POST;
    req.url = hv::asprintf("http://127.0.0.1:%d/echo", port);
    char fill = 'z';
    if (encoding == HTTP_CONTENT_ENCODING_GZIP) {
        fill = 'g';
    }
    req.body = MakePayload(fill, 2048);

    HttpCompressionOptions options = MakeOptions(1u << encoding, encoding);
    options.compress_request = true;
    req.SetCompression(options);

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "compressed request send failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_OK, "compressed request response status not OK")) return false;
    if (!Check(req.GetHeader("Content-Encoding") == encoding_name, "request Content-Encoding mismatch")) return false;
    if (!Check(resp.body == MakePayload(fill, 2048), "server did not decode compressed request body")) return false;
    if (!Check(resp.GetHeader("Content-Encoding").empty(), "response should not be compressed for request-only case")) return false;
    return true;
}

static bool TestRawResponseCompression(int port, http_content_encoding encoding, const char* encoding_name, const std::string& payload) {
    HttpClient client;
    client.setTimeout(3);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/payload", port);

    HttpCompressionOptions options = MakeOptions(1u << encoding, encoding);
    options.advertise_accept_encoding = true;
    req.SetCompression(options);

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "raw compressed response request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_OK, "raw compressed response status not OK")) return false;
    if (!Check(resp.GetHeader("Content-Encoding") == encoding_name, "response Content-Encoding mismatch")) return false;
    if (!Check(resp.GetHeader("Vary").find("Accept-Encoding") != std::string::npos, "response Vary header missing Accept-Encoding")) return false;
    if (!Check(resp.body != payload, "raw compressed response body should not match original payload")) return false;

    std::string decoded;
    bool ok = false;
    if (encoding == HTTP_CONTENT_ENCODING_GZIP) {
#ifdef WITH_ZLIB
        ok = Gunzip(resp.body, &decoded);
#endif
    } else if (encoding == HTTP_CONTENT_ENCODING_ZSTD) {
#ifdef WITH_ZSTD
        ok = DecompressZstd(resp.body, &decoded);
#endif
    }
    if (!Check(ok, "manual response decompression failed")) return false;
    if (!Check(decoded == payload, "manually decoded response payload mismatch")) return false;
    return true;
}

static bool TestAutoDecodeResponse(int port, const std::string& payload) {
    HttpClient client;
    client.setTimeout(3);

    HttpCompressionOptions options = MakeOptions(http_content_encoding_supported_mask(), HTTP_CONTENT_ENCODING_ZSTD);
    options.advertise_accept_encoding = true;
    options.decompress_response = true;
    client.setCompression(options);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/payload", port);

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "auto decode response request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_OK, "auto decode response status not OK")) return false;
    if (!Check(resp.body == payload, "auto decoded response payload mismatch")) return false;
    if (!Check(resp.GetHeader("Content-Encoding").empty(), "auto decoded response should not expose Content-Encoding")) return false;
    return true;
}

static bool TestResponseCallbackSeesDecodedHeaders(int port, const std::string& payload) {
    HttpClient client;
    client.setTimeout(3);

    HttpCompressionOptions options = MakeOptions(http_content_encoding_supported_mask(), PreferredAvailableEncoding());
    options.advertise_accept_encoding = true;
    options.decompress_response = true;
    client.setCompression(options);

    bool saw_headers = false;
    bool saw_body = false;
    bool saw_complete = false;
    bool content_encoding_removed = false;
    bool content_length_removed = false;
    std::string streamed_body;

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/payload", port);
    req.http_cb = [&](HttpMessage* msg, http_parser_state state, const char* data, size_t size) {
        HttpResponse* parsed = (HttpResponse*)msg;
        if (state == HP_HEADERS_COMPLETE) {
            saw_headers = true;
            content_encoding_removed = parsed->GetHeader("Content-Encoding").empty();
            content_length_removed = parsed->GetHeader("Content-Length").empty();
        } else if (state == HP_BODY) {
            saw_body = true;
            streamed_body.append(data, size);
        } else if (state == HP_MESSAGE_COMPLETE) {
            saw_complete = true;
        }
    };

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "response callback auto decode request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_OK, "response callback auto decode status not OK")) return false;
    if (!Check(saw_headers, "response callback should observe headers")) return false;
    if (!Check(saw_body, "response callback should observe decoded body")) return false;
    if (!Check(saw_complete, "response callback should observe message completion")) return false;
    if (!Check(content_encoding_removed, "response callback headers should hide Content-Encoding before body delivery")) return false;
    if (!Check(content_length_removed, "response callback headers should hide encoded Content-Length before body delivery")) return false;
    if (!Check(streamed_body == payload, "response callback should receive decoded body payload")) return false;
    return true;
}

static bool TestRedirectPreservesClientCompression(int port) {
    HttpClient client;
    client.setTimeout(3);

    HttpCompressionOptions options = MakeOptions(http_content_encoding_supported_mask(), HTTP_CONTENT_ENCODING_ZSTD);
    options.advertise_accept_encoding = false;
    options.decompress_response = false;
    client.setCompression(options);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.redirect = true;
    req.url = hv::asprintf("http://127.0.0.1:%d/redirect-accept", port);

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "redirect request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_OK, "redirect response status not OK")) return false;
    if (!Check(resp.body.empty(), "redirect should preserve disabled Accept-Encoding advertisement")) return false;
    return true;
}

static bool TestManualAcceptEncodingOverride(int port) {
    HttpClient client;
    client.setTimeout(3);

    HttpCompressionOptions options = MakeOptions(http_content_encoding_supported_mask(), HTTP_CONTENT_ENCODING_ZSTD);
    options.advertise_accept_encoding = true;
    client.setCompression(options);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/echo-accept", port);
    req.headers["Accept-Encoding"] = "br";

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "manual Accept-Encoding request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_OK, "manual Accept-Encoding status not OK")) return false;
    if (!Check(resp.body == "br", "manual Accept-Encoding header should not be overwritten")) return false;
    return true;
}

static bool TestExplicitEmptyAcceptEncoding(int port, const std::string& payload) {
    HttpClient client;
    client.setTimeout(3);

    HttpCompressionOptions options = MakeOptions(http_content_encoding_supported_mask(), PreferredAvailableEncoding());
    options.advertise_accept_encoding = true;
    client.setCompression(options);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/payload", port);
    req.headers["Accept-Encoding"] = "";

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "explicit empty Accept-Encoding request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_OK, "explicit empty Accept-Encoding status not OK")) return false;
    if (!Check(resp.body == payload, "explicit empty Accept-Encoding should keep identity payload")) return false;
    if (!Check(resp.GetHeader("Content-Encoding").empty(), "explicit empty Accept-Encoding should not apply content coding")) return false;
    if (!Check(resp.GetHeader("Vary").find("Accept-Encoding") != std::string::npos, "explicit empty Accept-Encoding response missing Vary header")) return false;
    if (!Check(req.headers.find("Accept-Encoding") != req.headers.end(), "explicit empty Accept-Encoding header should remain present")) return false;
    if (!Check(req.GetHeader("Accept-Encoding").empty(), "explicit empty Accept-Encoding header should remain empty")) return false;
    return true;
}

static bool TestManualContentEncodingDisablesRequestCompression(int port) {
    HttpClient client;
    client.setTimeout(3);

    HttpCompressionOptions options = MakeOptions(http_content_encoding_supported_mask(), HTTP_CONTENT_ENCODING_ZSTD);
    options.compress_request = true;
    client.setCompression(options);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_POST;
    req.url = hv::asprintf("http://127.0.0.1:%d/echo", port);
    req.headers["Content-Encoding"] = "identity";
    req.body = "manual-body";

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "manual Content-Encoding request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_OK, "manual Content-Encoding status not OK")) return false;
    if (!Check(req.GetHeader("Content-Encoding") == "identity", "manual Content-Encoding header should be preserved")) return false;
    if (!Check(resp.body == "manual-body", "manual Content-Encoding request body mismatch")) return false;
    return true;
}

static bool TestStateHandlerDecodedRequestHeaders(int port, http_content_encoding encoding) {
    HttpClient client;
    client.setTimeout(3);

    const char* encoding_name = http_content_encoding_str(encoding);
    char fill = 's';
    if (encoding == HTTP_CONTENT_ENCODING_GZIP) {
        fill = 'r';
    }
    const std::string payload = MakePayload(fill, 1024);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_POST;
    req.url = hv::asprintf("http://127.0.0.1:%d/state-echo", port);
    req.body = payload;

    HttpCompressionOptions options = MakeOptions(1u << encoding, encoding);
    options.compress_request = true;
    req.SetCompression(options);

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "state handler compressed request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_OK, "state handler compressed request status not OK")) return false;
    if (!Check(req.GetHeader("Content-Encoding") == encoding_name, "state handler request Content-Encoding mismatch")) return false;
    if (!Check(resp.body == "<missing>|" + hv::to_string(payload.size()) + "|" + payload,
               "state handler should observe decoded headers and body")) return false;
    return true;
}

static bool TestPreferredAcceptEncodingAdvertisement(int port) {
    HttpClient client;
    client.setTimeout(3);

    HttpCompressionOptions preferred = MakeOptions(http_content_encoding_supported_mask(), PreferredAvailableEncoding());
    preferred.advertise_accept_encoding = true;
    preferred.decompress_response = true;
    client.setCompression(preferred);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/echo-accept", port);

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "preferred Accept-Encoding request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_OK, "preferred Accept-Encoding status not OK")) return false;
    if (HasEncoding(HTTP_CONTENT_ENCODING_ZSTD) && HasEncoding(HTTP_CONTENT_ENCODING_GZIP)) {
        size_t zstd_pos = resp.body.find("zstd");
        size_t gzip_pos = resp.body.find("gzip");
        if (!Check(zstd_pos != std::string::npos && gzip_pos != std::string::npos && zstd_pos < gzip_pos,
                   "preferred Accept-Encoding should prioritize zstd")) return false;
        if (!Check(resp.body.find("q=0.9") != std::string::npos, "preferred Accept-Encoding should advertise q values")) return false;
    } else if (HasEncoding(HTTP_CONTENT_ENCODING_GZIP)) {
        if (!Check(resp.body == "gzip", "single-encoding advertisement should only include gzip")) return false;
    } else if (HasEncoding(HTTP_CONTENT_ENCODING_ZSTD)) {
        if (!Check(resp.body == "zstd", "single-encoding advertisement should only include zstd")) return false;
    }

    HttpClient reject_identity_client;
    reject_identity_client.setTimeout(3);
    HttpCompressionOptions reject_identity;
    reject_identity.enabled = true;
    reject_identity.advertise_accept_encoding = true;
    reject_identity.decompress_response = true;
    reject_identity.enabled_encodings = HTTP_CONTENT_ENCODING_GZIP_MASK;
    reject_identity.preferred_encoding = HTTP_CONTENT_ENCODING_GZIP;
    reject_identity.min_length = 1;
    reject_identity.max_decoded_size = 8u << 20;
    reject_identity_client.setCompression(reject_identity);

    HttpRequest reject_req;
    HttpResponse reject_resp;
    reject_req.method = HTTP_GET;
    reject_req.url = hv::asprintf("http://127.0.0.1:%d/echo-accept", port);
    ret = reject_identity_client.send(&reject_req, &reject_resp);
    if (!Check(ret == 0, "identity rejection Accept-Encoding request failed")) return false;
    if (!Check(reject_resp.status_code == HTTP_STATUS_OK, "identity rejection Accept-Encoding status not OK")) return false;
    if (!Check(reject_resp.body.find("gzip") != std::string::npos, "identity rejection Accept-Encoding should advertise gzip")) return false;
    if (!Check(reject_resp.body.find("zstd") == std::string::npos, "identity rejection Accept-Encoding should not advertise zstd")) return false;
    if (!Check(reject_resp.body.find("identity;q=0") != std::string::npos, "identity rejection Accept-Encoding should reject identity")) return false;
    return true;
}

static bool TestClientRejectsIdentityResponseWhenDisabled() {
    http_content_encoding encoding = PreferredAvailableEncoding();
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (!Check(listenfd >= 0, "failed to create raw HTTP test socket")) return false;

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (!Check(bind(listenfd, (sockaddr*)&addr, sizeof(addr)) == 0, "failed to bind raw HTTP test socket")) {
        close(listenfd);
        return false;
    }
    if (!Check(listen(listenfd, 1) == 0, "failed to listen on raw HTTP test socket")) {
        close(listenfd);
        return false;
    }

    socklen_t addrlen = sizeof(addr);
    if (!Check(getsockname(listenfd, (sockaddr*)&addr, &addrlen) == 0, "failed to query raw HTTP test socket")) {
        close(listenfd);
        return false;
    }
    int port = ntohs(addr.sin_port);

    std::string captured_request;
    std::thread server([listenfd, &captured_request]() {
        int connfd = accept(listenfd, NULL, NULL);
        if (connfd >= 0) {
            char buffer[4096];
            ssize_t nread = recv(connfd, buffer, sizeof(buffer), 0);
            if (nread > 0) {
                captured_request.assign(buffer, (size_t)nread);
            }
            std::string response =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Length: 5\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "plain";
            SendAll(connfd, response);
            close(connfd);
        }
        close(listenfd);
    });

    HttpClient client;
    client.setTimeout(3);
    HttpCompressionOptions options;
    options.enabled = true;
    options.decompress_response = true;
    options.advertise_accept_encoding = true;
    if (encoding == HTTP_CONTENT_ENCODING_IDENTITY) {
        options.enabled_encodings = 0;
    } else {
        options.enabled_encodings = 1u << encoding;
    }
    options.preferred_encoding = encoding;
    options.min_length = 1;
    options.max_decoded_size = 8u << 20;
    client.setCompression(options);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/raw-identity", port);

    int ret = client.send(&req, &resp);
    server.join();

    if (!Check(captured_request.find("Accept-Encoding:") != std::string::npos, "raw HTTP request should include Accept-Encoding header")) return false;
    if (!Check(captured_request.find("identity;q=0") != std::string::npos, "raw HTTP request should reject identity")) return false;
    if (encoding != HTTP_CONTENT_ENCODING_IDENTITY) {
        const char* name = http_content_encoding_str(encoding);
        if (!Check(name != NULL && captured_request.find(name) != std::string::npos, "raw HTTP request should advertise available content coding")) return false;
    }
    if (!Check(ret == ERR_UNSUPPORTED_CONTENT_ENCODING, "client should reject identity response when identity is disabled")) return false;
    return true;
}

static bool TestUnsupportedMediaTypeAdvertisesRequestEncodingsWhenForced() {
    http_content_encoding encoding = PreferredAvailableEncoding();
    if (encoding == HTTP_CONTENT_ENCODING_IDENTITY) {
        return true;
    }

    HttpService service;
    service.compression.enabled = true;
    service.compression.decompress_request = true;
    service.compression.compress_response = false;
    service.compression.advertise_accept_encoding = false;
    service.compression.enabled_encodings = 1u << encoding;
    service.compression.preferred_encoding = encoding;
    service.compression.min_length = 1;
    service.GET("/health", [](HttpRequest*, HttpResponse* resp) {
        return resp->String("ok");
    });
    service.POST("/echo", [](HttpRequest* req, HttpResponse* resp) {
        return resp->String(req->body);
    });

    HttpServer server(&service);
    int port = 23000 + (hv_getpid() % 1000);
    server.setPort(port);
    if (!Check(server.start() == 0, "failed to start unsupported-media-type test server")) return false;
    if (!Check(WaitHttpReady(port), "unsupported-media-type test server not ready")) {
        server.stop();
        return false;
    }

    HttpClient client;
    client.setTimeout(3);
    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_POST;
    req.url = hv::asprintf("http://127.0.0.1:%d/echo", port);
    req.body = "plain-body";

    int ret = client.send(&req, &resp);
    server.stop();

    if (!Check(ret == 0, "identity request to forced 415 server failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE, "identity request should return 415 when identity is disabled")) return false;
    std::string advertised = resp.GetHeader("Accept-Encoding");
    const char* name = http_content_encoding_str(encoding);
    if (!Check(name != NULL && advertised.find(name) != std::string::npos, "415 response should advertise supported request content coding")) return false;
    if (!Check(advertised.find("identity;q=0") != std::string::npos, "415 response should advertise identity rejection")) return false;
    return true;
}

static bool TestSelectContentEncodingLogic() {
    unsigned mask = http_content_encoding_supported_mask();
    if (!Check(SelectContentEncoding("*;q=0", mask, HTTP_CONTENT_ENCODING_ZSTD) == HTTP_CONTENT_ENCODING_UNKNOWN,
               "wildcard rejection should make all encodings unacceptable")) return false;
    if (!Check(SelectContentEncoding("", mask, HTTP_CONTENT_ENCODING_ZSTD) == HTTP_CONTENT_ENCODING_IDENTITY,
               "explicit empty Accept-Encoding should only allow identity")) return false;
    if (!Check(SelectContentEncoding("gzip;q=0.8, *;q=0.1", mask, HTTP_CONTENT_ENCODING_ZSTD) == HTTP_CONTENT_ENCODING_IDENTITY,
               "wildcard should not reduce identity below its implicit acceptability")) return false;
    if (!Check(SelectContentEncoding("gzip;q=0, zstd;q=0, identity;q=0", mask, HTTP_CONTENT_ENCODING_ZSTD) == HTTP_CONTENT_ENCODING_UNKNOWN,
               "explicit identity rejection should not fall back to identity")) return false;
    http_content_encoding higher_q_encoding = HasEncoding(HTTP_CONTENT_ENCODING_ZSTD) ?
            HTTP_CONTENT_ENCODING_ZSTD : HTTP_CONTENT_ENCODING_GZIP;
    std::string higher_q_header = hv::asprintf("%s;q=1.0, identity;q=0.1",
            http_content_encoding_str(higher_q_encoding));
    if (!Check(SelectContentEncoding(higher_q_header, mask, HTTP_CONTENT_ENCODING_IDENTITY) == higher_q_encoding,
               "higher q value should win over preferred encoding")) return false;
    if (HasEncoding(HTTP_CONTENT_ENCODING_ZSTD) && HasEncoding(HTTP_CONTENT_ENCODING_GZIP)) {
        if (!Check(SelectContentEncoding("gzip;q=0.4, zstd;q=1.0, identity;q=0",
                                         mask,
                                         HTTP_CONTENT_ENCODING_GZIP) == HTTP_CONTENT_ENCODING_ZSTD,
                   "higher q value should override preferred compressed encoding")) return false;
    }
    return true;
}

static bool TestRequestContentEncodingChain(int port) {
    HttpClient client;
    client.setTimeout(3);

    const std::string payload = "chain-request-payload";
    std::string compressed;
    std::vector<http_content_encoding> encodings;
    encodings.push_back(HTTP_CONTENT_ENCODING_GZIP);
    encodings.push_back(HTTP_CONTENT_ENCODING_ZSTD);
    if (!Check(CompressSequence(payload, encodings, &compressed), "failed to build chained request payload")) return false;

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_POST;
    req.url = hv::asprintf("http://127.0.0.1:%d/echo", port);
    req.headers["Content-Encoding"] = "gzip, zstd";
    req.body = compressed;

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "chained Content-Encoding request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_OK, "chained Content-Encoding request status not OK")) return false;
    if (!Check(resp.body == payload, "server did not decode chained Content-Encoding request body")) return false;
    return true;
}

static bool TestAutoDecodeChainedResponse(int port, const std::string& payload) {
    HttpClient client;
    client.setTimeout(3);

    HttpCompressionOptions options = MakeOptions(http_content_encoding_supported_mask(), HTTP_CONTENT_ENCODING_ZSTD);
    options.decompress_response = true;
    client.setCompression(options);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/double-encoded-response", port);

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "auto decode chained response request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_OK, "auto decode chained response status not OK")) return false;
    if (!Check(resp.body == payload, "auto decoded chained response payload mismatch")) return false;
    if (!Check(resp.GetHeader("Content-Encoding").empty(), "auto decoded chained response should not expose Content-Encoding")) return false;
    return true;
}

static bool TestAutoDecodeMultiMemberGzipResponse(int port) {
    HttpClient client;
    client.setTimeout(3);

    HttpCompressionOptions options = MakeOptions(http_content_encoding_supported_mask(), HTTP_CONTENT_ENCODING_ZSTD);
    options.decompress_response = true;
    client.setCompression(options);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/multi-member-gzip-response", port);

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "auto decode multi-member gzip response request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_OK, "auto decode multi-member gzip response status not OK")) return false;
    if (!Check(resp.body == "member-onemember-two", "auto decoded multi-member gzip response payload mismatch")) return false;
    if (!Check(resp.GetHeader("Content-Encoding").empty(), "auto decoded multi-member gzip response should not expose Content-Encoding")) return false;
    return true;
}

static bool TestIdentityResponseVary(int port, const std::string& payload) {
    HttpClient client;
    client.setTimeout(3);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/payload", port);
    req.headers["Accept-Encoding"] = "br";

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "identity response request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_OK, "identity response status not OK")) return false;
    if (!Check(resp.body == payload, "identity response payload mismatch")) return false;
    if (!Check(resp.GetHeader("Content-Encoding").empty(), "identity response should not expose Content-Encoding")) return false;
    if (!Check(resp.GetHeader("Vary").find("Accept-Encoding") != std::string::npos, "identity response missing Vary header")) return false;
    return true;
}

static bool TestUnacceptableAcceptEncoding(int port) {
    HttpClient client;
    client.setTimeout(3);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/payload", port);
    req.headers["Accept-Encoding"] = "gzip;q=0, zstd;q=0, identity;q=0";

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "unacceptable encoding request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_NOT_ACCEPTABLE, "unacceptable encoding should return 406")) return false;
    if (!Check(resp.body.empty(), "406 response should not carry compressed payload body")) return false;
    if (!Check(resp.GetHeader("Vary").find("Accept-Encoding") != std::string::npos, "406 response missing Vary header")) return false;
    return true;
}

static bool TestUnacceptableAcceptEncodingForIdentityOnlyResponse(int port) {
    HttpClient client;
    client.setTimeout(3);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/small-payload", port);
    req.headers["Accept-Encoding"] = "gzip;q=0, zstd;q=0, identity;q=0";

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "identity-only response request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_NOT_ACCEPTABLE, "identity-only response should return 406")) return false;
    if (!Check(resp.body.empty(), "identity-only 406 response should not carry payload body")) return false;
    if (!Check(resp.GetHeader("Vary").find("Accept-Encoding") != std::string::npos, "identity-only 406 response missing Vary header")) return false;
    return true;
}

static bool TestNoBodyResponseCompressionLogic() {
    HttpCompressionOptions options = MakeOptions(http_content_encoding_supported_mask(), PreferredAvailableEncoding());
    options.compress_response = true;
    options.enabled_encodings = http_content_encoding_supported_mask();

    HttpRequest reset_req;
    HttpResponse reset_resp;
    reset_req.method = HTTP_GET;
    reset_req.headers["Accept-Encoding"] = "identity;q=0";
    reset_resp.String("reset");
    reset_resp.status_code = HTTP_STATUS_RESET_CONTENT;
    if (!Check(!ShouldCompressResponse(&reset_req, &reset_resp, options, false), "205 response should not be compressed")) return false;
    if (!Check(!ShouldRejectIdentityResponse(&reset_req, &reset_resp), "205 response should not reject identity")) return false;
    http_content_encoding applied = HTTP_CONTENT_ENCODING_UNKNOWN;
    if (!Check(ApplyResponseCompression(&reset_req, &reset_resp, options, false, &applied) == 0, "205 response compression helper should succeed")) return false;
    if (!Check(applied == HTTP_CONTENT_ENCODING_IDENTITY, "205 response should remain identity")) return false;

    HttpRequest connect_req;
    HttpResponse connect_resp;
    connect_req.method = HTTP_CONNECT;
    connect_req.headers["Accept-Encoding"] = "identity;q=0";
    connect_resp.String("tunnel");
    connect_resp.status_code = HTTP_STATUS_OK;
    if (!Check(!ShouldCompressResponse(&connect_req, &connect_resp, options, false), "2xx CONNECT response should not be compressed")) return false;
    if (!Check(!ShouldRejectIdentityResponse(&connect_req, &connect_resp), "2xx CONNECT response should not reject identity")) return false;
    applied = HTTP_CONTENT_ENCODING_UNKNOWN;
    if (!Check(ApplyResponseCompression(&connect_req, &connect_resp, options, false, &applied) == 0, "CONNECT response compression helper should succeed")) return false;
    if (!Check(applied == HTTP_CONTENT_ENCODING_IDENTITY, "2xx CONNECT response should remain identity")) return false;
    return true;
}

static bool TestPartialContentPreservesEncodedBody(int port, const std::string& payload) {
    http_content_encoding encoding = PreferredAvailableEncoding();
    std::string compressed;
    if (!Check(CompressData(encoding, payload.data(), payload.size(), compressed) == 0, "failed to prepare expected partial response body")) return false;
    std::string expected_range = hv::asprintf("bytes 0-%zu/%zu", compressed.size() - 1, compressed.size());

    HttpClient client;
    client.setTimeout(3);

    HttpCompressionOptions options = MakeOptions(http_content_encoding_supported_mask(), encoding);
    options.decompress_response = true;
    options.advertise_accept_encoding = true;
    client.setCompression(options);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/partial-encoded", port);

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "206 response request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_PARTIAL_CONTENT, "206 response status mismatch")) return false;
    if (!Check(resp.GetHeader("Content-Encoding") == http_content_encoding_str(encoding), "206 response should preserve Content-Encoding")) return false;
    if (!Check(resp.GetHeader("Content-Range") == expected_range, "206 response should preserve Content-Range")) return false;
    if (!Check(resp.body == compressed, "206 response body should remain encoded")) return false;
    return true;
}

static bool TestCorruptedCompressedResponse(int port) {
    HttpClient client;
    client.setTimeout(3);

    HttpCompressionOptions options = MakeOptions(http_content_encoding_supported_mask(), PreferredAvailableEncoding());
    options.advertise_accept_encoding = true;
    options.decompress_response = true;
    client.setCompression(options);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/corrupted-encoded-response", port);

    int ret = client.send(&req, &resp);
    if (!Check(ret == ERR_DECOMPRESS, "corrupted compressed response should fail decompression")) return false;
    return true;
}

static bool TestResponseDecodeSizeLimit(int port) {
    HttpClient client;
    client.setTimeout(3);

    HttpCompressionOptions options = MakeOptions(http_content_encoding_supported_mask(), PreferredAvailableEncoding());
    options.advertise_accept_encoding = true;
    options.decompress_response = true;
    options.max_decoded_size = 1024;
    client.setCompression(options);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/oversized-encoded-response", port);

    int ret = client.send(&req, &resp);
    if (!Check(ret == ERR_OVER_LIMIT, "oversized decoded response should exceed max_decoded_size")) return false;
    return true;
}

static bool TestMalformedCompressedCorpus(http_content_encoding encoding) {
    char fill = 'z';
    if (encoding == HTTP_CONTENT_ENCODING_GZIP) {
        fill = 'g';
    }
    const std::string payload = MakePayload(fill, 4096);
    std::string compressed;
    if (!Check(CompressData(encoding, payload.data(), payload.size(), compressed) == 0,
               "failed to prepare malformed compressed corpus seed")) return false;

    for (unsigned i = 0; i < 128; ++i) {
        std::string mutated = MutatePayloadDeterministically(compressed, 0xC0FFEEu + i * 17u + (unsigned)encoding);
        HttpStreamingDecompressor decoder;
        if (!Check(decoder.Init(encoding, 8192) == 0, "failed to initialize decoder for malformed corpus")) return false;
        std::string out;
        int ret = decoder.Update(mutated.data(), mutated.size(), out);
        if (ret == 0) {
            std::string tail;
            ret = decoder.Finish(tail);
            if (ret == 0) {
                // Some mutated inputs can still decode successfully; the key
                // invariant is that decoded output stays bounded.
                out.append(tail);
                if (!Check(out.size() <= 8192, "malformed corpus decode should remain within configured limit")) return false;
            }
        }
    }
    return true;
}

static bool TestRepeatedCompressedRoundTrips(int port, http_content_encoding encoding) {
    HttpClient client;
    client.setTimeout(3);

    HttpCompressionOptions options = MakeOptions(1u << encoding, encoding);
    options.compress_request = true;
    options.decompress_response = true;
    options.advertise_accept_encoding = true;
    client.setCompression(options);

    for (int i = 0; i < 200; ++i) {
        HttpRequest req;
        HttpResponse resp;
        req.method = HTTP_POST;
        req.url = hv::asprintf("http://127.0.0.1:%d/echo", port);
        std::string expected = hv::asprintf("roundtrip-%02d:%s", i, MakePayload((char)('a' + (i % 26)), 1536).c_str());
        req.body = expected;

        int ret = client.send(&req, &resp);
        if (!Check(ret == 0, "repeated compressed round trip failed")) return false;
        if (!Check(resp.status_code == HTTP_STATUS_OK, "repeated compressed round trip status not OK")) return false;
        if (!Check(resp.body == expected, "repeated compressed round trip payload mismatch")) return false;
        if (!Check(resp.GetHeader("Content-Encoding").empty(), "repeated compressed round trip should auto decode response")) return false;
    }
    return true;
}

#ifdef WITH_ZSTD
static bool TestZstdHttpWindowLimit() {
    if (!HasEncoding(HTTP_CONTENT_ENCODING_ZSTD)) {
        return true;
    }
    std::string payload = MakePayload('Z', (9u << 20) + 4096);
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    if (!Check(cctx != NULL, "failed to allocate zstd context for window-limit test")) return false;
    std::string compressed;
    compressed.resize(ZSTD_compressBound(payload.size()));
    size_t ret = ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, 1);
    if (!ZSTD_isError(ret)) {
        ret = ZSTD_CCtx_setParameter(cctx, ZSTD_c_windowLog, 24);
    }
    if (!ZSTD_isError(ret)) {
        ret = ZSTD_compress2(cctx, (void*)compressed.data(), compressed.size(), payload.data(), payload.size());
    }
    ZSTD_freeCCtx(cctx);
    if (!Check(!ZSTD_isError(ret), "failed to build oversized-window zstd frame")) return false;
    compressed.resize(ret);

    HttpStreamingDecompressor decoder;
    if (!Check(decoder.Init(HTTP_CONTENT_ENCODING_ZSTD, 0) == 0, "failed to initialize zstd decoder for window-limit test")) return false;
    std::string out;
    if (!Check(decoder.Update(compressed.data(), compressed.size(), out) == ERR_DECOMPRESS,
               "oversized zstd window should be rejected by HTTP decoder")) return false;
    return true;
}
#endif

static bool TestHeadNoBodyAutoDecode(int port) {
    HttpClient client;
    client.setTimeout(3);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_HEAD;
    req.url = hv::asprintf("http://127.0.0.1:%d/head-encoding", port);

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "HEAD response with Content-Encoding should not fail auto decode")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_OK, "HEAD response status not OK")) return false;
    if (!Check(resp.body.empty(), "HEAD response should not contain body")) return false;
    if (!Check(resp.GetHeader("Content-Encoding") == "gzip", "HEAD response should preserve original Content-Encoding")) return false;
    return true;
}

static bool TestNoBodyUnsupportedContentEncoding(int port, http_method method, const char* path, http_status expected_status) {
    HttpClient client;
    client.setTimeout(3);

    HttpRequest req;
    HttpResponse resp;
    req.method = method;
    req.url = hv::asprintf("http://127.0.0.1:%d/%s", port, path);

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "no-body response with unsupported Content-Encoding should not fail")) return false;
    if (!Check(resp.status_code == expected_status, "no-body response status mismatch")) return false;
    if (!Check(resp.body.empty(), "no-body response should not contain body")) return false;
    if (!Check(resp.GetHeader("Content-Encoding") == "br", "no-body response should preserve unsupported Content-Encoding header")) return false;
    return true;
}

static bool TestAdvertiseRequestEncodings(int port) {
    HttpClient client;
    client.setTimeout(3);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/health", port);

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "request encoding advertisement request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_OK, "request encoding advertisement status not OK")) return false;
    if (HasEncoding(HTTP_CONTENT_ENCODING_GZIP)) {
        if (!Check(resp.GetHeader("Accept-Encoding").find("gzip") != std::string::npos, "response should advertise gzip request encoding")) return false;
    } else {
        if (!Check(resp.GetHeader("Accept-Encoding").find("gzip") == std::string::npos, "response should not advertise unavailable gzip request encoding")) return false;
    }
    if (HasEncoding(HTTP_CONTENT_ENCODING_ZSTD)) {
        if (!Check(resp.GetHeader("Accept-Encoding").find("zstd") != std::string::npos, "response should advertise zstd request encoding")) return false;
    } else {
        if (!Check(resp.GetHeader("Accept-Encoding").find("zstd") == std::string::npos, "response should not advertise unavailable zstd request encoding")) return false;
    }
    return true;
}

static bool TestStaticCompressedConditionalMetadata(int port, const std::string& path) {
    HttpCompressionOptions gzip_options = MakeOptions(HTTP_CONTENT_ENCODING_GZIP_MASK, HTTP_CONTENT_ENCODING_GZIP);
    gzip_options.advertise_accept_encoding = true;

    std::string filename = hv_basename(path.c_str());
    std::string url = hv::asprintf("http://127.0.0.1:%d/static/%s", port, filename.c_str());

    HttpClient compressed_client;
    compressed_client.setTimeout(3);
    compressed_client.setCompression(gzip_options);

    HttpRequest get_req;
    HttpResponse get_resp;
    get_req.method = HTTP_GET;
    get_req.url = url;
    int ret = compressed_client.send(&get_req, &get_resp);
    if (!Check(ret == 0, "compressed static GET request failed")) return false;
    if (!Check(get_resp.status_code == HTTP_STATUS_OK, "compressed static GET status not OK")) return false;
    if (!Check(get_resp.GetHeader("Content-Encoding") == "gzip", "compressed static GET should use gzip")) return false;
    if (!Check(!get_resp.GetHeader("Etag").empty(), "compressed static GET missing ETag")) return false;

    HttpClient identity_client;
    identity_client.setTimeout(3);
    HttpRequest identity_req;
    HttpResponse identity_resp;
    identity_req.method = HTTP_GET;
    identity_req.url = url;
    identity_req.headers["Accept-Encoding"] = "identity";
    ret = identity_client.send(&identity_req, &identity_resp);
    if (!Check(ret == 0, "identity static GET request failed")) return false;
    if (!Check(identity_resp.status_code == HTTP_STATUS_OK, "identity static GET status not OK")) return false;
    if (!Check(identity_resp.GetHeader("Content-Encoding").empty(), "identity static GET should not use Content-Encoding")) return false;
    if (!Check(identity_resp.GetHeader("Etag") != get_resp.GetHeader("Etag"), "compressed and identity static GET should use different ETags")) return false;

    HttpRequest head_req;
    HttpResponse head_resp;
    head_req.method = HTTP_HEAD;
    head_req.url = url;
    head_req.SetCompression(gzip_options);
    ret = compressed_client.send(&head_req, &head_resp);
    if (!Check(ret == 0, "compressed static HEAD request failed")) return false;
    if (!Check(head_resp.status_code == HTTP_STATUS_OK, "compressed static HEAD status not OK")) return false;
    if (!Check(head_resp.body.empty(), "compressed static HEAD should not contain body")) return false;
    if (!Check(head_resp.GetHeader("Content-Encoding") == "gzip", "compressed static HEAD should preserve gzip encoding")) return false;
    if (!Check(head_resp.GetHeader("Vary").find("Accept-Encoding") != std::string::npos, "compressed static HEAD missing Vary header")) return false;
    if (!Check(head_resp.GetHeader("Etag") == get_resp.GetHeader("Etag"), "compressed static HEAD should reuse compressed ETag")) return false;
    if (!Check(atoi(head_resp.GetHeader("Content-Length").c_str()) == (int)get_resp.body.size(), "compressed static HEAD Content-Length should match compressed GET payload")) return false;

    HttpRequest wrong_conditional_req;
    HttpResponse wrong_conditional_resp;
    wrong_conditional_req.method = HTTP_GET;
    wrong_conditional_req.url = url;
    wrong_conditional_req.SetCompression(gzip_options);
    wrong_conditional_req.headers["If-None-Match"] = identity_resp.GetHeader("Etag");
    ret = compressed_client.send(&wrong_conditional_req, &wrong_conditional_resp);
    if (!Check(ret == 0, "compressed static conditional GET with identity ETag failed")) return false;
    if (!Check(wrong_conditional_resp.status_code == HTTP_STATUS_OK, "compressed static conditional GET should not match identity ETag")) return false;

    HttpRequest conditional_req;
    HttpResponse conditional_resp;
    conditional_req.method = HTTP_GET;
    conditional_req.url = url;
    conditional_req.SetCompression(gzip_options);
    conditional_req.headers["If-None-Match"] = get_resp.GetHeader("Etag");
    ret = compressed_client.send(&conditional_req, &conditional_resp);
    if (!Check(ret == 0, "compressed static conditional GET failed")) return false;
    if (!Check(conditional_resp.status_code == HTTP_STATUS_NOT_MODIFIED, "compressed static conditional GET should return 304")) return false;
    if (!Check(conditional_resp.body.empty(), "compressed static conditional 304 should not contain body")) return false;
    if (!Check(conditional_resp.GetHeader("Etag") == get_resp.GetHeader("Etag"), "compressed static conditional 304 ETag mismatch")) return false;
    if (!Check(conditional_resp.GetHeader("Content-Encoding") == "gzip", "compressed static conditional 304 should preserve gzip encoding")) return false;
    return true;
}

static bool TestAsyncClientCompression(int port) {
    AsyncHttpClient client;
    std::atomic<bool> done(false);
    HttpResponsePtr async_resp;

    HttpRequestPtr req = std::make_shared<HttpRequest>();
    req->method = HTTP_GET;
    req->url = hv::asprintf("http://127.0.0.1:%d/echo-accept", port);
    HttpCompressionOptions options = MakeOptions(HTTP_CONTENT_ENCODING_GZIP_MASK | HTTP_CONTENT_ENCODING_ZSTD_MASK, HTTP_CONTENT_ENCODING_ZSTD);
    options.advertise_accept_encoding = true;
    options.decompress_response = true;
    req->SetCompression(options);
    int ret = client.send(req, [&done, &async_resp](const HttpResponsePtr& resp) {
        async_resp = resp;
        done = true;
    });
    if (!Check(ret == 0, "async Accept-Encoding request submission failed")) return false;
    for (int i = 0; i < 200 && !done.load(); ++i) {
        hv_delay(10);
    }
    if (!Check(done.load(), "async Accept-Encoding request did not complete")) return false;
    if (!Check(async_resp != NULL, "async Accept-Encoding response missing")) return false;
    const char* expected = "gzip";
    if (HasEncoding(HTTP_CONTENT_ENCODING_ZSTD)) {
        expected = "zstd";
    }
    if (!Check(async_resp->body.find(expected) != std::string::npos, "async client should advertise preferred available encoding")) return false;

    done = false;
    async_resp.reset();
    HttpRequestPtr post_req = std::make_shared<HttpRequest>();
    post_req->method = HTTP_POST;
    post_req->url = hv::asprintf("http://127.0.0.1:%d/echo", port);
    post_req->body = MakePayload('A', 1024);
    http_content_encoding post_encoding = PreferredAvailableEncoding();
    HttpCompressionOptions post_options = MakeOptions(1u << post_encoding, post_encoding);
    post_options.compress_request = true;
    post_req->SetCompression(post_options);
    ret = client.send(post_req, [&done, &async_resp](const HttpResponsePtr& resp) {
        async_resp = resp;
        done = true;
    });
    if (!Check(ret == 0, "async compressed request submission failed")) return false;
    for (int i = 0; i < 200 && !done.load(); ++i) {
        hv_delay(10);
    }
    if (!Check(done.load(), "async compressed request did not complete")) return false;
    if (!Check(async_resp != NULL, "async compressed response missing")) return false;
    if (!Check(async_resp->status_code == HTTP_STATUS_OK, "async compressed request status not OK")) return false;
    if (!Check(async_resp->body == MakePayload('A', 1024), "async compressed request payload mismatch")) return false;
    return true;
}

static bool TestZeroEnabledEncodingsIdentityFallback(int port, const std::string& payload) {
    HttpClient client;
    client.setTimeout(3);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/mask-zero", port);
    req.headers["Accept-Encoding"] = "br";

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "mask-zero identity fallback request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_OK, "mask-zero identity fallback status not OK")) return false;
    if (!Check(resp.body == payload, "mask-zero identity fallback payload mismatch")) return false;
    if (!Check(resp.GetHeader("Content-Encoding").empty(), "mask-zero identity fallback should not expose Content-Encoding")) return false;
    if (!Check(resp.GetHeader("Vary").find("Accept-Encoding") != std::string::npos, "mask-zero identity fallback missing Vary header")) return false;
    return true;
}

static bool TestZeroEnabledEncodingsRejectIdentity(int port) {
    HttpClient client;
    client.setTimeout(3);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/mask-zero", port);
    req.headers["Accept-Encoding"] = "gzip;q=0, zstd;q=0, identity;q=0";

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "mask-zero unacceptable request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_NOT_ACCEPTABLE, "mask-zero identity rejection should return 406")) return false;
    if (!Check(resp.body.empty(), "mask-zero 406 response should not carry body")) return false;
    if (!Check(resp.GetHeader("Vary").find("Accept-Encoding") != std::string::npos, "mask-zero 406 response missing Vary header")) return false;
    return true;
}

static bool TestChunkedUnacceptableAcceptEncoding(int port) {
    HttpClient client;
    client.setTimeout(3);

    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/chunked", port);
    req.headers["Accept-Encoding"] = "gzip;q=0, zstd;q=0, identity;q=0";

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "chunked unacceptable request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_NOT_ACCEPTABLE, "chunked unacceptable request should return 406")) return false;
    if (!Check(resp.body.empty(), "chunked 406 response should not carry body")) return false;
    if (!Check(resp.GetHeader("Vary").find("Accept-Encoding") != std::string::npos, "chunked 406 response missing Vary header")) return false;
    return true;
}

static bool TestLargeFileUnacceptableAcceptEncoding(int port, const std::string& path) {
    HttpClient client;
    client.setTimeout(3);

    std::string filename = hv_basename(path.c_str());
    HttpRequest req;
    HttpResponse resp;
    req.method = HTTP_GET;
    req.url = hv::asprintf("http://127.0.0.1:%d/static/%s", port, filename.c_str());
    req.headers["Accept-Encoding"] = "gzip;q=0, zstd;q=0, identity;q=0";

    int ret = client.send(&req, &resp);
    if (!Check(ret == 0, "large-file unacceptable request failed")) return false;
    if (!Check(resp.status_code == HTTP_STATUS_NOT_ACCEPTABLE, "large-file unacceptable request should return 406")) return false;
    if (!Check(resp.body.empty(), "large-file 406 response should not carry body")) return false;
    if (!Check(resp.GetHeader("Vary").find("Accept-Encoding") != std::string::npos, "large-file 406 response missing Vary header")) return false;
    return true;
}

static bool TestStaticFileUnacceptableAcceptEncodingRaw(int port, const std::string& path) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (!Check(fd >= 0, "failed to create raw static-file test socket")) return false;

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (!Check(connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0, "failed to connect raw static-file test socket")) {
        close(fd);
        return false;
    }

    std::string filename = hv_basename(path.c_str());
    std::string request = hv::asprintf(
            "GET /static/%s HTTP/1.1\r\n"
            "Host: 127.0.0.1:%d\r\n"
            "Accept-Encoding: gzip;q=0, zstd;q=0, identity;q=0\r\n"
            "Connection: close\r\n"
            "\r\n",
            filename.c_str(),
            port);
    if (!Check(SendAll(fd, request), "failed to send raw static-file request")) {
        close(fd);
        return false;
    }

    std::string response;
    bool ok = RecvAll(fd, &response);
    close(fd);
    if (!Check(ok, "failed to read raw static-file response")) return false;

    size_t header_end = response.find("\r\n\r\n");
    if (!Check(header_end != std::string::npos, "raw static-file response missing header terminator")) return false;

    std::string headers = response.substr(0, header_end + 4);
    std::string body = response.substr(header_end + 4);
    if (!Check(headers.find("406 Not Acceptable") != std::string::npos, "raw static-file response should return 406")) return false;
    if (!Check(headers.find("Content-Length: 0") != std::string::npos, "raw static-file 406 response should declare zero length")) return false;
    if (!Check(body.empty(), "raw static-file 406 response should not include cached file payload")) return false;
    if (!Check(headers.find("Vary: Accept-Encoding") != std::string::npos, "raw static-file 406 response missing Vary header")) return false;
    return true;
}

} // namespace

int main() {
#if !defined(WITH_ZLIB) && !defined(WITH_ZSTD)
    printf("http_compression_test skipped: requires WITH_ZLIB or WITH_ZSTD.\n");
    return 0;
#else
    const std::string payload = MakePayload('H', 4096);
    const std::string large_file_path = hv::asprintf("/tmp/libhv-http-compression-%d.bin", hv_getpid());
    const std::string static_file_path = hv::asprintf("/tmp/libhv-http-compression-static-%d.txt", hv_getpid());
    FILE* large_file = fopen(large_file_path.c_str(), "wb");
    if (!Check(large_file != NULL, "failed to create large-file test fixture")) {
        return 1;
    }
    std::string large_payload = MakePayload('L', 2048);
    size_t nwritten = fwrite(large_payload.data(), 1, large_payload.size(), large_file);
    fclose(large_file);
    if (!Check(nwritten == large_payload.size(), "failed to write large-file test fixture")) {
        remove(large_file_path.c_str());
        return 1;
    }
    FILE* static_file = fopen(static_file_path.c_str(), "wb");
    if (!Check(static_file != NULL, "failed to create static-file test fixture")) {
        remove(large_file_path.c_str());
        return 1;
    }
    const std::string static_payload = MakePayload('S', 48);
    nwritten = fwrite(static_payload.data(), 1, static_payload.size(), static_file);
    fclose(static_file);
    if (!Check(nwritten == static_payload.size(), "failed to write static-file test fixture")) {
        remove(large_file_path.c_str());
        remove(static_file_path.c_str());
        return 1;
    }

    HttpService service;
    service.max_file_cache_size = 64;
    service.compression.enabled = true;
    service.compression.compress_response = true;
    service.compression.decompress_request = true;
    service.compression.advertise_accept_encoding = true;
    service.compression.enabled_encodings = http_content_encoding_supported_mask();
    service.compression.preferred_encoding = PreferredAvailableEncoding();
    service.compression.min_length = 1;
    service.GET("/health", [](HttpRequest*, HttpResponse* resp) {
        return resp->String("ok");
    });
    service.GET("/payload", [&payload](HttpRequest*, HttpResponse* resp) {
        return resp->String(payload);
    });
    service.GET("/double-encoded-response", [&payload](HttpRequest*, HttpResponse* resp) {
        std::vector<http_content_encoding> encodings;
        encodings.push_back(HTTP_CONTENT_ENCODING_GZIP);
        encodings.push_back(HTTP_CONTENT_ENCODING_ZSTD);
        std::string compressed;
        if (!CompressSequence(payload, encodings, &compressed)) {
            return HTTP_STATUS_INTERNAL_SERVER_ERROR;
        }
        resp->body = compressed;
        resp->content = NULL;
        resp->content_length = resp->body.size();
        resp->headers["Content-Type"] = "text/plain";
        resp->headers["Content-Encoding"] = "gzip, zstd";
        resp->headers["Content-Length"] = hv::to_string(resp->body.size());
        return HTTP_STATUS_OK;
    });
    service.GET("/multi-member-gzip-response", [](HttpRequest*, HttpResponse* resp) {
        std::string member_one;
        std::string member_two;
        if (CompressData(HTTP_CONTENT_ENCODING_GZIP, "member-one", strlen("member-one"), member_one) != 0 ||
            CompressData(HTTP_CONTENT_ENCODING_GZIP, "member-two", strlen("member-two"), member_two) != 0) {
            return HTTP_STATUS_INTERNAL_SERVER_ERROR;
        }
        resp->body = member_one + member_two;
        resp->content = NULL;
        resp->content_length = resp->body.size();
        resp->headers["Content-Type"] = "text/plain";
        resp->headers["Content-Encoding"] = "gzip";
        resp->headers["Content-Length"] = hv::to_string(resp->body.size());
        return HTTP_STATUS_OK;
    });
    service.GET("/mask-zero", [&payload](HttpRequest*, HttpResponse* resp) {
        HttpCompressionOptions options = MakeOptions(0, HTTP_CONTENT_ENCODING_UNKNOWN);
        options.compress_response = true;
        options.enabled_encodings = 0;
        resp->SetCompression(options);
        return resp->String(payload);
    });
    service.GET("/small-payload", [](HttpRequest*, HttpResponse* resp) {
        HttpCompressionOptions options = resp->compression;
        options.enabled = true;
        options.compress_response = true;
        options.enabled_encodings = http_content_encoding_supported_mask();
        options.preferred_encoding = HTTP_CONTENT_ENCODING_ZSTD;
        options.min_length = 1024;
        resp->SetCompression(options);
        return resp->String("tiny");
    });
    service.POST("/echo", [](HttpRequest* req, HttpResponse* resp) {
        return resp->String(req->body);
    });
    service.POST("/state-echo", http_state_handler([](const HttpContextPtr& ctx, http_parser_state state, const char*, size_t) {
        if (state == HP_MESSAGE_COMPLETE) {
            const std::string content_encoding = ctx->header("Content-Encoding", "<missing>");
            const std::string content_length = ctx->header("Content-Length", "<missing>");
            return ctx->sendString(content_encoding + "|" + content_length + "|" + ctx->body());
        }
        return HTTP_STATUS_NEXT;
    }));
    service.GET("/echo-accept", [](HttpRequest* req, HttpResponse* resp) {
        return resp->String(req->GetHeader("Accept-Encoding"));
    });
    service.GET("/redirect-accept", [](HttpRequest*, HttpResponse* resp) {
        return resp->Redirect("/echo-accept");
    });
    service.POST("/echo-content-encoding", [](HttpRequest* req, HttpResponse* resp) {
        return resp->String(req->GetHeader("Content-Encoding") + "|" + req->body);
    });
    service.HEAD("/head-encoding", [](HttpRequest*, HttpResponse* resp) {
        resp->status_code = HTTP_STATUS_OK;
        resp->SetContentEncoding(HTTP_CONTENT_ENCODING_GZIP);
        resp->headers["Content-Length"] = "0";
        return HTTP_STATUS_OK;
    });
    service.HEAD("/head-unsupported-encoding", [](HttpRequest*, HttpResponse* resp) {
        resp->status_code = HTTP_STATUS_OK;
        resp->headers["Content-Encoding"] = "br";
        resp->headers["Content-Length"] = "0";
        return HTTP_STATUS_OK;
    });
    service.GET("/status-204-encoding", [](HttpRequest*, HttpResponse* resp) {
        resp->status_code = HTTP_STATUS_NO_CONTENT;
        resp->headers["Content-Encoding"] = "br";
        resp->headers["Content-Length"] = "0";
        return HTTP_STATUS_NO_CONTENT;
    });
    service.GET("/status-205-encoding", [](HttpRequest*, HttpResponse* resp) {
        resp->status_code = HTTP_STATUS_RESET_CONTENT;
        resp->headers["Content-Encoding"] = "br";
        resp->headers["Content-Length"] = "0";
        return HTTP_STATUS_RESET_CONTENT;
    });
    service.GET("/status-304-encoding", [](HttpRequest*, HttpResponse* resp) {
        resp->status_code = HTTP_STATUS_NOT_MODIFIED;
        resp->headers["Content-Encoding"] = "br";
        resp->headers["Content-Length"] = "0";
        return HTTP_STATUS_NOT_MODIFIED;
    });
    service.GET("/partial-encoded", [&payload](HttpRequest*, HttpResponse* resp) {
        http_content_encoding encoding = PreferredAvailableEncoding();
        std::string compressed;
        if (CompressData(encoding, payload.data(), payload.size(), compressed) != 0) {
            return HTTP_STATUS_INTERNAL_SERVER_ERROR;
        }
        resp->status_code = HTTP_STATUS_PARTIAL_CONTENT;
        resp->body = compressed;
        resp->content = NULL;
        resp->content_length = resp->body.size();
        resp->headers["Content-Type"] = "text/plain";
        resp->headers["Content-Encoding"] = http_content_encoding_str(encoding);
        resp->headers["Content-Range"] = hv::asprintf("bytes 0-%zu/%zu", compressed.size() - 1, compressed.size());
        resp->headers["Content-Length"] = hv::to_string(resp->body.size());
        return HTTP_STATUS_PARTIAL_CONTENT;
    });
    service.GET("/corrupted-encoded-response", [&payload](HttpRequest*, HttpResponse* resp) {
        http_content_encoding encoding = PreferredAvailableEncoding();
        std::string compressed;
        if (CompressData(encoding, payload.data(), payload.size(), compressed) != 0 || compressed.size() < 2) {
            return HTTP_STATUS_INTERNAL_SERVER_ERROR;
        }
        compressed.resize(compressed.size() - 1);
        resp->status_code = HTTP_STATUS_OK;
        resp->body = compressed;
        resp->content = NULL;
        resp->content_length = resp->body.size();
        resp->headers["Content-Type"] = "text/plain";
        resp->headers["Content-Encoding"] = http_content_encoding_str(encoding);
        resp->headers["Content-Length"] = hv::to_string(resp->body.size());
        return HTTP_STATUS_OK;
    });
    service.GET("/oversized-encoded-response", [](HttpRequest*, HttpResponse* resp) {
        http_content_encoding encoding = PreferredAvailableEncoding();
        std::string large_payload = MakePayload('O', 16384);
        std::string compressed;
        if (CompressData(encoding, large_payload.data(), large_payload.size(), compressed) != 0) {
            return HTTP_STATUS_INTERNAL_SERVER_ERROR;
        }
        resp->status_code = HTTP_STATUS_OK;
        resp->body = compressed;
        resp->content = NULL;
        resp->content_length = resp->body.size();
        resp->headers["Content-Type"] = "text/plain";
        resp->headers["Content-Encoding"] = http_content_encoding_str(encoding);
        resp->headers["Content-Length"] = hv::to_string(resp->body.size());
        return HTTP_STATUS_OK;
    });
    service.GET("/chunked", [](const HttpRequestPtr&, const HttpResponseWriterPtr& writer) {
        writer->Begin();
        writer->WriteHeader("Content-Type", "text/plain");
        writer->WriteChunked("chunk-one");
        writer->EndChunked();
    });
    service.Static("/static", "/tmp");

    HttpServer server(&service);
    int port = 21000 + (hv_getpid() % 1000);
    server.setPort(port);
    if (!Check(server.start() == 0, "failed to start HTTP test server")) {
        remove(large_file_path.c_str());
        remove(static_file_path.c_str());
        return 1;
    }
    if (!Check(WaitHttpReady(port), "HTTP test server not ready")) {
        server.stop();
        remove(large_file_path.c_str());
        remove(static_file_path.c_str());
        return 1;
    }

    bool ok = true;
    ok = ok && TestSelectContentEncodingLogic();
    ok = ok && TestPreferredAcceptEncodingAdvertisement(port);
    ok = ok && TestClientRejectsIdentityResponseWhenDisabled();
    ok = ok && TestUnsupportedMediaTypeAdvertisesRequestEncodingsWhenForced();
    ok = ok && TestNoBodyResponseCompressionLogic();
#ifdef WITH_ZSTD
    ok = ok && TestZstdHttpWindowLimit();
#endif
    if (HasEncoding(HTTP_CONTENT_ENCODING_GZIP)) {
        ok = ok && TestMalformedCompressedCorpus(HTTP_CONTENT_ENCODING_GZIP);
        ok = ok && TestRequestCompression(port, HTTP_CONTENT_ENCODING_GZIP, "gzip");
        ok = ok && TestStateHandlerDecodedRequestHeaders(port, HTTP_CONTENT_ENCODING_GZIP);
        ok = ok && TestRawResponseCompression(port, HTTP_CONTENT_ENCODING_GZIP, "gzip", payload);
        ok = ok && TestAutoDecodeMultiMemberGzipResponse(port);
        ok = ok && TestStaticCompressedConditionalMetadata(port, static_file_path);
    }
    if (HasEncoding(HTTP_CONTENT_ENCODING_ZSTD)) {
        ok = ok && TestMalformedCompressedCorpus(HTTP_CONTENT_ENCODING_ZSTD);
        ok = ok && TestRequestCompression(port, HTTP_CONTENT_ENCODING_ZSTD, "zstd");
        ok = ok && TestStateHandlerDecodedRequestHeaders(port, HTTP_CONTENT_ENCODING_ZSTD);
        ok = ok && TestRawResponseCompression(port, HTTP_CONTENT_ENCODING_ZSTD, "zstd", payload);
    }
    if (HasEncoding(HTTP_CONTENT_ENCODING_GZIP) && HasEncoding(HTTP_CONTENT_ENCODING_ZSTD)) {
        ok = ok && TestRequestContentEncodingChain(port);
        ok = ok && TestAutoDecodeChainedResponse(port, payload);
    }
    ok = ok && TestAutoDecodeResponse(port, payload);
    ok = ok && TestResponseCallbackSeesDecodedHeaders(port, payload);
    ok = ok && TestRedirectPreservesClientCompression(port);
    ok = ok && TestManualAcceptEncodingOverride(port);
    ok = ok && TestExplicitEmptyAcceptEncoding(port, payload);
    ok = ok && TestManualContentEncodingDisablesRequestCompression(port);
    ok = ok && TestIdentityResponseVary(port, payload);
    ok = ok && TestAdvertiseRequestEncodings(port);
    ok = ok && TestUnacceptableAcceptEncoding(port);
    ok = ok && TestUnacceptableAcceptEncodingForIdentityOnlyResponse(port);
    ok = ok && TestPartialContentPreservesEncodedBody(port, payload);
    ok = ok && TestCorruptedCompressedResponse(port);
    ok = ok && TestResponseDecodeSizeLimit(port);
    ok = ok && TestHeadNoBodyAutoDecode(port);
    ok = ok && TestNoBodyUnsupportedContentEncoding(port, HTTP_HEAD, "head-unsupported-encoding", HTTP_STATUS_OK);
    ok = ok && TestNoBodyUnsupportedContentEncoding(port, HTTP_GET, "status-204-encoding", HTTP_STATUS_NO_CONTENT);
    ok = ok && TestNoBodyUnsupportedContentEncoding(port, HTTP_GET, "status-205-encoding", HTTP_STATUS_RESET_CONTENT);
    ok = ok && TestNoBodyUnsupportedContentEncoding(port, HTTP_GET, "status-304-encoding", HTTP_STATUS_NOT_MODIFIED);
    ok = ok && TestZeroEnabledEncodingsIdentityFallback(port, payload);
    ok = ok && TestZeroEnabledEncodingsRejectIdentity(port);
    ok = ok && TestChunkedUnacceptableAcceptEncoding(port);
    ok = ok && TestLargeFileUnacceptableAcceptEncoding(port, large_file_path);
    ok = ok && TestStaticFileUnacceptableAcceptEncodingRaw(port, static_file_path);
    ok = ok && TestAsyncClientCompression(port);
    if (HasEncoding(HTTP_CONTENT_ENCODING_GZIP)) {
        ok = ok && TestRepeatedCompressedRoundTrips(port, HTTP_CONTENT_ENCODING_GZIP);
    }
    if (HasEncoding(HTTP_CONTENT_ENCODING_ZSTD)) {
        ok = ok && TestRepeatedCompressedRoundTrips(port, HTTP_CONTENT_ENCODING_ZSTD);
    }

    server.stop();
    hv_delay(50);
    remove(large_file_path.c_str());
    remove(static_file_path.c_str());
    if (!ok) {
        return 1;
    }

    printf("http_compression_test passed.\n");
    return 0;
#endif
}
