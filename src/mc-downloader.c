#include "mc-downloader.h"

#include <libsoup/soup.h>

typedef struct {
  McDownloaderCallback callback;
  gpointer             user_data;
} FetchCtx;

static void
on_read_finished (GObject *source, GAsyncResult *result, gpointer user_data) {
  FetchCtx *ctx = user_data;
  GError *error = NULL;
  GBytes *bytes = soup_session_send_and_read_finish (SOUP_SESSION (source),
                                                      result, &error);

  ctx->callback (bytes, error, ctx->user_data);

  if (bytes) g_bytes_unref (bytes);
  if (error) g_error_free (error);
  g_free (ctx);
}

void
mc_downloader_fetch_string_async (const char          *url,
                                   GCancellable        *cancellable,
                                   McDownloaderCallback callback,
                                   gpointer             user_data) {
  SoupSession *session = soup_session_new ();
  SoupMessage *msg     = soup_message_new ("GET", url);

  if (!msg) {
    GError *err = g_error_new (G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                               "无效 URL: %s", url);
    callback (NULL, err, user_data);
    g_error_free (err);
    g_object_unref (session);
    return;
  }

  FetchCtx *ctx = g_new0 (FetchCtx, 1);
  ctx->callback  = callback;
  ctx->user_data = user_data;

  soup_session_send_and_read_async (session, msg, G_PRIORITY_DEFAULT,
                                     cancellable, on_read_finished, ctx);

  g_object_unref (msg);
  g_object_unref (session);   /* 异步操作内部持有引用，安全 */
}