#include "mc-library.h"

struct _McLibrary {
  GObject parent_instance;
  char *path;
  char *url;
};

enum { PROP_0, PROP_PATH, PROP_URL, N_PROPS };
static GParamSpec *props[N_PROPS];

G_DEFINE_FINAL_TYPE (McLibrary, mc_library, G_TYPE_OBJECT)

static void
mc_library_get_property (GObject *obj, guint id, GValue *v, GParamSpec *p) {
  McLibrary *self = MC_LIBRARY (obj);
  switch (id) {
    case PROP_PATH: g_value_set_string (v, self->path); break;
    case PROP_URL:  g_value_set_string (v, self->url);  break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, p);
  }
}

static void
mc_library_finalize (GObject *obj) {
  McLibrary *self = MC_LIBRARY (obj);
  g_free (self->path);
  g_free (self->url);
  G_OBJECT_CLASS (mc_library_parent_class)->finalize (obj);
}

static void
mc_library_class_init (McLibraryClass *k) {
  G_OBJECT_CLASS (k)->finalize     = mc_library_finalize;
  G_OBJECT_CLASS (k)->get_property = mc_library_get_property;
  props[PROP_PATH] = g_param_spec_string ("path", NULL, NULL, NULL,
                                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  props[PROP_URL]  = g_param_spec_string ("url", NULL, NULL, NULL,
                                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (G_OBJECT_CLASS (k), N_PROPS, props);
}

static void mc_library_init (McLibrary *self) {}

McLibrary *
mc_library_new (const char *path, const char *url) {
  McLibrary *self = g_object_new (MC_TYPE_LIBRARY, NULL);
  self->path = g_strdup (path);
  self->url  = g_strdup (url);
  return self;
}

const char *mc_library_get_path (McLibrary *self) { return self->path; }
const char *mc_library_get_url  (McLibrary *self) { return self->url; }