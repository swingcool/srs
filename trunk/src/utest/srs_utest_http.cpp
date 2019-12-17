/*
The MIT License (MIT)

Copyright (c) 2013-2019 Winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include <srs_utest_http.hpp>

#include <sstream>
using namespace std;

#include <srs_http_stack.hpp>
#include <srs_service_http_conn.hpp>
#include <srs_utest_protocol.hpp>
#include <srs_protocol_json.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_file.hpp>
#include <srs_utest_kernel.hpp>
#include <srs_app_http_static.hpp>

class MockResponseWriter : virtual public ISrsHttpResponseWriter, virtual public ISrsHttpHeaderFilter
{
public:
    SrsHttpResponseWriter* w;
    MockBufferIO io;
public:
    MockResponseWriter();
    virtual ~MockResponseWriter();
public:
    virtual srs_error_t final_request();
    virtual SrsHttpHeader* header();
    virtual srs_error_t write(char* data, int size);
    virtual srs_error_t writev(const iovec* iov, int iovcnt, ssize_t* pnwrite);
    virtual void write_header(int code);
public:
    virtual srs_error_t filter(SrsHttpHeader* h);
};

MockResponseWriter::MockResponseWriter()
{
    w = new SrsHttpResponseWriter(&io);
    w->hf = this;
}

MockResponseWriter::~MockResponseWriter()
{
    srs_freep(w);
}

srs_error_t MockResponseWriter::final_request()
{
    return w->final_request();
}

SrsHttpHeader* MockResponseWriter::header()
{
    return w->header();
}

srs_error_t MockResponseWriter::write(char* data, int size)
{
    return w->write(data, size);
}

srs_error_t MockResponseWriter::writev(const iovec* iov, int iovcnt, ssize_t* pnwrite)
{
    return w->writev(iov, iovcnt, pnwrite);
}

void MockResponseWriter::write_header(int code)
{
    w->write_header(code);
}

srs_error_t MockResponseWriter::filter(SrsHttpHeader* h)
{
    h->del("Content-Type");
    h->del("Server");
    h->del("Connection");
    h->del("Location");
    h->del("Content-Range");
    return srs_success;
}

string mock_http_response(int status, string content)
{
    stringstream ss;
    ss << "HTTP/1.1 " << status << " " << srs_generate_http_status_text(status) << "\r\n"
        << "Content-Length: " << content.length() << "\r\n"
        << "\r\n"
        << content;
    return ss.str();
}

string mock_http_response2(int status, string content)
{
    stringstream ss;
    ss << "HTTP/1.1 " << status << " " << srs_generate_http_status_text(status) << "\r\n"
        << "Transfer-Encoding: chunked" << "\r\n"
        << "\r\n"
        << content;
    return ss.str();
}

class MockFileReaderFactory : public ISrsFileReaderFactory
{
public:
    string bytes;
    MockFileReaderFactory(string data) {
        bytes = data;
    }
    virtual ~MockFileReaderFactory() {
    }
    virtual SrsFileReader* create_file_reader() {
        return new MockSrsFileReader((const char*)bytes.data(), (int)bytes.length());
    }
};

class MockHttpHandler : public ISrsHttpHandler
{
public:
    string bytes;
    MockHttpHandler(string data) {
        bytes = data;
    }
    virtual ~MockHttpHandler() {
    }
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* /*r*/) {
        return w->write((char*)bytes.data(), (int)bytes.length());
    }
};

bool _mock_srs_path_always_exists(std::string /*path*/)
{
    return true;
}

bool _mock_srs_path_not_exists(std::string /*path*/)
{
    return false;
}

#define __MOCK_HTTP_EXPECT_STREQ(status, text, w) \
        EXPECT_STREQ(mock_http_response(status, text).c_str(), HELPER_BUFFER2STR(&w.io.out_buffer).c_str())

#define __MOCK_HTTP_EXPECT_STREQ2(status, text, w) \
        EXPECT_STREQ(mock_http_response2(status, text).c_str(), HELPER_BUFFER2STR(&w.io.out_buffer).c_str())

VOID TEST(ProtocolHTTPTest, StatusCode2Text)
{
    EXPECT_STREQ(SRS_CONSTS_HTTP_OK_str, srs_generate_http_status_text(SRS_CONSTS_HTTP_OK).c_str());
    EXPECT_STREQ("Status Unknown", srs_generate_http_status_text(999).c_str());

    EXPECT_FALSE(srs_go_http_body_allowd(SRS_CONSTS_HTTP_Continue));
    EXPECT_FALSE(srs_go_http_body_allowd(SRS_CONSTS_HTTP_OK-1));
    EXPECT_FALSE(srs_go_http_body_allowd(SRS_CONSTS_HTTP_NoContent));
    EXPECT_FALSE(srs_go_http_body_allowd(SRS_CONSTS_HTTP_NotModified));
    EXPECT_TRUE(srs_go_http_body_allowd(SRS_CONSTS_HTTP_OK));
}

VOID TEST(ProtocolHTTPTest, ResponseDetect)
{
    EXPECT_STREQ("application/octet-stream", srs_go_http_detect(NULL, 0).c_str());
    EXPECT_STREQ("application/octet-stream", srs_go_http_detect((char*)"Hello, world!", 0).c_str());
}

VOID TEST(ProtocolHTTPTest, ResponseWriter)
{
    // If directly write string, response with content-length.
    if (true) {
        MockResponseWriter w;

        char msg[] = "Hello, world!";
        w.write((char*)msg, sizeof(msg) - 1);

        __MOCK_HTTP_EXPECT_STREQ(200, "Hello, world!", w);
    }

    // Response with specified length string, response with content-length.
    if (true) {
        MockResponseWriter w;

        char msg[] = "Hello, world!";

        w.header()->set_content_type("text/plain; charset=utf-8");
        w.header()->set_content_length(sizeof(msg) - 1);
        w.write_header(SRS_CONSTS_HTTP_OK);
        w.write((char*)msg, sizeof(msg) - 1);
        w.final_request();

        __MOCK_HTTP_EXPECT_STREQ(200, "Hello, world!", w);
    }

    // If set content-length to 0 then final_request, send an empty resonse with content-length 0.
    if (true) {
        MockResponseWriter w;

        w.header()->set_content_length(0);
        w.write_header(SRS_CONSTS_HTTP_OK);
        w.final_request();

        __MOCK_HTTP_EXPECT_STREQ(200, "", w);
    }

    // If set content-length to 0 then write, send an empty resonse with content-length 0.
    if (true) {
        MockResponseWriter w;

        w.header()->set_content_length(0);
        w.write_header(SRS_CONSTS_HTTP_OK);
        w.write(NULL, 0);

        __MOCK_HTTP_EXPECT_STREQ(200, "", w);
    }

    // If write_header without content-length, enter chunked encoding mode.
    if (true) {
        MockResponseWriter w;

        w.header()->set_content_type("application/octet-stream");
        w.write_header(SRS_CONSTS_HTTP_OK);
        w.write((char*)"Hello", 5);
        w.write((char*)", world!", 8);
        w.final_request();

        __MOCK_HTTP_EXPECT_STREQ2(200, "5\r\nHello\r\n8\r\n, world!\r\n0\r\n\r\n", w);
    }

    // If directly write empty string, sent an empty response with content-length 0
    if (true) {
        MockResponseWriter w;
        w.write(NULL, 0);
        __MOCK_HTTP_EXPECT_STREQ(200, "", w);
    }

    // If directly final request, response with EOF of chunked.
    if (true) {
        MockResponseWriter w;
        w.final_request();
        __MOCK_HTTP_EXPECT_STREQ2(200, "0\r\n\r\n", w);
    }
}

VOID TEST(ProtocolHTTPTest, ResponseHTTPError)
{
    srs_error_t err;

    if (true) {
        MockResponseWriter w;
        HELPER_EXPECT_SUCCESS(srs_go_http_error(&w, SRS_CONSTS_HTTP_Found));
        __MOCK_HTTP_EXPECT_STREQ(302, "Found", w);
    }

    if (true) {
        MockResponseWriter w;
        HELPER_EXPECT_SUCCESS(srs_go_http_error(&w, SRS_CONSTS_HTTP_InternalServerError));
        __MOCK_HTTP_EXPECT_STREQ(500, "Internal Server Error", w);
    }

    if (true) {
        MockResponseWriter w;
        HELPER_EXPECT_SUCCESS(srs_go_http_error(&w, SRS_CONSTS_HTTP_ServiceUnavailable));
        __MOCK_HTTP_EXPECT_STREQ(503, "Service Unavailable", w);
    }
}

VOID TEST(ProtocolHTTPTest, HTTPHeader)
{
    SrsHttpHeader h;
    h.set("Server", "SRS");
    EXPECT_STREQ("SRS", h.get("Server").c_str());
    EXPECT_EQ(1, h.count());

    stringstream ss;
    h.write(ss);
    EXPECT_STREQ("Server: SRS\r\n", ss.str().c_str());

    h.del("Server");
    EXPECT_TRUE(h.get("Server").empty());
    EXPECT_EQ(0, h.count());

    EXPECT_EQ(-1, h.content_length());

    h.set_content_length(0);
    EXPECT_EQ(0, h.content_length());
    EXPECT_EQ(1, h.count());

    h.set_content_length(1024);
    EXPECT_EQ(1024, h.content_length());

    h.set_content_type("text/plain");
    EXPECT_STREQ("text/plain", h.content_type().c_str());
    EXPECT_EQ(2, h.count());

    SrsJsonObject* o = SrsJsonAny::object();
    h.dumps(o);
    EXPECT_EQ(2, o->count());
    srs_freep(o);
}

VOID TEST(ProtocolHTTPTest, HTTPServerMuxer)
{
    srs_error_t err;

    if (true) {
        SrsHttpServeMux s;
        HELPER_ASSERT_SUCCESS(s.initialize());

        MockHttpHandler* hroot = new MockHttpHandler("Hello, world!");
        HELPER_ASSERT_SUCCESS(s.handle("/", hroot));

        MockResponseWriter w;
        SrsHttpMessage r(NULL, NULL);
        HELPER_ASSERT_SUCCESS(r.set_url("/index.html", false));

        HELPER_ASSERT_SUCCESS(s.serve_http(&w, &r));
        __MOCK_HTTP_EXPECT_STREQ(200, "Hello, world!", w);
    }
}

VOID TEST(ProtocolHTTPTest, VodStreamHandlers)
{
    srs_error_t err;

    if (true) {
        SrsHttpMuxEntry e;
        e.pattern = "/";

        string fs;
        int nn_flv_prefix = 0;
        if (true) {
            char flv_header[13];
            nn_flv_prefix += sizeof(flv_header);
            HELPER_ARRAY_INIT(flv_header, 13, 0);
            fs.append(flv_header, 13);
        }
        if (true) {
            uint8_t tag[15] = {9};
            nn_flv_prefix += sizeof(tag);
            HELPER_ARRAY_INIT(tag+1, 14, 0);
            fs.append((const char*)tag, sizeof(tag));
        }
        if (true) {
            uint8_t tag[15] = {8};
            nn_flv_prefix += sizeof(tag);
            HELPER_ARRAY_INIT(tag+1, 14, 0);
            fs.append((const char*)tag, sizeof(tag));
        }
        string flv_content = "Hello, world!";
        fs.append(flv_content);

        SrsVodStream h("/tmp");
        h.set_fs_factory(new MockFileReaderFactory(fs));
        h.set_path_check(_mock_srs_path_always_exists);
        h.entry = &e;

        MockResponseWriter w;
        SrsHttpMessage r(NULL, NULL);
        HELPER_ASSERT_SUCCESS(r.set_url("/index.flv?start=" + srs_int2str(nn_flv_prefix + 2), false));

        HELPER_ASSERT_SUCCESS(h.serve_http(&w, &r));

        // We only compare the last content, ignore HTTP and FLV header.
        string av2 = HELPER_BUFFER2STR(&w.io.out_buffer);
        string av = av2.substr(av2.length() - flv_content.length() + 2);
        string ev2 = mock_http_response(200, "llo, world!");
        string ev = ev2.substr(ev2.length() - flv_content.length() + 2);
        EXPECT_STREQ(ev.c_str(), av.c_str());
    }

    if (true) {
        SrsHttpMuxEntry e;
        e.pattern = "/";

        SrsVodStream h("/tmp");
        h.set_fs_factory(new MockFileReaderFactory("Hello, world!"));
        h.set_path_check(_mock_srs_path_always_exists);
        h.entry = &e;

        MockResponseWriter w;
        SrsHttpMessage r(NULL, NULL);
        HELPER_ASSERT_SUCCESS(r.set_url("/index.mp4?range=2-3", false));

        HELPER_ASSERT_SUCCESS(h.serve_http(&w, &r));
        __MOCK_HTTP_EXPECT_STREQ(206, "ll", w);
    }

    if (true) {
        SrsHttpMuxEntry e;
        e.pattern = "/";

        SrsVodStream h("/tmp");
        h.set_fs_factory(new MockFileReaderFactory("Hello, world!"));
        h.set_path_check(_mock_srs_path_always_exists);
        h.entry = &e;

        MockResponseWriter w;
        SrsHttpMessage r(NULL, NULL);
        HELPER_ASSERT_SUCCESS(r.set_url("/index.mp4?bytes=2-5", false));

        HELPER_ASSERT_SUCCESS(h.serve_http(&w, &r));
        __MOCK_HTTP_EXPECT_STREQ(206, "llo,", w);
    }
}

VOID TEST(ProtocolHTTPTest, BasicHandlers)
{
    srs_error_t err;

    if (true) {
        SrsHttpMuxEntry e;
        e.pattern = "/";

        SrsHttpFileServer h("/tmp");
        h.set_fs_factory(new MockFileReaderFactory("Hello, world!"));
        h.set_path_check(_mock_srs_path_always_exists);
        h.entry = &e;

        MockResponseWriter w;
        SrsHttpMessage r(NULL, NULL);
        HELPER_ASSERT_SUCCESS(r.set_url("/index.mp4?range=12-3", false));

        HELPER_ASSERT_SUCCESS(h.serve_http(&w, &r));
        __MOCK_HTTP_EXPECT_STREQ(200, "Hello, world!", w);
    }

    if (true) {
        SrsHttpMuxEntry e;
        e.pattern = "/";

        SrsHttpFileServer h("/tmp");
        h.set_fs_factory(new MockFileReaderFactory("Hello, world!"));
        h.set_path_check(_mock_srs_path_always_exists);
        h.entry = &e;

        MockResponseWriter w;
        SrsHttpMessage r(NULL, NULL);
        HELPER_ASSERT_SUCCESS(r.set_url("/index.mp4?range=2-3", false));

        HELPER_ASSERT_SUCCESS(h.serve_http(&w, &r));
        __MOCK_HTTP_EXPECT_STREQ(200, "Hello, world!", w);
    }

    if (true) {
        SrsHttpMuxEntry e;
        e.pattern = "/";

        SrsHttpFileServer h("/tmp");
        h.set_fs_factory(new MockFileReaderFactory("Hello, world!"));
        h.set_path_check(_mock_srs_path_always_exists);
        h.entry = &e;

        MockResponseWriter w;
        SrsHttpMessage r(NULL, NULL);
        HELPER_ASSERT_SUCCESS(r.set_url("/index.flv?start=2", false));

        HELPER_ASSERT_SUCCESS(h.serve_http(&w, &r));
        __MOCK_HTTP_EXPECT_STREQ(200, "Hello, world!", w);
    }

    if (true) {
        SrsHttpMuxEntry e;
        e.pattern = "/";

        SrsHttpFileServer h("/tmp");
        h.set_fs_factory(new MockFileReaderFactory("Hello, world!"));
        h.set_path_check(_mock_srs_path_always_exists);
        h.entry = &e;

        MockResponseWriter w;
        SrsHttpMessage r(NULL, NULL);
        HELPER_ASSERT_SUCCESS(r.set_url("/index.flv", false));

        HELPER_ASSERT_SUCCESS(h.serve_http(&w, &r));
        __MOCK_HTTP_EXPECT_STREQ(200, "Hello, world!", w);
    }

    if (true) {
        SrsHttpMuxEntry e;
        e.pattern = "/";

        SrsHttpFileServer h("/tmp");
        h.set_fs_factory(new MockFileReaderFactory("Hello, world!"));
        h.set_path_check(_mock_srs_path_always_exists);
        h.entry = &e;

        MockResponseWriter w;
        SrsHttpMessage r(NULL, NULL);
        HELPER_ASSERT_SUCCESS(r.set_url("/index.html", false));

        HELPER_ASSERT_SUCCESS(h.serve_http(&w, &r));
        __MOCK_HTTP_EXPECT_STREQ(200, "Hello, world!", w);
    }

    if (true) {
        SrsHttpMuxEntry e;
        e.pattern = "/";

        SrsHttpFileServer h("/tmp");
        h.set_fs_factory(new MockFileReaderFactory("Hello, world!"));
        h.set_path_check(_mock_srs_path_not_exists);
        h.entry = &e;

        MockResponseWriter w;
        SrsHttpMessage r(NULL, NULL);
        HELPER_ASSERT_SUCCESS(r.set_url("/index.html", false));

        HELPER_ASSERT_SUCCESS(h.serve_http(&w, &r));
        __MOCK_HTTP_EXPECT_STREQ(404, "Not Found", w);
    }

    if (true) {
        EXPECT_STREQ("/tmp/index.html", srs_http_fs_fullpath("/tmp", "/", "/").c_str());
        EXPECT_STREQ("/tmp/index.html", srs_http_fs_fullpath("/tmp", "/", "/index.html").c_str());
        EXPECT_STREQ("/tmp/index.html", srs_http_fs_fullpath("/tmp/", "/", "/index.html").c_str());
        EXPECT_STREQ("/tmp/ndex.html", srs_http_fs_fullpath("/tmp/", "//", "/index.html").c_str());
        EXPECT_STREQ("/tmp/index.html", srs_http_fs_fullpath("/tmp/", "/api", "/api/index.html").c_str());
        EXPECT_STREQ("/tmp/index.html", srs_http_fs_fullpath("/tmp/", "ossrs.net/api", "/api/index.html").c_str());
        EXPECT_STREQ("/tmp/views/index.html", srs_http_fs_fullpath("/tmp/", "/api", "/api/views/index.html").c_str());
        EXPECT_STREQ("/tmp/index.html", srs_http_fs_fullpath("/tmp/", "/api/", "/api/index.html").c_str());
        EXPECT_STREQ("/tmp/ndex.html", srs_http_fs_fullpath("/tmp/", "/api//", "/api/index.html").c_str());
    }

    if (true) {
        SrsHttpRedirectHandler h("/api", 500);
        EXPECT_FALSE(h.is_not_found());

        MockResponseWriter w;
        SrsHttpMessage r(NULL, NULL);
        HELPER_ASSERT_SUCCESS(r.set_url("/api?v=2.0", false));

        HELPER_ASSERT_SUCCESS(h.serve_http(&w, &r));
        __MOCK_HTTP_EXPECT_STREQ(500, "Redirect to /api?v=2.0", w);
    }

    if (true) {
        SrsHttpNotFoundHandler h;

        MockResponseWriter w;
        HELPER_ASSERT_SUCCESS(h.serve_http(&w, NULL));
        __MOCK_HTTP_EXPECT_STREQ(404, "Not Found", w);
        EXPECT_TRUE(h.is_not_found());
    }
}

class MockMSegmentsReader : public ISrsReader
{
public:
    vector<string> in_bytes;
public:
    MockMSegmentsReader();
    virtual ~MockMSegmentsReader();
public:
    virtual srs_error_t read(void* buf, size_t size, ssize_t* nread);
};

MockMSegmentsReader::MockMSegmentsReader()
{
}

MockMSegmentsReader::~MockMSegmentsReader()
{
}

srs_error_t MockMSegmentsReader::read(void* buf, size_t size, ssize_t* nread)
{
    srs_error_t err = srs_success;

    for (;;) {
        if (in_bytes.empty() || size <= 0) {
            return srs_error_new(-1, "EOF");
        }

        string v = in_bytes[0];
        if (v.empty()) {
            in_bytes.erase(in_bytes.begin());
            continue;
        }

        int nn = srs_min(size, v.length());
        memcpy(buf, v.data(), nn);
        if (nread) {
            *nread = nn;
        }

        if (nn < (int)v.length()) {
            in_bytes[0] = string(v.data() + nn, v.length() - nn);
        } else {
            in_bytes.erase(in_bytes.begin());
        }
        break;
    }

    return err;
}

VOID TEST(ProtocolHTTPTest, MSegmentsReader)
{
    srs_error_t err;

    MockMSegmentsReader r;
    r.in_bytes.push_back("GET /api/v1/versions HTTP/1.1\r\n");
    r.in_bytes.push_back("Host: ossrs.net\r\n");

    if (true) {
        char buf[1024];
        HELPER_ARRAY_INIT(buf, 1024, 0);

        ssize_t nn = 0;
        HELPER_EXPECT_SUCCESS(r.read(buf, 1024, &nn));
        ASSERT_EQ(31, nn);
        EXPECT_STREQ("GET /api/v1/versions HTTP/1.1\r\n", buf);
    }

    if (true) {
        char buf[1024];
        HELPER_ARRAY_INIT(buf, 1024, 0);

        ssize_t nn = 0;
        HELPER_EXPECT_SUCCESS(r.read(buf, 1024, &nn));
        ASSERT_EQ(17, nn);
        EXPECT_STREQ("Host: ossrs.net\r\n", buf);
    }
}

VOID TEST(ProtocolHTTPTest, HTTPMessageParser)
{
    srs_error_t err;

    if (true) {
        MockMSegmentsReader r;
        r.in_bytes.push_back("GET /api/v1/versions HTTP/1.1\r\n");
        r.in_bytes.push_back("Host: ossrs");
        r.in_bytes.push_back(".net\r");
        r.in_bytes.push_back("\n");
        r.in_bytes.push_back("\r\n");

        SrsHttpParser p;
        HELPER_ASSERT_SUCCESS(p.initialize(HTTP_REQUEST, false));

        ISrsHttpMessage* msg = NULL;
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
        EXPECT_TRUE(msg->is_http_get());
        EXPECT_STREQ("/api/v1/versions", msg->path().c_str());
        EXPECT_STREQ("ossrs.net", msg->host().c_str());
        srs_freep(msg);
    }

    if (true) {
        MockMSegmentsReader r;
        r.in_bytes.push_back("GET /api/v1/versions HTTP/1.1\r\n");
        r.in_bytes.push_back("Host: ossrs");
        r.in_bytes.push_back(".net\r\n");
        r.in_bytes.push_back("\r\n");

        SrsHttpParser p;
        HELPER_ASSERT_SUCCESS(p.initialize(HTTP_REQUEST, false));

        ISrsHttpMessage* msg = NULL;
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
        EXPECT_TRUE(msg->is_http_get());
        EXPECT_STREQ("/api/v1/versions", msg->path().c_str());
        EXPECT_STREQ("ossrs.net", msg->host().c_str());
        srs_freep(msg);
    }

    if (true) {
        MockMSegmentsReader r;
        r.in_bytes.push_back("GET /api/v1/versions HTTP/1.1\r\n");
        r.in_bytes.push_back("User-Agent: curl/7.54.0\r\n");
        r.in_bytes.push_back("Host: ossrs");
        r.in_bytes.push_back(".net\r\n");
        r.in_bytes.push_back("\r\n");

        SrsHttpParser p;
        HELPER_ASSERT_SUCCESS(p.initialize(HTTP_REQUEST, false));

        ISrsHttpMessage* msg = NULL;
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
        EXPECT_TRUE(msg->is_http_get());
        EXPECT_STREQ("/api/v1/versions", msg->path().c_str());
        EXPECT_STREQ("ossrs.net", msg->host().c_str());
        EXPECT_STREQ("curl/7.54.0", msg->header()->get("User-Agent").c_str());
        srs_freep(msg);
    }

    if (true) {
        MockMSegmentsReader r;
        r.in_bytes.push_back("GET /api/v1/versions HTTP/1.1\r\n");
        r.in_bytes.push_back("User-");
        r.in_bytes.push_back("Agent: curl/7.54.0\r\n");
        r.in_bytes.push_back("Host: ossrs");
        r.in_bytes.push_back(".net\r\n");
        r.in_bytes.push_back("\r\n");

        SrsHttpParser p;
        HELPER_ASSERT_SUCCESS(p.initialize(HTTP_REQUEST, false));

        ISrsHttpMessage* msg = NULL;
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
        EXPECT_TRUE(msg->is_http_get());
        EXPECT_STREQ("/api/v1/versions", msg->path().c_str());
        EXPECT_STREQ("ossrs.net", msg->host().c_str());
        EXPECT_STREQ("curl/7.54.0", msg->header()->get("User-Agent").c_str());
        srs_freep(msg);
    }

    if (true) {
        MockMSegmentsReader r;
        r.in_bytes.push_back("GET /api/v1/versions HTTP/1.1\r\n");
        r.in_bytes.push_back("User-");
        r.in_bytes.push_back("Agent: curl");
        r.in_bytes.push_back("/7.54.0\r\n");
        r.in_bytes.push_back("Host: ossrs");
        r.in_bytes.push_back(".net\r\n");
        r.in_bytes.push_back("\r\n");

        SrsHttpParser p;
        HELPER_ASSERT_SUCCESS(p.initialize(HTTP_REQUEST, false));

        ISrsHttpMessage* msg = NULL;
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
        EXPECT_TRUE(msg->is_http_get());
        EXPECT_STREQ("/api/v1/versions", msg->path().c_str());
        EXPECT_STREQ("ossrs.net", msg->host().c_str());
        EXPECT_STREQ("curl/7.54.0", msg->header()->get("User-Agent").c_str());
        srs_freep(msg);
    }

    if (true) {
        MockMSegmentsReader r;
        r.in_bytes.push_back("GET /api/v1/versions HTTP/1.1\r\n");
        r.in_bytes.push_back("Host: ossrs.net\r\n");
        r.in_bytes.push_back("\r\n");

        SrsHttpParser p;
        HELPER_ASSERT_SUCCESS(p.initialize(HTTP_REQUEST, false));

        ISrsHttpMessage* msg = NULL;
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
        EXPECT_TRUE(msg->is_http_get());
        EXPECT_STREQ("/api/v1/versions", msg->path().c_str());
        EXPECT_STREQ("ossrs.net", msg->host().c_str());
        srs_freep(msg);
    }
}