#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

/* 回调会在默认主循环里被调用。bytes 所有权归调用方，用完 g_bytes_unref。 */
typedef void (*McDownloaderCallback) (GBytes *bytes, GError *error, gpointer user_data);

void mc_downloader_fetch_string_async (const char          *url,
                                       GCancellable        *cancellable,
                                       McDownloaderCallback callback,
                                       gpointer             user_data);

G_END_DECLS
