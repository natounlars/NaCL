#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum {
  MC_VERSION_CATEGORY_RELEASE,
  MC_VERSION_CATEGORY_SNAPSHOT,
  MC_VERSION_CATEGORY_OLD,
  MC_VERSION_CATEGORY_FOOL,
} McVersionCategory;

#define MC_TYPE_VERSION (mc_version_get_type())
G_DECLARE_FINAL_TYPE (McVersion, mc_version, MC, VERSION, GObject)

McVersion *mc_version_new (const char *id,
                            const char *type,
                            const char *release_time,
                            const char *url);

const char *mc_version_get_id           (McVersion *self);
const char *mc_version_get_type_str     (McVersion *self);
const char *mc_version_get_release_time (McVersion *self);
const char *mc_version_get_url          (McVersion *self);
McVersionCategory mc_version_get_category (McVersion *self);

GListModel *mc_version_manifest_parse (const char *json, GError **error);

G_END_DECLS