#include "mc-version.h"
#include <json-glib/json-glib.h>
#include <string.h>

struct _McVersion {
  GObject parent_instance;
  char *id;
  char *type;
  char *release_time;
  char *url;
  McVersionCategory category;
};

enum {
  PROP_0,
  PROP_ID,
  PROP_TYPE,
  PROP_RELEASE_TIME,
  PROP_URL,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

G_DEFINE_FINAL_TYPE (McVersion, mc_version, G_TYPE_OBJECT)

/* 前向声明：必须先于 mc_version_new 出现 */
static McVersionCategory mc_version_categorize (const char *id,
                                                 const char *type,
                                                 const char *release_time);

static void
mc_version_get_property (GObject *object, guint prop_id,
                          GValue *value, GParamSpec *pspec) {
  McVersion *self = MC_VERSION (object);
  switch (prop_id) {
    case PROP_ID:           g_value_set_string (value, self->id);           break;
    case PROP_TYPE:         g_value_set_string (value, self->type);         break;
    case PROP_RELEASE_TIME: g_value_set_string (value, self->release_time); break;
    case PROP_URL:          g_value_set_string (value, self->url);          break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
mc_version_finalize (GObject *object) {
  McVersion *self = MC_VERSION (object);
  g_free (self->id);
  g_free (self->type);
  g_free (self->release_time);
  g_free (self->url);
  G_OBJECT_CLASS (mc_version_parent_class)->finalize (object);
}

static void
mc_version_class_init (McVersionClass *klass) {
  G_OBJECT_CLASS (klass)->finalize     = mc_version_finalize;
  G_OBJECT_CLASS (klass)->get_property = mc_version_get_property;

  props[PROP_ID]           = g_param_spec_string ("id", NULL, NULL, NULL,
                                                  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  props[PROP_TYPE]         = g_param_spec_string ("type", NULL, NULL, NULL,
                                                  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  props[PROP_RELEASE_TIME] = g_param_spec_string ("release-time", NULL, NULL, NULL,
                                                  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  props[PROP_URL]          = g_param_spec_string ("url", NULL, NULL, NULL,
                                                  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (G_OBJECT_CLASS (klass), N_PROPS, props);
}

static void mc_version_init (McVersion *self) {}

static McVersionCategory
mc_version_categorize (const char *id, const char *type, const char *release_time) {
  /* 远古版：Alpha / Beta */
  if (g_strcmp0 (type, "old_alpha") == 0 || g_strcmp0 (type, "old_beta") == 0)
    return MC_VERSION_CATEGORY_OLD;

  /* 正式版：完全依赖 Mojang 的 type 字段 */
  if (g_strcmp0 (type, "release") == 0)
    return MC_VERSION_CATEGORY_RELEASE;

  /* 剩下的是 snapshot，再细分愚人节 */
  if (g_strcmp0 (type, "snapshot") == 0) {
    /* 精确白名单：只匹配已知的愚人节版本 ID */
    static const char *fool_ids[] = {
      "2.0",           /* 2013 Minecraft 2.0 */
      "15w14a",        /* 2015 爱与拥抱更新 */
      "1.RV-Pre1",     /* 2016 Trendy Update */
      "3D Shareware v1.34", /* 2019 */
      "20w14∞",        /* 2020 */
      "22w13oneblockatatime", /* 2022 */
      "23w13a_or_b",   /* 2023 */
      "24w14potato",   /* 2024 */
      "25w14craftmine",/* 2025 */
      NULL
    };

    for (int i = 0; fool_ids[i]; i++) {
      if (g_strcmp0 (id, fool_ids[i]) == 0)
        return MC_VERSION_CATEGORY_FOOL;
    }

    return MC_VERSION_CATEGORY_SNAPSHOT;
  }

  /* 兜底：未知 type 按快照处理 */
  return MC_VERSION_CATEGORY_SNAPSHOT;
}

McVersion *
mc_version_new (const char *id, const char *type,
                const char *release_time, const char *url) {
  McVersion *self = g_object_new (MC_TYPE_VERSION, NULL);
  self->id           = g_strdup (id);
  self->type         = g_strdup (type);
  self->release_time = g_strdup (release_time);
  self->url          = g_strdup (url);
  self->category     = mc_version_categorize (id, type, release_time);
  return self;
}

const char *mc_version_get_id           (McVersion *self) { return self->id; }
const char *mc_version_get_type_str     (McVersion *self) { return self->type; }
const char *mc_version_get_release_time (McVersion *self) { return self->release_time; }
const char *mc_version_get_url          (McVersion *self) { return self->url; }
McVersionCategory mc_version_get_category (McVersion *self) { return self->category; }

GListModel *
mc_version_manifest_parse (const char *json, GError **error) {
  g_autoptr (JsonParser) parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, json, -1, error))
    return NULL;

  JsonNode *root = json_parser_get_root (parser);
  if (!root || !JSON_NODE_HOLDS_OBJECT (root)) {
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "manifest 根不是对象");
    return NULL;
  }
  JsonObject *obj = json_node_get_object (root);
  if (!json_object_has_member (obj, "versions")) {
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "manifest 缺少 versions 字段");
    return NULL;
  }

  JsonArray *arr = json_object_get_array_member (obj, "versions");
  GListStore *store = g_list_store_new (MC_TYPE_VERSION);
  guint n = json_array_get_length (arr);

  for (guint i = 0; i < n; i++) {
    JsonObject *v = json_array_get_object_element (arr, i);
    const char *id   = json_object_get_string_member (v, "id");
    const char *type = json_object_has_member (v, "type")
                         ? json_object_get_string_member (v, "type") : "unknown";
    const char *rt   = json_object_has_member (v, "releaseTime")
                         ? json_object_get_string_member (v, "releaseTime") : "";
    const char *url  = json_object_has_member (v, "url")
                         ? json_object_get_string_member (v, "url") : "";
    McVersion *ver = mc_version_new (id, type, rt, url);
    g_list_store_append (store, ver);
    g_object_unref (ver);
  }
  return G_LIST_MODEL (store);
}