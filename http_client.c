#include <nng/nng.h>
#include <nng/supplemental/http/http.h>
#include <nng/supplemental/tls/tls.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    nng_aio *aio;
    nng_url *url;
    nng_http_client *client;
    nng_http_conn *conn;
    nng_http_res *res;
    nng_http_req *req;
    nng_tls_config *tls_config;
    int rv;
    const char *hdr;
    void *data;
    nng_iov iov;

    if ((rv = nng_tls_config_alloc(&tls_config, NNG_TLS_MODE_CLIENT)) != 0) {
        fprintf(stderr, "Failed to tls_config_alloc: %s\n", nng_strerror(rv));
        return 1;
    }
    if ((rv = nng_tls_config_auth_mode(tls_config,
                                       NNG_TLS_AUTH_MODE_REQUIRED)) != 0) {
        fprintf(stderr, "Failed to tls_config_auth_mode: %s\n",
                nng_strerror(rv));
        return 1;
    }

    if ((rv = nng_tls_config_ca_file(tls_config, "/tmp/cacert.pem")) != 0) {
        fprintf(stderr, "Failed to nng_tls_config_ca_file: %s\n",
                nng_strerror(rv));
        return 1;
    }

    if ((rv = nng_url_parse(&url, "https://google.com")) != 0) {
        fprintf(stderr, "Failed to parse url: %s\n", nng_strerror(rv));
        return 1;
    }
    if ((rv = nng_aio_alloc(&aio, NULL, NULL)) != 0) {
        fprintf(stderr, "Failed to aio_alloc: %s\n", nng_strerror(rv));
        return 1;
    }
    if ((rv = nng_http_client_alloc(&client, url)) != 0) {
        fprintf(stderr, "Failed to http_client_alloc: %s\n", nng_strerror(rv));
        return 1;
    }
    if ((rv = nng_http_req_alloc(&req, url)) != 0) {
        fprintf(stderr, "Failed to http_req_alloc: %s\n", nng_strerror(rv));
        return 1;
    }
    if ((rv = nng_http_res_alloc(&res)) != 0) {
        fprintf(stderr, "Failed to http_res_alloc: %s\n", nng_strerror(rv));
        return 1;
    }

    if ((rv = nng_http_client_set_tls(client, tls_config)) != 0) {
        fprintf(stderr, "Failed to nng_http_client_set_tls: %s\n",
                nng_strerror(rv));
        return 1;
    }

    nng_http_client_connect(client, aio);

    // Wait for connection to establish (or attempt to fail).
    nng_aio_wait(aio);

    if ((rv = nng_aio_result(aio)) != 0) {
        printf("Connection failed: %s\n", nng_strerror(rv));
        return 1;
    }

    // Connection established, get it.
    conn = nng_aio_get_output(aio, 0);

    // Send the request, and wait for that to finish.
    nng_http_conn_write_req(conn, req, aio);
    nng_aio_wait(aio);
    if ((rv = nng_aio_result(aio)) != 0) {
        printf("Failed to send request: %s\n", nng_strerror(rv));
        return 1;
    }

    // Read a response.
    nng_http_conn_read_res(conn, res, aio);
    nng_aio_wait(aio);

    if ((rv = nng_aio_result(aio)) != 0) {
        fprintf(stderr, "Failed to read result: %s\n", nng_strerror(rv));
        return 1;
    }

    if (nng_http_res_get_status(res) != NNG_HTTP_STATUS_OK) {
        fprintf(stderr, "HTTP Server Responded: %d %s\n",
                nng_http_res_get_status(res), nng_http_res_get_reason(res));
    }

    // This only supports regular transfer encoding (no Chunked-Encoding,
    // and a Content-Length header is required.)
    if ((hdr = nng_http_res_get_header(res, "Content-Length")) == NULL) {
        fprintf(stderr, "Missing Content-Length header.\n");
        exit(1);
    }

    size_t len = atoi(hdr);
    if (len == 0) {
        return (0);
    }

    // Allocate a buffer to receive the body data.
    data = malloc(len);

    // Set up a single iov to point to the buffer.
    iov.iov_len = len;
    iov.iov_buf = data;

    // Following never fails with fewer than 5 elements.
    nng_aio_set_iov(aio, 1, &iov);

    // Now attempt to receive the data.
    nng_http_conn_read_all(conn, aio);

    // Wait for it to complete.
    nng_aio_wait(aio);

    if ((rv = nng_aio_result(aio)) != 0) {
        fprintf(stderr, "Failed to read result: %s\n", nng_strerror(rv));
        return 1;
    }

    fwrite(data, 1, len, stdout);
}
