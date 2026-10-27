#include "mc-instance.h"

#include <json-glib/json-glib.h>
#include <glib/gstdio.h>

struct _McInstance {
  GObject parent_instance;
  char *id;
  char *name;
  char *mc_version;
  char *loader;
  char *path;
  gboolean installed;
};

enum {
  PROP_0,
  PROP_ID,
  PROP_NAME,
  PROP_MC_VERSION,
  PROP_LOADER,
  PROP_PATH,
  PROP_INSTALLED,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

G_DEFINE_FINAL_TYPE (McInstance, mc_instance, G_TYPE_OBJECT)

static void
mc_instance_get_property (GObject *obj, guint id, GValue *v, GParamSpec *p) {
  McInstance *self = MC_INSTANCE (obj);
  switch (id) {
    case PROP_ID:         g_value_set_string  (v, self->id);         break;
    case PROP_NAME:       g_value_set_string  (v, self->name);       break;
    case PROP_MC_VERSION: g_value_set_string  (v, self->mc_version); break;
    case PROP_LOADER:     g_value_set_string  (v, self->loader);     break;
    case PROP_PATH:       g_value_set_string  (v, self->path);       break;
    case PROP_INSTALLED:  g_value_set_boolean (v, self->installed);  break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, p);
  }
}

static void
mc_instance_finalize (GObject *obj) {
  McInstance *self = MC_INSTANCE (obj);
  g_free (self->id);
  g_free (self->name);
  g_free (self->mc_version);
  g_free (self->loader);
  g_free (self->path);
  G_OBJECT_CLASS (mc_instance_parent_class)->finalize (obj);
}

static void
mc_instance_class_init (McInstanceClass *k) {
  G_OBJECT_CLASS (k)->finalize     = mc_instance_finalize;
  G_OBJECT_CLASS (k)->get_property = mc_instance_get_property;

  props[PROP_ID]         = g_param_spec_string ("id", NULL, NULL, NULL,
                                                G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  props[PROP_NAME]       = g_param_spec_string ("name", NULL, NULL, NULL,
                                                G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  props[PROP_MC_VERSION] = g_param_spec_string ("mc-version", NULL, NULL, NULL,
                                                G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  props[PROP_LOADER]     = g_param_spec_string ("loader", NULL, NULL, NULL,
                                                G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  props[PROP_PATH]       = g_param_spec_string ("path", NULL, NULL, NULL,
                                                G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  props[PROP_INSTALLED]  = g_param_spec_boolean ("installed", NULL, NULL, TRUE,
                                                 G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (G_OBJECT_CLASS (k), N_PROPS, props);
}

static void mc_instance_init (McInstance *self) {}

McInstance *
mc_instance_new (const char *id, const char *name,
                  const char *mc_version, const char *loader,
                  const char *path) {
  McInstance *self = g_object_new (MC_TYPE_INSTANCE, NULL);
  self->id         = g_strdup (id);
  self->name       = g_strdup (name);
  self->mc_version = g_strdup (mc_version);
  self->loader     = g_strdup (loader);
  self->path       = g_strdup (path);
  self->installed  = TRUE;
  return self;
}

McInstance *
mc_instance_new_from_dir (const char *dir, GError **error) {
  g_autofree char *meta_path = g_build_filename (dir, "instance.json", NULL);
  g_autofree char *content = NULL;

  if (g_file_get_contents (meta_path, &content, NULL, NULL)) {
    g_autoptr (JsonParser) parser = json_parser_new ();
    if (json_parser_load_from_data (parser, content, -1, NULL)) {
      JsonObject *o = json_node_get_object (json_parser_get_root (parser));
      if (o) {
        gboolean installed = TRUE;
        if (json_object_has_member (o, "installed"))
          installed = json_object_get_boolean_member (o, "installed");
        if (!installed)
          return NULL;   /* 下载未完成，扫描时跳过 */

        const char *name = json_object_has_member (o, "name")
                             ? json_object_get_string_member (o, "name") : NULL;
        const char *ver  = json_object_has_member (o, "mc_version")
                             ? json_object_get_string_member (o, "mc_version") : NULL;
        const char *ldr  = json_object_has_member (o, "loader")
                             ? json_object_get_string_member (o, "loader") : NULL;
        g_autofree char *base = g_path_get_basename (dir);
        return mc_instance_new (base,
                                name ? name : base,
                                ver ? ver : "unknown",
                                ldr ? ldr : "Vanilla",
                                dir);
      }
    }
  }

  g_autofree char *vjson = g_build_filename (dir, "version.json", NULL);
  if (!g_file_test (vjson, G_FILE_TEST_EXISTS)) {
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                         "目录中没有 instance.json 也没有 version.json");
    return NULL;
  }

  g_autofree char *base = g_path_get_basename (dir);
  return mc_instance_new (base, base, base, "Vanilla", dir);
}

const char *mc_instance_get_id         (McInstance *self) { return self->id; }
const char *mc_instance_get_name       (McInstance *self) { return self->name; }
const char *mc_instance_get_mc_version (McInstance *self) { return self->mc_version; }
const char *mc_instance_get_loader     (McInstance *self) { return self->loader; }
const char *mc_instance_get_path       (McInstance *self) { return self->path; }
gboolean    mc_instance_get_installed  (McInstance *self) { return self->installed; }

void
mc_instance_set_installed (McInstance *self, gboolean installed) {
  if (self->installed == installed) return;
  self->installed = installed;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_INSTALLED]);
}

gboolean
mc_instance_save (McInstance *self, GError **error) {
  g_autoptr (JsonBuilder) b = json_builder_new ();
  json_builder_begin_object (b);
  json_builder_set_member_name (b, "name");
  json_builder_add_string_value (b, self->name);
  json_builder_set_member_name (b, "mc_version");
  json_builder_add_string_value (b, self->mc_version);
  json_builder_set_member_name (b, "loader");
  json_builder_add_string_value (b, self->loader);
  json_builder_set_member_name (b, "installed");
  json_builder_add_boolean_value (b, self->installed);
  json_builder_end_object (b);

  g_autoptr (JsonGenerator) gen = json_generator_new ();
  json_generator_set_root (gen, json_builder_get_root (b));
  g_autofree char *data = json_generator_to_data (gen, NULL);

  g_autofree char *path = g_build_filename (self->path, "instance.json", NULL);
  return g_file_set_contents (path, data, -1, error);
}

void
mc_instance_ensure_layout (McInstance *self) {
  static const char *dirs[] = {
    "mods", "saves", "resourcepacks", "shaderpacks",
    "logs", "config", "libraries", "assets", "natives", NULL
  };
  for (int i = 0; dirs[i]; i++) {
    g_autofree char *p = g_build_filename (self->path, dirs[i], NULL);
    g_mkdir_with_parents (p, 0755);
  }
}

gboolean
mc_instance_mark_installed (const char *instance_dir) {
  g_autofree char *meta_path = g_build_filename (instance_dir, "instance.json", NULL);
  g_autofree char *content = NULL;
  if (!g_file_get_contents (meta_path, &content, NULL, NULL))
    return FALSE;

  g_autoptr (JsonParser) parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, content, -1, NULL))
    return FALSE;

  JsonNode *root = json_parser_get_root (parser);
  JsonObject *o = json_node_get_object (root);
  json_object_set_boolean_member (o, "installed", TRUE);

  g_autoptr (JsonGenerator) gen = json_generator_new ();
  json_generator_set_root (gen, root);
  g_autofree char *data = json_generator_to_data (gen, NULL);
  return g_file_set_contents (meta_path, data, -1, NULL);
}