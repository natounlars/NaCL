#pragma once

#include <adwaita.h>
#include "mc-installer.h"

G_BEGIN_DECLS

#define MC_TYPE_DOWNLOAD_WINDOW (mc_download_window_get_type())
G_DECLARE_FINAL_TYPE (McDownloadWindow, mc_download_window, MC,
                       DOWNLOAD_WINDOW, AdwWindow)

McDownloadWindow *mc_download_window_new         (GtkWindow    *parent,
                                                   McInstaller  *installer);
void              mc_download_window_on_progress (McDownloadWindow *self,
                                                   const char *filename,
                                                   gboolean started,
                                                   int completed, int total,
                                                   gint64 received,
                                                   gint64 total_bytes,
                                                   double speed_bps);
void              mc_download_window_on_done     (McDownloadWindow *self,
                                                   GError *error);

G_END_DECLS