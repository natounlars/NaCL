#include "mc-installer.h"
#include "mc-downloader.h"

#include <glib/gstdio.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <string.h>

#define MAX_CONCURRENT 8

typedef enum {
  JOB_FILE,
  JOB_NATIVES,
  JOB_ASSET,
} JobType;

typedef struct {
  char   *url;
  char   *dest;
  JobType type;

  gint64  total_bytes;    /* 来自 Content-Length，-1 表示未知 */
  gint64  received;       /* 已收字节 */
  gint64  start_time;     /* g_get_monotonic_time() */
} Job;

typedef struct {
  McInstaller *self;
  Job         *job;
} JobCtx;

struct _McInstaller {
  McVersionMeta *meta;
  char *instance_dir;

  GPtrArray *jobs;
  guint      next_job;
  guint      active_count;
  guint      completed_count;
  gboolean   finished;
  gboolean   cancelled;
  gboolean   preparing;   /* TRUE 表示正在下载 asset index */

  GCancellable *cancellable;
  SoupSession  *session;

  McInstallerProgressCb progress_cb;
  McInstallerDoneCb     done_cb;
  gpointer              user_data;
};

static void
job_free (gpointer data) {
  Job *j = data;
  g_free (j->url);
  g_free (j->dest);
  g_free (j);
}

McInstaller *
mc_installer_new (McVersionMeta *meta, const char *instance_dir) {
  McInstaller *self = g_new0 (McInstaller, 1);
  self->meta         = g_object_ref (meta);
  self->instance_dir = g_strdup (instance_dir);
  self->jobs         = g_ptr_array_new_with_free_func (job_free);
  self->cancellable  = g_cancellable_new ();
  self->session      = soup_session_new ();
  return self;
}

void
mc_installer_cancel (McInstaller *self) {
  if (self->finished || self->cancelled) return;
  self->cancelled = TRUE;
  g_cancellable_cancel (self->cancellable);
}

void
mc_installer_free (McInstaller *self) {
  if (!self) return;
  if (!self->finished && !self->cancelled)
    mc_installer_cancel (self);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->session);
  g_clear_object (&self->meta);
  g_clear_pointer (&self->jobs, g_ptr_array_unref);
  g_free (self->instance_dir);
  g_free (self);
}

static void
add_job (McInstaller *self, const char *url, const char *dest, JobType type) {
  Job *j = g_new0 (Job, 1);
  j->url  = g_strdup (url);
  j->dest = g_strdup (dest);
  j->type = type;
  g_ptr_array_add (self->jobs, j);
}

static void
add_asset_jobs_from_index (McInstaller *self) {
  const char *assets_id = mc_version_meta_get_assets_id (self->meta);
  if (!assets_id) return;

  g_autofree char *idx_name = g_strdup_printf ("%s.json", assets_id);
  g_autofree char *idx_path = g_build_filename (self->instance_dir,
                                                  "assets", "indexes",
                                                  idx_name, NULL);

  g_autofree char *content = NULL;
  gsize len = 0;
  if (!g_file_get_contents (idx_path, &content, &len, NULL))
    return;

  g_autoptr (JsonParser) parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, content, len, NULL))
    return;

  JsonNode *root_node = json_parser_get_root (parser);
  if (!root_node || !JSON_NODE_HOLDS_OBJECT (root_node))
    return;

  JsonObject *root = json_node_get_object (root_node);
  if (!json_object_has_member (root, "objects"))
    return;

  JsonObject *objects = json_object_get_object_member (root, "objects");
  GList *keys = json_object_get_members (objects);

  for (GList *l = keys; l; l = l->next) {
    const char *name = l->data;
    if (!json_object_has_member (objects, name))
      continue;
    JsonObject *obj = json_object_get_object_member (objects, name);
    if (!obj || !json_object_has_member (obj, "hash"))
      continue;

    const char *hash = json_object_get_string_member (obj, "hash");
    if (!hash || strlen (hash) < 2)
      continue;

    g_autofree char *prefix = g_strndup (hash, 2);
    g_autofree char *dest = g_build_filename (self->instance_dir,
                                                "assets", "objects",
                                                prefix, hash, NULL);

    /* 同 hash 已被其他版本下载过，直接跳过 */
    if (g_file_test (dest, G_FILE_TEST_EXISTS))
      continue;

    g_autofree char *url = g_strdup_printf (
        "https://resources.download.minecraft.net/%s/%s", prefix, hash);

    add_job (self, url, dest, JOB_ASSET);
  }

  g_list_free (keys);
}

static void
build_download_jobs (McInstaller *self) {
  GPtrArray *libs = mc_version_meta_get_libraries (self->meta);
  for (guint i = 0; i < libs->len; i++) {
    McLibrary *l = g_ptr_array_index (libs, i);
    g_autofree char *dest = g_build_filename (self->instance_dir,
                                                "libraries",
                                                mc_library_get_path (l), NULL);
    add_job (self, mc_library_get_url (l), dest, JOB_FILE);
  }

  GPtrArray *nats = mc_version_meta_get_natives (self->meta);
  for (guint i = 0; i < nats->len; i++) {
    McLibrary *n = g_ptr_array_index (nats, i);
    g_autofree char *dest = g_build_filename (self->instance_dir,
                                                "natives_temp",
                                                mc_library_get_path (n), NULL);
    add_job (self, mc_library_get_url (n), dest, JOB_NATIVES);
  }

  const char *client_url = mc_version_meta_get_client_jar_url (self->meta);
  if (client_url) {
    g_autofree char *dest = g_build_filename (self->instance_dir,
                                                "client.jar", NULL);
    add_job (self, client_url, dest, JOB_FILE);
  }
}

static gboolean
extract_natives (McInstaller *self, GError **error) {
  g_autofree char *natives_dir = g_build_filename (self->instance_dir,
                                                     "natives", NULL);
  g_mkdir_with_parents (natives_dir, 0755);

  gboolean any = FALSE;
  for (guint i = 0; i < self->jobs->len; i++) {
    Job *j = g_ptr_array_index (self->jobs, i);
    if (j->type != JOB_NATIVES) continue;
    any = TRUE;

    const char *argv[] = {
      "unzip", "-o", "-j", "-q", j->dest, "-d", natives_dir, NULL
    };
    g_autoptr (GError) local_err = NULL;
    GSubprocess *proc = g_subprocess_newv (
        argv,
        G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
        &local_err);
    if (!proc) {
      g_propagate_error (error, g_steal_pointer (&local_err));
      return FALSE;
    }
    if (!g_subprocess_wait_check (proc, NULL, error)) {
      g_object_unref (proc);
      return FALSE;
    }
    g_object_unref (proc);
    g_unlink (j->dest);
  }

  if (any) {
    g_autofree char *tmp = g_build_filename (self->instance_dir,
                                               "natives_temp", NULL);
    g_rmdir (tmp);
  }
  return TRUE;
}

static void
check_completion (McInstaller *self) {
  if (self->finished) return;
  if (self->preparing) return;
  if (self->active_count > 0) return;
  if (!self->cancelled && self->next_job < self->jobs->len) return;

  self->finished = TRUE;

  if (self->cancelled) {
    GError *err = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED, "已取消");
    if (self->done_cb) self->done_cb (err, self->user_data);
    g_error_free (err);
    return;
  }

  GError *err = NULL;
  if (!extract_natives (self, &err)) {
    if (self->done_cb) self->done_cb (err, self->user_data);
    g_error_free (err);
    return;
  }
  if (self->done_cb) self->done_cb (NULL, self->user_data);
}

static void on_job_done         (GObject *src, GAsyncResult *res, gpointer data);
static void on_asset_index_done (GObject *src, GAsyncResult *res, gpointer data);
static void pump                (McInstaller *self);

static void
on_got_headers (SoupMessage *msg, JobCtx *ctx) {
  Job *job = ctx->job;
  SoupMessageHeaders *h = soup_message_get_response_headers (msg);
  gint64 len = soup_message_headers_get_content_length (h);
  if (len > 0) job->total_bytes = len;
}

static void
on_body_data (SoupMessage *msg, guint chunk_size, JobCtx *ctx) {
  McInstaller *self = ctx->self;
  Job *job = ctx->job;

  if (job->total_bytes <= 0) {
    SoupMessageHeaders *h = soup_message_get_response_headers (msg);
    gint64 len = soup_message_headers_get_content_length (h);
    if (len > 0) job->total_bytes = len;
  }

  job->received += chunk_size;

  gint64 now = g_get_monotonic_time ();
  double elapsed = (now - job->start_time) / 1e6;
  double speed = elapsed > 0.1 ? (double) job->received / elapsed : 0.0;

  if (self->progress_cb)
    self->progress_cb (job->dest, TRUE,
                        self->completed_count, self->jobs->len,
                        job->received, job->total_bytes, speed,
                        self->user_data);
}

static void
start_job (McInstaller *self, Job *job) {
  JobCtx *ctx = g_new0 (JobCtx, 1);
  ctx->self = self;
  ctx->job  = job;

  job->received    = 0;
  job->total_bytes = -1;
  job->start_time  = g_get_monotonic_time ();

  SoupMessage *msg = soup_message_new ("GET", job->url);
  if (!msg) {
    g_free (ctx);
    self->active_count--;
    self->cancelled = TRUE;
    check_completion (self);
    return;
  }

  g_signal_connect (msg, "got-headers",   G_CALLBACK (on_got_headers), ctx);
  g_signal_connect (msg, "got-body-data", G_CALLBACK (on_body_data),   ctx);

  if (self->progress_cb)
    self->progress_cb (job->dest, TRUE,
                        self->completed_count, self->jobs->len,
                        0, -1, 0.0, self->user_data);

  soup_session_send_and_read_async (self->session, msg, G_PRIORITY_DEFAULT,
                                     self->cancellable, on_job_done, ctx);
  g_object_unref (msg);
}

static void
pump (McInstaller *self) {
  if (self->finished || self->cancelled) {
    check_completion (self);
    return;
  }

  while (self->active_count < MAX_CONCURRENT &&
         self->next_job < self->jobs->len) {
    Job *job = g_ptr_array_index (self->jobs, self->next_job);
    self->next_job++;
    self->active_count++;
    start_job (self, job);
  }

  if (self->active_count == 0 && self->next_job >= self->jobs->len)
    check_completion (self);
}

static void
on_job_done (GObject *src, GAsyncResult *res, gpointer data) {
  JobCtx *ctx = data;
  McInstaller *self = ctx->self;
  Job *job = ctx->job;
  g_free (ctx);

  GError *error = NULL;
  GBytes *bytes = soup_session_send_and_read_finish (SOUP_SESSION (src),
                                                      res, &error);

  self->active_count--;

  if (self->cancelled ||
      g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    if (error) g_error_free (error);
    if (bytes) g_bytes_unref (bytes);
    check_completion (self);
    return;
  }

  if (error || !bytes) {
    self->cancelled = TRUE;
    if (self->done_cb) {
      g_autofree char *msg = g_strdup_printf ("下载失败：%s",
                                               error ? error->message : "未知");
      GError *e = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_FAILED, msg);
      self->done_cb (e, self->user_data);
      g_error_free (e);
    }
    self->finished = TRUE;
    if (error) g_error_free (error);
    if (bytes) g_bytes_unref (bytes);
    return;
  }

  g_autofree char *parent = g_path_get_dirname (job->dest);
  g_mkdir_with_parents (parent, 0755);

  gsize len;
  const void *raw = g_bytes_get_data (bytes, &len);
  gboolean wrote = g_file_set_contents (job->dest, raw, len, NULL);
  g_bytes_unref (bytes);

  if (!wrote) {
    self->cancelled = TRUE;
    if (self->done_cb) {
      g_autofree char *msg = g_strdup_printf ("写入失败：%s", job->dest);
      GError *e = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_FAILED, msg);
      self->done_cb (e, self->user_data);
      g_error_free (e);
    }
    self->finished = TRUE;
    return;
  }

  self->completed_count++;

    if (self->progress_cb)
    self->progress_cb (job->dest, FALSE,
                       self->completed_count, self->jobs->len,
                       job->received, job->total_bytes, 0.0,
                       self->user_data);

  pump (self);
}

static void
on_asset_index_done (GObject *src, GAsyncResult *res, gpointer data) {
  McInstaller *self = data;
  GError *error = NULL;
  GBytes *bytes = soup_session_send_and_read_finish (SOUP_SESSION (src),
                                                      res, &error);

  if (self->cancelled ||
      g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    if (error) g_error_free (error);
    if (bytes) g_bytes_unref (bytes);
    self->preparing = FALSE;
    check_completion (self);
    return;
  }

  if (error || !bytes) {
    /* asset index 失败不致命，客户端仍可离线启动，只是缺音效与语言 */
    g_warning ("获取资源索引失败: %s", error ? error->message : "未知");
    if (error) g_error_free (error);
    if (bytes) g_bytes_unref (bytes);
    self->preparing = FALSE;
    build_download_jobs (self);
    pump (self);
    return;
  }

  const char *assets_id = mc_version_meta_get_assets_id (self->meta);
  if (assets_id) {
    g_autofree char *idx_name = g_strdup_printf ("%s.json", assets_id);
    g_autofree char *idx_path = g_build_filename (self->instance_dir,
                                                    "assets", "indexes",
                                                    idx_name, NULL);
    g_autofree char *parent = g_path_get_dirname (idx_path);
    g_mkdir_with_parents (parent, 0755);

    gsize len;
    const void *raw = g_bytes_get_data (bytes, &len);
    g_file_set_contents (idx_path, raw, len, NULL);
  }
  g_bytes_unref (bytes);

  /* 先把元数据 job 排好，再追加 assets，进度显示更有条理 */
  build_download_jobs (self);
  add_asset_jobs_from_index (self);
  self->preparing = FALSE;
  pump (self);
}

static void
start_asset_index_download (McInstaller *self) {
  const char *assets_url = mc_version_meta_get_assets_url (self->meta);
  const char *assets_id  = mc_version_meta_get_assets_id (self->meta);

  if (!assets_url || !assets_id) {
    self->preparing = FALSE;
    build_download_jobs (self);
    pump (self);
    return;
  }

  self->preparing = TRUE;
  SoupMessage *msg = soup_message_new ("GET", assets_url);
  if (!msg) {
    self->preparing = FALSE;
    build_download_jobs (self);
    pump (self);
    return;
  }

  soup_session_send_and_read_async (self->session, msg, G_PRIORITY_DEFAULT,
                                     self->cancellable, on_asset_index_done, self);
  g_object_unref (msg);
}

void
mc_installer_start (McInstaller          *self,
                     McInstallerProgressCb progress_cb,
                     McInstallerDoneCb     done_cb,
                     gpointer              user_data) {
  self->progress_cb = progress_cb;
  self->done_cb     = done_cb;
  self->user_data   = user_data;

  if (progress_cb)
    progress_cb ("准备中…", TRUE, 0, 0, 0, -1, 0.0, user_data);

  start_asset_index_download (self);
}
