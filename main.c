#include <assert.h>
//#include <http_parser.h>
#include <stdlib.h>
#include <uv.h>

static uv_loop_t* loop;

typedef struct {
    uv_tcp_t tcp;
    uv_connect_t connect_req;
    uv_shutdown_t shutdown_req;
    uv_buf_t data[10000];
} pinger_t;

typedef struct buf_s {
    uv_buf_t uv_buf_t;
    struct buf_s* next;
} buf_t;

typedef struct {
    uv_write_t req;
    uv_buf_t buf;
} write_req_t;

static pinger_t pinger;

static void echo_write(uv_write_t* req, int status) {
    if (status != 0) {
        fprintf(stderr, "%s:%d:Error writing to the client: %s\n", __FILE__,
                __LINE__, uv_strerror(status));
    }
    write_req_t* write_req = (write_req_t*)req;
    free(write_req->buf.base);
    free(write_req);
}

static void echo_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf) {
    if (nread > 0) {
        write_req_t* req = malloc(sizeof(write_req_t));
        req->buf = uv_buf_init(buf->base, nread);

        int status;
        if ((status = uv_write((uv_write_t*)req, client, &req->buf, 1,
                               echo_write)) != 0) {
            fprintf(stderr, "%s:%d:Error writing to the client: %s\n", __FILE__,
                    __LINE__, uv_strerror(status));
        }
        return;
    } else if (nread < 0) {
        if (nread != UV_EOF) {
            fprintf(stderr, "%s:%d:Error reading: %s\n", __FILE__, __LINE__,
                    uv_strerror(nread));
        }
        uv_close((uv_handle_t*)client, NULL);
    }
    free((void*)buf->base);
}

static void alloc_cb(uv_handle_t* handle, size_t suggested_size,
                     uv_buf_t* buf) {
    (void)handle;
    buf->base = malloc(suggested_size);
    buf->len = suggested_size;
}

static void on_connection(uv_stream_t* tcp, int status) {
    pinger_t* pinger = tcp->data;
    uv_tcp_t* client = NULL;

    if (status != 0) {
        fprintf(stderr, "%s:%d:Error on_connection: %s\n", __FILE__, __LINE__,
                uv_strerror(status));
        goto err;
    }

    client = malloc(sizeof(uv_tcp_t));

    if ((status = uv_tcp_init(loop, client)) != 0) {
        fprintf(stderr, "%s:%d:Error uv_tcp_init: %s\n", __FILE__, __LINE__,
                uv_strerror(status));
        goto err;
    }
    if ((status = uv_accept((uv_stream_t*)&pinger->tcp,
                            (uv_stream_t*)client)) != 0) {
        fprintf(stderr, "%s:%d:Error uv_accept: %s\n", __FILE__, __LINE__,
                uv_strerror(status));
        goto err;
    }
    if ((status = uv_read_start((uv_stream_t*)client, alloc_cb, echo_read)) !=
        0) {
        fprintf(stderr, "%s:%d:Error uv_read_start: %s\n", __FILE__, __LINE__,
                uv_strerror(status));
        goto err;
    }

err:
    if (status != 0 && client != NULL) {
        free(client);
    }
}

int main() {
    loop = uv_default_loop();

    struct sockaddr_in addr;
    uv_ip4_addr("127.0.0.1", 8888, &addr);

    pinger = (pinger_t){0};
    int status = 0;

    if ((status = uv_tcp_init(loop, &pinger.tcp)) != 0) {
        fprintf(stderr, "%s:%d:Error uv_tcp_init: %s\n", __FILE__, __LINE__,
                uv_strerror(status));
        return status;
    }

    pinger.tcp.data = &pinger;

    if ((status = uv_tcp_bind(&pinger.tcp, (const struct sockaddr*)&addr, 0)) !=
        0) {
        fprintf(stderr, "%s:%d:Error uv_tcp_bind: %s\n", __FILE__, __LINE__,
                uv_strerror(status));
        return status;
    }
    if ((status = uv_listen((uv_stream_t*)&pinger.tcp, 10, on_connection)) !=
        0) {
        fprintf(stderr, "%s:%d:Error uv_listen: %s\n", __FILE__, __LINE__,
                uv_strerror(status));
        return status;
    }

    return uv_run(loop, UV_RUN_DEFAULT);
}
