#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define MC_TYPE_MOD (mc_mod_get_type())
G_DECLARE_FINAL_TYPE (McMod, mc_mod, MC, MOD, GObject)

McMod      *mc_mod_new_from_path    (const char *path);
const char *mc_mod_get_path         (McMod *self);
const char *mc_mod_get_display_name (McMod *self);
gboolean    mc_mod_get_enabled      (McMod *self);

/* 切换启用状态并重命名文件（.jar ↔ .jar.disabled），失败时返回 FALSE */
gboolean    mc_mod_set_enabled      (McMod *self, gboolean enabled);

G_END_DECLS