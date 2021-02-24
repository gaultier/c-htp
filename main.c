#include <http_parser.h>
#include <stdint.h>
#include <stdlib.h>
#include <uv.h>

#include "buf.h"
#include "common.h"

typedef struct {
    uv_tcp_t tcp;
} server_t;

typedef struct {
    uv_write_t req;
    uv_buf_t buf;
} write_req_t;

typedef struct {
    uv_tcp_t tcp;
    uv_timer_t timer;
    http_parser parser;
} client_t;

#define HTTP_OK                                  \
    "HTTP/1.1 200 OK\r\n"                        \
    "Content-Type: text/html; charset=UTF-8\r\n" \
    "Content-Length: 19\r\n"                     \
    "Connection: close\r\n"                      \
    "\r\n"                                       \
    "<html>Hello</html>"

typedef struct {
    usize str_len;
    char* str_s;
} str_t;

str_t str_from_c_str0_alloc(const char* c_str0) {
    CHECK((void*)c_str0, !=, NULL, "%p");

    const usize len = strlen(c_str0);
    u8* s = malloc(len);
    memcpy(s, c_str0, len);
    return (str_t){.str_len = len, .str_s = s};
}

str_t str_from_c_str0_noalloc(char* c_str0) {
    CHECK((void*)c_str0, !=, NULL, "%p");

    return (str_t){.str_len = strlen(c_str0), .str_s = c_str0};
}

str_t str_n(usize n) { return (str_t){.str_len = n, .str_s = calloc(n, 1)}; }

typedef struct {
    str_t hkv_key;
    str_t* hkv_values;
} http_header_t;

#define HTTP11 "HTTP/1.1"

static const char HDR_CONTENT_TYPE[] = "Content-Type";
static const char HDR_CONTENT_LENGTH[] = "Content-Length";

typedef enum http_status http_status_t;

typedef struct {
    http_header_t* hre_headers;
    http_status_t hre_status;
    str_t hre_body;
    str_t hre_version;
} http_response_t;

void http_response_to_str(const http_response_t* response, str_t s){};

void http_response_init(http_response_t* response, http_status_t status,
                        str_t hre_body, i32 http_headers_count, ...) {
    CHECK((void*)response, !=, NULL, "%p");

    response->hre_status = status;
    response->hre_body = hre_body;
    response->hre_version = str_from_c_str0_noalloc(HTTP11);

    str_t* header_content_length_values = NULL;
    str_t header_content_length_value = str_n(26);
    snprintf(header_content_length_value.str_s,
             header_content_length_value.str_len, "%llu", hre_body.str_len);
    buf_push(header_content_length_values, header_content_length_value);
    http_header_t header_content_length = (http_header_t){
        .hkv_key = str_from_c_str0_noalloc(HDR_CONTENT_LENGTH),
    };
    buf_push(response->hre_headers, header_content_length);

    va_list ap;
    va_start(ap, http_headers_count);

    for (; http_headers_count; http_headers_count--) {
        http_header_t header = va_arg(ap, http_header_t);
        buf_push(response->hre_headers, header);
    }
}

static void on_client_close(uv_handle_t* handle) { free(handle); }

static void echo_write(uv_write_t* req, int status) {
    if (status != 0) {
        fprintf(stderr, "%s:%d:Error writing to the client: %s\n", __FILE__,
                __LINE__, uv_strerror(status));
    }
    write_req_t* write_req = (write_req_t*)req;
    /* free(write_req->buf.base); */
    free(write_req);
}

static void echo_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    client_t* client = stream->data;
    http_parser_settings settings = {0};

    if (nread > 0) {
        write_req_t* req = malloc(sizeof(write_req_t));
        http_response_t response = {0};
        str_t body = str_from_c_str0("Hello, world!") http_response_init(
                         &response, HTTP_STATUS_OK, body, 1, ...)

                         req->buf = uv_buf_init(HTTP_OK, sizeof(HTTP_OK) - 1);

        size_t parsed =
            http_parser_execute(&client->parser, &settings, buf->base, nread);
        if (client->parser.upgrade) {
            goto err;
        } else if (parsed != nread) {
            goto err;
        }

        int status;
        if ((status = uv_write((uv_write_t*)req, stream, &req->buf, 1,
                               echo_write)) != 0) {
            fprintf(stderr, "%s:%d:Error writing to the client: %s\n", __FILE__,
                    __LINE__, uv_strerror(status));
            goto err;
        }
        return;
    } else if (nread < 0) {
        if (nread != UV_EOF) {
            fprintf(stderr, "%s:%d:Error reading: %s\n", __FILE__, __LINE__,
                    uv_strerror(nread));
            goto err;
        }
        size_t parsed =
            http_parser_execute(&client->parser, &settings, buf->base, 0);
        goto ok;
    }

err:
    uv_timer_stop(&client->timer);
    uv_close((uv_handle_t*)stream, on_client_close);

ok:
    if (buf) free((void*)buf->base);
}

static void alloc_cb(uv_handle_t* handle, size_t suggested_size,
                     uv_buf_t* buf) {
    (void)handle;
    buf->base = malloc(suggested_size);
    buf->len = suggested_size;
}

static void connection_close_on_timeout(uv_timer_t* context) {
    client_t* client = (client_t*)context->data;
    printf("Closing connection on timeout\n");
    uv_close((uv_handle_t*)&client->tcp, on_client_close);
}

static void on_connection(uv_stream_t* tcp, int status) {
    server_t* server = tcp->data;
    client_t* client = NULL;

    if (status != 0) {
        fprintf(stderr, "%s:%d:Error on_connection: %s\n", __FILE__, __LINE__,
                uv_strerror(status));
        goto err;
    }

    client = malloc(sizeof(client_t));

    if ((status = uv_tcp_init(uv_default_loop(), &client->tcp)) != 0) {
        fprintf(stderr, "%s:%d:Error uv_tcp_init: %s\n", __FILE__, __LINE__,
                uv_strerror(status));
        goto err;
    }
    client->tcp.data = client;
    if ((status = uv_accept((uv_stream_t*)&server->tcp,
                            (uv_stream_t*)client)) != 0) {
        fprintf(stderr, "%s:%d:Error uv_accept: %s\n", __FILE__, __LINE__,
                uv_strerror(status));
        goto err;
    }

    if ((status = uv_timer_init(uv_default_loop(), &client->timer)) != 0) {
        fprintf(stderr, "%s:%d:Error uv_timer_init: %s\n", __FILE__, __LINE__,
                uv_strerror(status));
        goto err;
    }

    client->timer.data = client;
    uv_timer_start(&client->timer, connection_close_on_timeout, 5000, 0);

    if ((status = uv_read_start((uv_stream_t*)client, alloc_cb, echo_read)) !=
        0) {
        fprintf(stderr, "%s:%d:Error uv_read_start: %s\n", __FILE__, __LINE__,
                uv_strerror(status));
        goto err;
    }
    http_parser_init(&client->parser, HTTP_REQUEST);
    client->parser.data = &client->tcp;

err:
    if (status != 0 && client != NULL) {
        uv_close((uv_handle_t*)client, on_client_close);
    }
}

int main() {
    struct sockaddr_in addr;
    uv_ip4_addr("127.0.0.1", 8888, &addr);

    server_t server = {0};
    int status = 0;

    if ((status = uv_tcp_init(uv_default_loop(), &server.tcp)) != 0) {
        fprintf(stderr, "%s:%d:Error uv_tcp_init: %s\n", __FILE__, __LINE__,
                uv_strerror(status));
        return status;
    }

    server.tcp.data = &server;

    if ((status = uv_tcp_bind(&server.tcp, (const struct sockaddr*)&addr, 0)) !=
        0) {
        fprintf(stderr, "%s:%d:Error uv_tcp_bind: %s\n", __FILE__, __LINE__,
                uv_strerror(status));
        return status;
    }
    if ((status = uv_listen((uv_stream_t*)&server.tcp, 10, on_connection)) !=
        0) {
        fprintf(stderr, "%s:%d:Error uv_listen: %s\n", __FILE__, __LINE__,
                uv_strerror(status));
        return status;
    }

    return uv_run(uv_default_loop(), UV_RUN_DEFAULT);
}
