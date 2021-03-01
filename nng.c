#include <ctype.h>
#include <nng/nng.h>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>
#include <nng/supplemental/http/http.h>
#include <nng/supplemental/util/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

static void fatal(const char *what, int rv) {
  fprintf(stderr, "%s: %s\n", what, nng_strerror(rv));
  exit(1);
}

static void rest_http_fatal(nng_http_res *http_res, nng_aio *aio,
                            const char *fmt, int rv) {
  char buf[128];

  snprintf(buf, sizeof(buf), fmt, nng_strerror(rv));
  nng_http_res_set_status(http_res, NNG_HTTP_STATUS_INTERNAL_SERVER_ERROR);
  nng_http_res_set_reason(http_res, buf);
  nng_aio_set_output(aio, 0, http_res);
  nng_aio_finish(aio, 0);
}

static void rest_handle(nng_aio *aio) {
  nng_http_req *req = nng_aio_get_input(aio, 0);
  size_t sz;
  int rv;
  void *data;

  nng_http_res *http_res;
  if (((rv = nng_http_res_alloc(&http_res)) != 0)) {
    rest_http_fatal(http_res, aio, "copy", rv);
    nng_aio_finish(aio, rv);
    return;
  }

  nng_http_req_get_data(req, &data, &sz);
  nng_http_res_set_data(http_res, data, sz);
  nng_aio_set_output(aio, 0, http_res);
  nng_aio_finish(aio, 0);
}

static void rest_start(u16 port) {
  nng_http_server *server;
  nng_http_handler *handler;
  nng_url *url;
  int rv;

  static char base_url[50];
  snprintf(base_url, sizeof(base_url), "http://0.0.0.0:%hu", port);
  if ((rv = nng_url_parse(&url, base_url)) != 0) {
    fatal("nng_url_parse", rv);
  }

  // Get a suitable HTTP server instance.  This creates one
  // if it doesn't already exist.
  if ((rv = nng_http_server_hold(&server, url)) != 0) {
    fatal("nng_http_server_hold", rv);
  }

  // `/`
  {
    // Allocate the handler - we use a dynamic handler for REST
    // using the function "rest_handle" declared above.
    rv = nng_http_handler_alloc(&handler, url->u_path, rest_handle);
    if (rv != 0) {
      fatal("nng_http_handler_alloc", rv);
    }
    if ((rv = nng_http_handler_set_method(handler, "POST")) != 0) {
      fatal("nng_http_handler_set_method", rv);
    }
    if ((rv = nng_http_handler_collect_body(handler, true, 1024 * 128)) != 0) {
      fatal("nng_http_handler_collect_body", rv);
    }
    if ((rv = nng_http_server_add_handler(server, handler)) != 0) {
      fatal("nng_http_server_add_handler", rv);
    }
  }
  // `/home`
  {
    nng_http_handler *home_handler;
    rv = nng_http_handler_alloc_file(&home_handler, "/home", "home.html");
    if (rv != 0) {
      fatal("nng_http_handler_alloc", rv);
    }
    if ((rv = nng_http_server_add_handler(server, home_handler)) != 0) {
      fatal("nng_http_server_add_handler", rv);
    }
  }

  if ((rv = nng_http_server_start(server)) != 0) {
    fatal("nng_http_server_start", rv);
  }
}

int main() {
  u16 port = 0;

  if (getenv("PORT") != NULL) {
    port = (u16)atoi(getenv("PORT"));
  }
  port = port ? port : 8888;
  rest_start(port);

  // Wait forever
  nng_mtx *mutex;
  nng_mtx_alloc(&mutex);
  nng_mtx_lock(mutex);
  nng_cv *cond_wait;
  nng_cv_alloc(&cond_wait, mutex);
  nng_cv_wait(cond_wait);
}
