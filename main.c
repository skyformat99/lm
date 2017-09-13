#include "mjs.h"
#include "mongoose.h"

static const char *s_msp_pattern = "**.msp$";
static const char *s_http_port = "8100";
static struct mg_serve_http_opts s_http_server_opts;

const char *mgsptr(const struct mg_str *s) {
  return s == NULL ? NULL : s->p;
}

int mgslen(const struct mg_str *s) {
  return s == NULL ? 0 : s->len;
}

static void msp(struct mg_connection *c, const char *p, size_t len,
                struct mjs *mjs) {
  int i, j, pos = 0;
  mjs_val_t res;

  for (i = 0; i < len; i++) {
    if (p[i] == '{' && p[i + 1] == '{') {
      for (j = i + 1; j < len; j++) {
        if (p[j] == '}' && p[j + 1] == '}') {
          int code_len = j - (i + 2);
          char *code = malloc(code_len + 1);
          mg_send(c, p + pos, i - pos);
          memcpy(code, p + (i + 2), code_len);
          code[code_len] = '\0';
          if (mjs_exec(mjs, code, &res) != MJS_OK) {
            printf("MJS error while executing [%.*s]:\n", code_len, code);
            mjs_print_error(mjs, stdout, NULL, 1);
          }
          free(code);
          pos = j + 2;
          i = pos - 1;
          break;
        }
      }
    }
  }
  if (i > pos) mg_send(c, p + pos, i - pos);
}

// Serve server-side scripted page
static void serve_msp(struct mg_connection *c, struct http_message *hm) {
  extern char *cs_read_file(const char *path, size_t *size);
  char *s, path[100];
  size_t file_len;

  snprintf(path, sizeof(path), "%.*s", (int) hm->uri.len - 1, hm->uri.p + 1);
  if ((s = cs_read_file(path, &file_len)) == NULL) {
    mg_http_send_error(c, 500, "MSP error");
  } else {
    mg_send_response_line(c, 200, "Content-Type: text/html\r\n");
    printf("Serving MSP file [%s]...\n", path);

    // Create mjs instance and register vars
    struct mjs *mjs = mjs_create();
    mjs_val_t v = mjs_mk_object(mjs);
    mjs_set(mjs, v, "hm", ~0, mjs_mk_foreign(mjs, hm));
    mjs_set(mjs, v, "conn", ~0, mjs_mk_foreign(mjs, c));
    mjs_set(mjs, mjs_get_global(mjs), "MSP", ~0, v);

    mjs_exec(mjs, "let _send = ffi('void mg_send(void *, char *, int)')", &v);
    mjs_exec(mjs, "let send = function(s) {_send(MSP.conn, s, s.length);}", &v);

    msp(c, s, file_len, mjs);
    mjs_destroy(mjs);

    printf("done!\n");
    free(s);
    c->flags |= MG_F_SEND_AND_CLOSE;
  }
}

static void ev_handler(struct mg_connection *c, int ev, void *p) {
  struct http_message *hm = (struct http_message *) p;
  switch (ev) {
    case MG_EV_HTTP_REQUEST: {
      struct mg_str pattern = mg_mk_str(s_msp_pattern);
      if (mg_match_prefix_n(pattern, hm->uri) > 0) {
        serve_msp(c, hm);
      } else {
        mg_serve_http(c, hm, s_http_server_opts);
      }
      break;
    }
  }
}

int main(void) {
  struct mg_mgr mgr;
  struct mg_connection *c;

  mg_mgr_init(&mgr, NULL);
  printf("Starting web server on port %s\n", s_http_port);
  c = mg_bind(&mgr, s_http_port, ev_handler);
  if (c == NULL) {
    printf("Failed to create listener\n");
    return 1;
  }

  mg_set_protocol_http_websocket(c);
  s_http_server_opts.document_root = ".";  // Serve current directory
  s_http_server_opts.enable_directory_listing = "yes";

  for (;;) {
    mg_mgr_poll(&mgr, 1000);
  }
  mg_mgr_free(&mgr);

  return 0;
}
