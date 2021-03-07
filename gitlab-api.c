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

typedef struct {
  i64 pf_id;
  sds pf_name;
  sds pf_path_with_namespace;
  sds pf_api_url;
  sds pf_api_data;
} project_t;

project_t *projects = NULL;

void project_init(project_t *project, i64 id) {
  project->pf_name = sdsempty();
  project->pf_path_with_namespace = sdsempty();
  project->pf_api_url =
      sdscatprintf(sdsempty(), "https://gitlab.com/api/v4/projects/%lld", id);
  project->pf_api_data = sdsempty();
}

void project_parse_json(project_t *project) {
  jsmn_parser parser;
  jsmn_init(&parser);

  jsmntok_t tokens[512] = {0};
  int res = jsmn_parse(&parser, project->pf_api_data,
                       sdslen(project->pf_api_data), NULL, 0);
  if (res <= 0) {
    fprintf(stderr, "Failed to parse project info: \n");
    return;
  }
}

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
  curl_easy_setopt(eh, CURLOPT_URL, projects[i].pf_api_url);
  curl_easy_setopt(eh, CURLOPT_WRITEDATA, i);
  curl_easy_setopt(eh, CURLOPT_PRIVATE, i);
  curl_multi_add_handle(cm, eh);
}

int main() {
  CURLM *cm;
  CURLMsg *msg;
  int msgs_left = -1;
  int still_alive = 1;
  i64 *project_ids = NULL;
  buf_push(project_ids, 3472737);
  buf_push(project_ids, 278964);

  curl_global_init(CURL_GLOBAL_ALL);
  cm = curl_multi_init();

  for (i64 i = 0; i < buf_size(project_ids); i++) {
    const i64 id = project_ids[i];

    project_t project = {0};
    project_init(&project, id);
    buf_push(projects, project);
    add_transfer(cm, i);
  }

  do {
    curl_multi_perform(cm, &still_alive);

    while ((msg = curl_multi_info_read(cm, &msgs_left))) {
      if (msg->msg == CURLMSG_DONE) {
        CURL *e = msg->easy_handle;
        i64 project_i = 0;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &project_i);
        project_t *project = &projects[project_i];
        fprintf(stderr, "R: %d - %s <%s>\n", msg->data.result,
                curl_easy_strerror(msg->data.result), project->pf_api_url);
        if (msg->data.result == 0) {
          project_parse_json(project);
        }
        curl_multi_remove_handle(cm, e);
        curl_easy_cleanup(e);
      } else {
        fprintf(stderr, "E: CURLMsg (%d)\n", msg->msg);
      }
    }
    if (still_alive) curl_multi_wait(cm, NULL, 0, 1000, NULL);

  } while (still_alive);
}
