#pragma once

#include <gio/gio.h>
#include "mc-library.h"

G_BEGIN_DECLS

#define MC_TYPE_VERSION_META (mc_version_meta_get_type())
G_DECLARE_FINAL_TYPE (McVersionMeta, mc_version_meta, MC, VERSION_META, GObject)

McVersionMeta *mc_version_meta_parse (const char *json, GError **error);

const char *mc_version_meta_get_id             (McVersionMeta *self);
const char *mc_version_meta_get_main_class     (McVersionMeta *self);
const char *mc_version_meta_get_assets_id      (McVersionMeta *self);
const char *mc_version_meta_get_assets_url     (McVersionMeta *self);
const char *mc_version_meta_get_client_jar_url (McVersionMeta *self);

GPtrArray  *mc_version_meta_get_libraries      (McVersionMeta *self);
/* 返回 GPtrArray<McLibrary*>，path 字段是 natives classifier 名（如 natives-linux） */
GPtrArray  *mc_version_meta_get_natives        (McVersionMeta *self);

G_END_DECLS