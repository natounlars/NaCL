#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define MC_TYPE_INSTANCE (mc_instance_get_type())
G_DECLARE_FINAL_TYPE (McInstance, mc_instance, MC, INSTANCE, GObject)

McInstance *mc_instance_new          (const char *id,
                                       const char *name,
                                       const char *mc_version,
                                       const char *loader,
                                       const char *path);
McInstance *mc_instance_new_from_dir (const char *dir, GError **error);

const char *mc_instance_get_id         (McInstance *self);
const char *mc_instance_get_name       (McInstance *self);
const char *mc_instance_get_mc_version (McInstance *self);
const char *mc_instance_get_loader     (McInstance *self);
const char *mc_instance_get_path       (McInstance *self);

gboolean mc_instance_get_installed (McInstance *self);
void     mc_instance_set_installed (McInstance *self, gboolean installed);

gboolean mc_instance_save (McInstance *self, GError **error);

void mc_instance_ensure_layout (McInstance *self);

/* 直接操作磁盘：把指定目录 instance.json 的 installed 字段置 TRUE */
gboolean mc_instance_mark_installed (const char *instance_dir);

G_END_DECLS