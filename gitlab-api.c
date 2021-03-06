#include <curl/curl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *urls[] = {
    "https://gitlab.com/api/v4/projects/3472737",
    "https://gitlab.com/api/v4/projects/278964",
};

#define NUM_URLS sizeof(urls) / sizeof(char *)

static size_t write_cb(char *data, size_t n, size_t l, void *userp) {
  /* take care of the data here, ignored in this example */
  (void)data;
  (void)userp;
  fprintf(stderr, "%s: %.*s\n", (char *)userp, (int)(n * l), data);
  return n * l;
}

static void add_transfer(CURLM *cm, int i) {
  CURL *eh = curl_easy_init();
  curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(eh, CURLOPT_URL, urls[i]);
  curl_easy_setopt(eh, CURLOPT_PRIVATE, urls[i]);
  curl_multi_add_handle(cm, eh);
}

int main() {
  CURLM *cm;
  CURLMsg *msg;
  unsigned int transfers = 0;
  int msgs_left = -1;
  int still_alive = 1;

  curl_global_init(CURL_GLOBAL_ALL);
  cm = curl_multi_init();

  /* Limit the amount of simultaneous connections curl should allow: */
  curl_multi_setopt(cm, CURLMOPT_MAXCONNECTS, NUM_URLS);

  for (transfers = 0; transfers < NUM_URLS; transfers++)
    add_transfer(cm, transfers);

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
      if (transfers < NUM_URLS) add_transfer(cm, transfers++);
    }
    if (still_alive) curl_multi_wait(cm, NULL, 0, 1000, NULL);

  } while (still_alive || (transfers < NUM_URLS));
}
