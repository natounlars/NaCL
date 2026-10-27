#include "mc-version-meta.h"
#include <json-glib/json-glib.h>
#include <string.h>

struct _McVersionMeta {
  GObject parent_instance;
  char *id;
  char *main_class;
  char *assets_id;
  char *assets_url;
  char *client_jar_url;
  GPtrArray *libraries;
  GPtrArray *natives;
};

enum {
  PROP_0, PROP_ID, PROP_MAIN_CLASS, PROP_ASSETS_ID,
  PROP_ASSETS_URL, PROP_CLIENT_JAR_URL, N_PROPS
};

static GParamSpec *props[N_PROPS];

G_DEFINE_FINAL_TYPE (McVersionMeta, mc_version_meta, G_TYPE_OBJECT)

static void
mc_version_meta_get_property (GObject *obj, guint id, GValue *v, GParamSpec *p) {
  McVersionMeta *self = MC_VERSION_META (obj);
  switch (id) {
    case PROP_ID:             g_value_set_string (v, self->id);             break;
    case PROP_MAIN_CLASS:     g_value_set_string (v, self->main_class);     break;
    case PROP_ASSETS_ID:      g_value_set_string (v, self->assets_id);      break;
    case PROP_ASSETS_URL:     g_value_set_string (v, self->assets_url);     break;
    case PROP_CLIENT_JAR_URL: g_value_set_string (v, self->client_jar_url); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, p);
  }
}

static void
mc_version_meta_finalize (GObject *obj) {
  McVersionMeta *self = MC_VERSION_META (obj);
  g_free (self->id);
  g_free (self->main_class);
  g_free (self->assets_id);
  g_free (self->assets_url);
  g_free (self->client_jar_url);
  g_clear_pointer (&self->libraries, g_ptr_array_unref);
  g_clear_pointer (&self->natives,   g_ptr_array_unref);
  G_OBJECT_CLASS (mc_version_meta_parent_class)->finalize (obj);
}

static void
mc_version_meta_class_init (McVersionMetaClass *k) {
  G_OBJECT_CLASS (k)->finalize     = mc_version_meta_finalize;
  G_OBJECT_CLASS (k)->get_property = mc_version_meta_get_property;
  props[PROP_ID]             = g_param_spec_string ("id", NULL, NULL, NULL,
                                                    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  props[PROP_MAIN_CLASS]     = g_param_spec_string ("main-class", NULL, NULL, NULL,
                                                    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  props[PROP_ASSETS_ID]      = g_param_spec_string ("assets-id", NULL, NULL, NULL,
                                                    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  props[PROP_ASSETS_URL]     = g_param_spec_string ("assets-url", NULL, NULL, NULL,
                                                    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  props[PROP_CLIENT_JAR_URL] = g_param_spec_string ("client-jar-url", NULL, NULL, NULL,
                                                    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (G_OBJECT_CLASS (k), N_PROPS, props);
}

static void mc_version_meta_init (McVersionMeta *self) {}

static char *
compute_path_from_name (const char *name) {
  g_auto (GStrv) parts = g_strsplit (name, ":", -1);
  if (g_strv_length (parts) < 3) return NULL;
  g_autofree char *group = g_strdup (parts[0]);
  for (char *p = group; *p; p++) if (*p == '.') *p = '/';
  return g_strdup_printf ("%s/%s/%s/%s-%s.jar",
                          group, parts[1], parts[2], parts[1], parts[2]);
}

/* ${arch} → 64 / 32 */
static char *
resolve_arch (const char *classifier) {
  if (!strstr (classifier, "${arch}"))
    return g_strdup (classifier);

  const char *arch = (sizeof (void *) == 8) ? "64" : "32";
  g_auto (GStrv) parts = g_strsplit (classifier, "${arch}", -1);
  return g_strjoinv (arch, parts);
}

static const char *
current_os_key (void) {
#ifdef __linux__
  return "linux";
#elif defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "osx";
#else
  return "linux";
#endif
}

McVersionMeta *
mc_version_meta_parse (const char *json, GError **error) {
  g_autoptr (JsonParser) parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, json, -1, error))
    return NULL;

  JsonNode *root = json_parser_get_root (parser);
  if (!root || !JSON_NODE_HOLDS_OBJECT (root)) {
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "版本 JSON 根不是对象");
    return NULL;
  }

  JsonObject *obj = json_node_get_object (root);
  McVersionMeta *self = g_object_new (MC_TYPE_VERSION_META, NULL);
  self->libraries = g_ptr_array_new_with_free_func (g_object_unref);
  self->natives   = g_ptr_array_new_with_free_func (g_object_unref);

  if (json_object_has_member (obj, "id"))
    self->id = g_strdup (json_object_get_string_member (obj, "id"));
  if (json_object_has_member (obj, "mainClass"))
    self->main_class = g_strdup (json_object_get_string_member (obj, "mainClass"));

  if (json_object_has_member (obj, "assetIndex")) {
    JsonObject *ai = json_object_get_object_member (obj, "assetIndex");
    if (json_object_has_member (ai, "id"))
      self->assets_id = g_strdup (json_object_get_string_member (ai, "id"));
    if (json_object_has_member (ai, "url"))
      self->assets_url = g_strdup (json_object_get_string_member (ai, "url"));
  }

  if (json_object_has_member (obj, "downloads")) {
    JsonObject *dl = json_object_get_object_member (obj, "downloads");
    if (json_object_has_member (dl, "client")) {
      JsonObject *c = json_object_get_object_member (dl, "client");
      if (json_object_has_member (c, "url"))
        self->client_jar_url = g_strdup (json_object_get_string_member (c, "url"));
    }
  }

  const char *os_key = current_os_key ();

  if (json_object_has_member (obj, "libraries")) {
    JsonArray *arr = json_object_get_array_member (obj, "libraries");
    guint n = json_array_get_length (arr);

    for (guint i = 0; i < n; i++) {
      JsonObject *lib = json_array_get_object_element (arr, i);
      JsonObject *d = json_object_has_member (lib, "downloads")
                        ? json_object_get_object_member (lib, "downloads") : NULL;
      if (!d) continue;

      /* 主 artifact */
      if (json_object_has_member (d, "artifact")) {
        JsonObject *art = json_object_get_object_member (d, "artifact");
        if (json_object_has_member (art, "url")) {
          const char *url  = json_object_get_string_member (art, "url");
          const char *path = json_object_has_member (art, "path")
                               ? json_object_get_string_member (art, "path")
                               : NULL;
          g_autofree char *path_owned = NULL;
          if (!path && json_object_has_member (lib, "name")) {
            path_owned = compute_path_from_name (
                json_object_get_string_member (lib, "name"));
            path = path_owned;
          }
          if (url && path)
            g_ptr_array_add (self->libraries, mc_library_new (path, url));
        }
      }

      /* natives classifier */
      if (json_object_has_member (lib, "natives") &&
          json_object_has_member (d, "classifiers")) {
        JsonObject *nat = json_object_get_object_member (lib, "natives");
        if (json_object_has_member (nat, os_key)) {
          const char *raw_cls = json_object_get_string_member (nat, os_key);
          g_autofree char *cls = resolve_arch (raw_cls);

          JsonObject *clss = json_object_get_object_member (d, "classifiers");
          if (json_object_has_member (clss, cls)) {
            JsonObject *art = json_object_get_object_member (clss, cls);
            if (json_object_has_member (art, "url")) {
              const char *url = json_object_get_string_member (art, "url");
              g_ptr_array_add (self->natives, mc_library_new (cls, url));
            }
          }
        }
      }
    }
  }

  return self;
}

const char *mc_version_meta_get_id             (McVersionMeta *self) { return self->id; }
const char *mc_version_meta_get_main_class     (McVersionMeta *self) { return self->main_class; }
const char *mc_version_meta_get_assets_id      (McVersionMeta *self) { return self->assets_id; }
const char *mc_version_meta_get_assets_url     (McVersionMeta *self) { return self->assets_url; }
const char *mc_version_meta_get_client_jar_url (McVersionMeta *self) { return self->client_jar_url; }
GPtrArray  *mc_version_meta_get_libraries      (McVersionMeta *self) { return self->libraries; }
GPtrArray  *mc_version_meta_get_natives        (McVersionMeta *self) { return self->natives; }