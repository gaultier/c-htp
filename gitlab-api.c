#include <curl/curl.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "deps/buf/buf.h"
#include "deps/jsmn/jsmn.h"
#include "deps/sds/sds.c"
#include "deps/sds/sds.h"
#include "deps/sds/sdsalloc.h"

typedef int64_t i64;
typedef uint64_t u64;

static const char *urls[] = {
    "https://gitlab.com/api/v4/projects/3472737",
    "https://gitlab.com/api/v4/projects/278964",
};

typedef struct {
  i64 pf_id;
  sds pf_name;
  sds pf_path_with_namespace;
  sds pf_api_url;
  sds pf_api_data;
} project_t;

project_t *projects = NULL;

void project_init(project_t *project, char *api_url) {
  project->pf_name = sdsempty();
  project->pf_path_with_namespace = sdsempty();
  project->pf_api_url = sdsnew(api_url);
  project->pf_api_data = sdsempty();
}

#define NUM_URLS sizeof(urls) / sizeof(char *)

static size_t write_cb(char *data, size_t n, size_t l, void *userp) {
  /* take care of the data here, ignored in this example */
  (void)data;

  const i64 project_i = (i64)userp;
  project_t *project = &projects[project_i];
  fprintf(stderr, "[%s] %.*s\n", project->pf_api_url, (int)(n * l), data);
  project->pf_api_data = sdscatlen(project->pf_api_data, data, n * l);

  return n * l;
}

static void add_transfer(CURLM *cm, int i) {
  CURL *eh = curl_easy_init();
  curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(eh, CURLOPT_URL, urls[i]);
  curl_easy_setopt(eh, CURLOPT_WRITEDATA, i);
  curl_multi_add_handle(cm, eh);
}

int main() {
  CURLM *cm;
  CURLMsg *msg;
  int msgs_left = -1;
  int still_alive = 1;

  curl_global_init(CURL_GLOBAL_ALL);
  cm = curl_multi_init();

  /* Limit the amount of simultaneous connections curl should allow: */
  curl_multi_setopt(cm, CURLMOPT_MAXCONNECTS, NUM_URLS);

  for (i64 i = 0; i < (i64)NUM_URLS; i++) {
    project_t project = {0};
    project_init(&project, (char *)urls[i]);
    buf_push(projects, project);
    add_transfer(cm, i);
  }

  do {
    curl_multi_perform(cm, &still_alive);

    while ((msg = curl_multi_info_read(cm, &msgs_left))) {
      if (msg->msg == CURLMSG_DONE) {
        char *url;
        CURL *e = msg->easy_handle;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &url);
        fprintf(stderr, "R: %d - %s <%s>\n", msg->data.result,
                curl_easy_strerror(msg->data.result), url);
        curl_multi_remove_handle(cm, e);
        curl_easy_cleanup(e);
      } else {
        fprintf(stderr, "E: CURLMsg (%d)\n", msg->msg);
      }
    }
    if (still_alive) curl_multi_wait(cm, NULL, 0, 1000, NULL);

  } while (still_alive);
}
