#include "mc-mod.h"

#include <glib/gstdio.h>
#include <string.h>

struct _McMod {
  GObject parent_instance;
  char   *path;
  char   *display_name;
  gboolean enabled;
};

enum {
  PROP_0,
  PROP_PATH,
  PROP_DISPLAY_NAME,
  PROP_ENABLED,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

G_DEFINE_FINAL_TYPE (McMod, mc_mod, G_TYPE_OBJECT)

static void
mc_mod_get_property (GObject *obj, guint id, GValue *v, GParamSpec *p) {
  McMod *self = MC_MOD (obj);
  switch (id) {
    case PROP_PATH:         g_value_set_string  (v, self->path);         break;
    case PROP_DISPLAY_NAME: g_value_set_string  (v, self->display_name); break;
    case PROP_ENABLED:      g_value_set_boolean (v, self->enabled);      break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, p);
  }
}

static void
mc_mod_finalize (GObject *obj) {
  McMod *self = MC_MOD (obj);
  g_free (self->path);
  g_free (self->display_name);
  G_OBJECT_CLASS (mc_mod_parent_class)->finalize (obj);
}

static void
mc_mod_class_init (McModClass *k) {
  G_OBJECT_CLASS (k)->finalize     = mc_mod_finalize;
  G_OBJECT_CLASS (k)->get_property = mc_mod_get_property;

  props[PROP_PATH] = g_param_spec_string ("path", NULL, NULL, NULL,
                                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  props[PROP_DISPLAY_NAME] = g_param_spec_string ("display-name", NULL, NULL, NULL,
                                                  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  props[PROP_ENABLED] = g_param_spec_boolean ("enabled", NULL, NULL, TRUE,
                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (G_OBJECT_CLASS (k), N_PROPS, props);
}

static void mc_mod_init (McMod *self) {}

McMod *
mc_mod_new_from_path (const char *path) {
  McMod *self = g_object_new (MC_TYPE_MOD, NULL);
  self->path = g_strdup (path);

  const char *base = strrchr (path, '/');
  base = base ? base + 1 : path;

  gboolean disabled = g_str_has_suffix (base, ".jar.disabled");
  self->enabled = !disabled;

  g_autofree char *name = g_strdup (base);
  if (disabled)
    name[strlen (name) - strlen (".disabled")] = '\0';
  self->display_name = g_strdup (name);

  return self;
}

const char *mc_mod_get_path         (McMod *self) { return self->path; }
const char *mc_mod_get_display_name (McMod *self) { return self->display_name; }
gboolean    mc_mod_get_enabled      (McMod *self) { return self->enabled; }

gboolean
mc_mod_set_enabled (McMod *self, gboolean enabled) {
  if (enabled == self->enabled) return TRUE;

  g_autofree char *new_path = NULL;
  if (enabled) {
    gsize len = strlen (self->path);
    new_path = g_strndup (self->path, len - strlen (".disabled"));
  } else {
    new_path = g_strdup_printf ("%s.disabled", self->path);
  }

  if (g_rename (self->path, new_path) != 0) {
    g_warning ("重命名 Mod 失败: %s", self->path);
    return FALSE;
  }

  g_free (self->path);
  self->path = g_steal_pointer (&new_path);
  self->enabled = enabled;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ENABLED]);
  return TRUE;
}