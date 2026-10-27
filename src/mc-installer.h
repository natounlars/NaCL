#pragma once

#include <gio/gio.h>
#include "mc-version-meta.h"

G_BEGIN_DECLS

typedef struct _McInstaller McInstaller;

/* started=TRUE 开始/更新，FALSE 表示任务完成。
 * received/total_bytes 为该文件已收/总字节，total_bytes<=0 表示未知。
 * speed_bps 为当前瞬时速度。 */
typedef void (*McInstallerProgressCb) (const char *filename, gboolean started,
                                        int completed, int total,
                                        gint64 received, gint64 total_bytes,
                                        double speed_bps,
                                        gpointer user_data);
typedef void (*McInstallerDoneCb)     (GError *error, gpointer user_data);

McInstaller *mc_installer_new    (McVersionMeta *meta, const char *instance_dir);
void         mc_installer_start  (McInstaller          *self,
                                   McInstallerProgressCb progress_cb,
                                   McInstallerDoneCb     done_cb,
                                   gpointer              user_data);
void         mc_installer_cancel (McInstaller *self);
void         mc_installer_free   (McInstaller *self);

G_END_DECLS