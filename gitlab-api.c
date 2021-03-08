#include <assert.h>
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

jsmntok_t *json_tokens;

static int json_eq(const char *json, jsmntok_t *tok, const char *s, u64 s_len) {
  if (tok->type == JSMN_STRING && ((int)s_len == tok->end - tok->start) &&
      strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
    return 0;
  }
  return -1;
}

typedef struct {
  i64 pip_id;
  sds pip_vcs_ref, pip_url, pip_created_at, pip_updated_at, pip_status;
} pipeline_t;

typedef struct {
  i64 pro_id;
  sds pro_name, pro_path_with_namespace, pro_api_url, pro_api_data,
      pro_api_pipelines_url;
  pipeline_t *pro_pipelines;
} project_t;

project_t *projects = NULL;

static void project_init(project_t *project, i64 id) {
  project->pro_id = id;
  project->pro_api_url =
      sdscatprintf(sdsempty(), "https://gitlab.com/api/v4/projects/%lld", id);
  project->pro_api_data = sdsempty();
  project->pro_api_pipelines_url = sdscatprintf(
      sdsempty(), "https://gitlab.com/api/v4/projects/%lld/pipelines", id);
}

static void project_parse_json(project_t *project) {
  buf_clear(json_tokens);

  jsmn_parser parser;
  jsmn_init(&parser);

  const char *const s = project->pro_api_data;
  int res = jsmn_parse(&parser, s, sdslen((char *)s), json_tokens,
                       sdslen(project->pro_api_data));
  if (res <= 0 || json_tokens[0].type != JSMN_OBJECT) {
    fprintf(stderr, "%s:%d:Malformed JSON for project: id=%lld\n", __FILE__,
            __LINE__, project->pro_id);
    return;
  }

  for (i64 i = 1; i < res; i++) {
    jsmntok_t *const tok = &json_tokens[i];
    if (tok->type != JSMN_STRING) continue;

    if (json_eq(s, tok, "name", sizeof("name") - 1) == 0) {
      project->pro_name =
          sdsnewlen(s + json_tokens[i + 1].start,
                    json_tokens[i + 1].end - json_tokens[i + 1].start);
      i++;
    } else if (json_eq(s, tok, "path_with_namespace",
                       sizeof("path_with_namespace") - 1) == 0) {
      project->pro_path_with_namespace =
          sdsnewlen(s + json_tokens[i + 1].start,
                    json_tokens[i + 1].end - json_tokens[i + 1].start);
      i++;
    }
  }
}

static void project_parse_pipelines_json(project_t *project) {
  buf_clear(json_tokens);

  jsmn_parser parser;
  jsmn_init(&parser);

  const char *const s = project->pro_api_data;
  int res = jsmn_parse(&parser, s, sdslen((char *)s), json_tokens,
                       sdslen(project->pro_api_data));
  if (res <= 0 || json_tokens[0].type != JSMN_ARRAY) {
    fprintf(stderr, "%s:%d:Malformed JSON for project: id=%lld\n", __FILE__,
            __LINE__, project->pro_id);
    return;
  }

  pipeline_t *pipeline = NULL;
  for (i64 i = 1; i < res; i++) {
    jsmntok_t *const tok = &json_tokens[i];
    if (tok->type == JSMN_OBJECT) {
      buf_push(project->pro_pipelines, ((pipeline_t){0}));
      pipeline = &project->pro_pipelines[buf_size(project->pro_pipelines) - 1];
      continue;
    }

    if (json_eq(project->pro_api_data, tok, "id", sizeof("id") - 1) == 0) {
      i++;
      const jsmntok_t *const t = &json_tokens[i];
      if (t->type != JSMN_PRIMITIVE) {
        fprintf(stderr, "%s:%d:Malformed JSON for project: id=%lld\n", __FILE__,
                __LINE__, project->pro_id);
        return;
      }

      const char *const value = project->pro_api_data + t->start;
      pipeline->pip_id = strtoll(value, NULL, 10);
      printf("Pipeline id=%lld\n", pipeline->pip_id);
    }
  }
}

static size_t write_cb(char *data, size_t n, size_t l, void *userp) {
  const i64 project_i = (i64)userp;
  project_t *project = &projects[project_i];
  project->pro_api_data = sdscatlen(project->pro_api_data, data, n * l);

  return n * l;
}

static void project_fetch_queue(CURLM *cm, int i) {
  CURL *eh = curl_easy_init();
  curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(eh, CURLOPT_URL, projects[i].pro_api_url);
  curl_easy_setopt(eh, CURLOPT_WRITEDATA, i);
  curl_easy_setopt(eh, CURLOPT_PRIVATE, i);
  curl_multi_add_handle(cm, eh);
}

static void project_pipelines_fetch_queue(CURLM *cm, int i) {
  CURL *eh = curl_easy_init();
  curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(eh, CURLOPT_URL, projects[i].pro_api_pipelines_url);
  curl_easy_setopt(eh, CURLOPT_WRITEDATA, i);
  curl_easy_setopt(eh, CURLOPT_PRIVATE, i);
  curl_multi_add_handle(cm, eh);
}

static void projects_fetch(CURLM *cm) {
  int still_alive = 1;
  int msgs_left = -1;
  do {
    curl_multi_perform(cm, &still_alive);

    CURLMsg *msg;
    while ((msg = curl_multi_info_read(cm, &msgs_left))) {
      CURL *e = msg->easy_handle;
      i64 project_i = 0;
      curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &project_i);

      project_t *project = &projects[project_i];
      if (msg->msg == CURLMSG_DONE) {
        fprintf(stderr, "R: %d - %s\n", msg->data.result,
                curl_easy_strerror(msg->data.result));
        curl_multi_remove_handle(cm, e);
        curl_easy_cleanup(e);
      } else {
        fprintf(stderr, "Failed to fetch from API: id=%lld err=%d\n",
                project->pro_id, msg->msg);
      }
    }
    if (still_alive) curl_multi_wait(cm, NULL, 0, 1000, NULL);

  } while (still_alive);
}

int main() {
  i64 *project_ids = NULL;
  buf_push(project_ids, 3472737);
  buf_push(project_ids, 278964);

  curl_global_init(CURL_GLOBAL_ALL);

  buf_trunc(json_tokens, 10 * 1024);  // 10 KiB

  CURLM *cm;

  // Project
  {
    cm = curl_multi_init();
    for (u64 i = 0; i < buf_size(project_ids); i++) {
      const i64 id = project_ids[i];

      project_t project = {0};
      project_init(&project, id);
      buf_push(projects, project);
      project_fetch_queue(cm, i);
    }
    projects_fetch(cm);
    curl_multi_cleanup(cm);

    for (u64 i = 0; i < buf_size(project_ids); i++) {
      project_t *project = &projects[i];
      project_parse_json(project);
      printf("Project: id=%lld path_with_namespace=%s name=%s\n",
             project->pro_id, project->pro_path_with_namespace,
             project->pro_name);
    }
  }

  // Pipelines
  {
    cm = curl_multi_init();
    for (u64 i = 0; i < buf_size(project_ids); i++) {
      sdsclear(projects[i].pro_api_data);
      project_pipelines_fetch_queue(cm, i);
    }
    projects_fetch(cm);
    curl_multi_cleanup(cm);
    for (u64 i = 0; i < buf_size(project_ids); i++) {
      project_t *project = &projects[i];
      project_parse_pipelines_json(project);
    }
  }
}
