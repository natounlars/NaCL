#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define MC_TYPE_LIBRARY (mc_library_get_type())
G_DECLARE_FINAL_TYPE (McLibrary, mc_library, MC, LIBRARY, GObject)

McLibrary *mc_library_new       (const char *path, const char *url);
const char *mc_library_get_path (McLibrary *self);
const char *mc_library_get_url  (McLibrary *self);

G_END_DECLS