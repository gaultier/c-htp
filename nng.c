#define REST_URL "http://127.0.0.1:%u/api/rest/rot13"

#include <ctype.h>
#include <nng/nng.h>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>
#include <nng/supplemental/http/http.h>
#include <nng/supplemental/util/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// utility function
/* void fatal(const char *what, int rv) { */
/*   fprintf(stderr, "%s: %s\n", what, nng_strerror(rv)); */
/*   exit(1); */
/* } */

///* typedef struct rest_job { */
/*   nng_aio *http_aio;       // aio from HTTP we must reply to */
/*   nng_http_res *http_res;  // HTTP response object */
/*   job_state state;         // 0 = sending, 1 = receiving */
/*   nng_msg *msg;            // request message */
/*   nng_aio *aio;            // request flow */
/*   nng_ctx ctx;             // context on the request socket */
/*   struct rest_job *next;   // next on the freelist */
/* } rest_job; */

nng_socket req_sock;

// We maintain a queue of free jobs.  This way we don't have to
// deallocate them from the callback; we just reuse them.
/* nng_mtx *job_lock; */
/* rest_job *job_freelist; */

/* static void rest_job_cb(void *arg); */

/* static void rest_recycle_job(rest_job *job) { */
/*   if (job->http_res != NULL) { */
/*     nng_http_res_free(job->http_res); */
/*     job->http_res = NULL; */
/*   } */
/*   if (job->msg != NULL) { */
/*     nng_msg_free(job->msg); */
/*     job->msg = NULL; */
/*   } */
/*   if (nng_ctx_id(job->ctx) != 0) { */
/*     nng_ctx_close(job->ctx); */
/*   } */

/*   nng_mtx_lock(job_lock); */
/*   job->next = job_freelist; */
/*   job_freelist = job; */
/*   nng_mtx_unlock(job_lock); */
/* } */

/* static rest_job *rest_get_job(void) { */
/*   rest_job *job; */

/*   nng_mtx_lock(job_lock); */
/*   if ((job = job_freelist) != NULL) { */
/*     job_freelist = job->next; */
/*     nng_mtx_unlock(job_lock); */
/*     job->next = NULL; */
/*     return (job); */
/*   } */
/*   nng_mtx_unlock(job_lock); */
/*   if ((job = calloc(1, sizeof(*job))) == NULL) { */
/*     return (NULL); */
/*   } */
/*   if (nng_aio_alloc(&job->aio, rest_job_cb, job) != 0) { */
/*     free(job); */
/*     return (NULL); */
/*   } */
/*   return (job); */
/* } */

/* static void rest_http_fatal(rest_job *job, const char *fmt, int rv) { */
/*   char buf[128]; */
/*   nng_aio *aio = job->http_aio; */
/*   nng_http_res *res = job->http_res; */

/*   job->http_res = NULL; */
/*   job->http_aio = NULL; */
/*   snprintf(buf, sizeof(buf), fmt, nng_strerror(rv)); */
/*   nng_http_res_set_status(res, NNG_HTTP_STATUS_INTERNAL_SERVER_ERROR); */
/*   nng_http_res_set_reason(res, buf); */
/*   nng_aio_set_output(aio, 0, res); */
/*   nng_aio_finish(aio, 0); */
/*   /1* rest_recycle_job(job); *1/ */
/* } */

/* static void rest_job_cb(void *arg) { */
/*   rest_job *job = arg; */
/*   int rv; */

/*   static char data[] = "hello"; */
/*   rv = nng_http_res_set_data(job->http_res, data, 5); */
/*   if (rv != 0) { */
/*     rest_http_fatal(job, "nng_http_res_copy_data: %s", rv); */
/*     return; */
/*   } */
/*   // Set the output - the HTTP server will send it back to the */
/*   // user agent with a 200 response. */
/*   /1* job->http_aio = NULL; *1/ */
/*   /1* job->http_res = NULL; *1/ */
/*   // We are done with the job. */
/*   /1* rest_recycle_job(job); *1/ */
/*   return; */
/* } */

// Our rest server just takes the message body, creates a request ID
// for it, and sends it on.  This runs in raw mode, so
void rest_handle(nng_aio *aio) {
  nng_http_req *req = nng_aio_get_input(aio, 0);
  /* nng_http_conn *conn = nng_aio_get_input(aio, 2); */
  size_t sz;
  int rv;
  void *data;

  /* if ((job = rest_get_job()) == NULL) { */
  /*   nng_aio_finish(aio, NNG_ENOMEM); */
  /*   return; */
  /* } */
  nng_http_res *http_res;
  if (((rv = nng_http_res_alloc(&http_res)) != 0)) {
    /* rest_recycle_job(job); */
    nng_aio_finish(aio, rv);
    return;
  }

  nng_http_req_get_data(req, &data, &sz);
  nng_http_res_set_data(http_res, data, sz);
  nng_aio_set_output(aio, 0, http_res);
  nng_aio_finish(aio, 0);
}

void rest_start(uint16_t port) {
  nng_http_server *server;
  nng_http_handler *handler;
  char rest_addr[128];
  nng_url *url;
  int rv;

  // Set up some strings, etc.  We use the port number
  // from the argument list.
  snprintf(rest_addr, sizeof(rest_addr), REST_URL, port);
  if ((rv = nng_url_parse(&url, rest_addr)) != 0) {
    abort();
  }

  // Get a suitable HTTP server instance.  This creates one
  // if it doesn't already exist.
  if ((rv = nng_http_server_hold(&server, url)) != 0) {
    abort();
  }

  // Allocate the handler - we use a dynamic handler for REST
  // using the function "rest_handle" declared above.
  rv = nng_http_handler_alloc(&handler, url->u_path, rest_handle);
  if (rv != 0) {
    abort();
  }

  if ((rv = nng_http_handler_set_method(handler, "POST")) != 0) {
    abort();
  }
  // We want to collect the body, and we (arbitrarily) limit this to
  // 128KB.  The default limit is 1MB.  You can explicitly collect
  // the data yourself with another HTTP read transaction by disabling
  // this, but that's a lot of work, especially if you want to handle
  // chunked transfers.
  if ((rv = nng_http_handler_collect_body(handler, true, 1024 * 128)) != 0) {
    abort();
  }
  if ((rv = nng_http_server_add_handler(server, handler)) != 0) {
    abort();
  }
  if ((rv = nng_http_server_start(server)) != 0) {
    abort();
  }

  nng_url_free(url);
}

int main(int argc, char **argv) {
  int rv;
  uint16_t port = 0;

  if (getenv("PORT") != NULL) {
    port = (uint16_t)atoi(getenv("PORT"));
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
