#include "mc-instance-manager.h"

#include <glib/gstdio.h>
#include <json-glib/json-glib.h> 
#include <string.h>

struct _McInstanceManager {
  char *base_dir;
};

McInstanceManager *
mc_instance_manager_new (void) {
  McInstanceManager *self = g_new0 (McInstanceManager, 1);
  self->base_dir = g_build_filename (g_get_user_data_dir (),
                                       "mc-launcher", "instances", NULL);
  g_mkdir_with_parents (self->base_dir, 0755);
  return self;
}

void
mc_instance_manager_free (McInstanceManager *self) {
  if (!self) return;
  g_free (self->base_dir);
  g_free (self);
}

const char *
mc_instance_manager_get_base_dir (McInstanceManager *self) {
  return self->base_dir;
}

static char *
safe_dir_name (const char *name) {
  GString *out = g_string_new (NULL);
  for (const char *p = name; *p; p = g_utf8_next_char (p)) {
    gunichar c = g_utf8_get_char (p);
    if (g_unichar_isalnum (c) || c == '-' || c == '_' || c == '.')
      g_string_append_unichar (out, c);
    else
      g_string_append_c (out, '_');
  }
  if (out->len == 0) g_string_append (out, "instance");
  return g_string_free (out, FALSE);
}

static char *
unique_dir_name (McInstanceManager *self, const char *base) {
  g_autofree char *safe = safe_dir_name (base);
  for (int i = 0; i < 1000; i++) {
    g_autofree char *candidate = (i == 0)
        ? g_strdup (safe)
        : g_strdup_printf ("%s-%d", safe, i);
    g_autofree char *full = g_build_filename (self->base_dir, candidate, NULL);
    if (!g_file_test (full, G_FILE_TEST_EXISTS))
      return g_strdup (candidate);
  }
  return g_strdup_printf ("instance-%d", g_random_int ());
}

GListModel *
mc_instance_manager_scan (McInstanceManager *self) {
  GListStore *store = g_list_store_new (MC_TYPE_INSTANCE);

  GDir *d = g_dir_open (self->base_dir, 0, NULL);
  if (d) {
    const char *name;
    while ((name = g_dir_read_name (d)) != NULL) {
      if (name[0] == '.') continue;
      g_autofree char *full = g_build_filename (self->base_dir, name, NULL);
      if (!g_file_test (full, G_FILE_TEST_IS_DIR)) continue;

      g_autoptr (GError) err = NULL;
      McInstance *inst = mc_instance_new_from_dir (full, &err);
      if (inst) {
        g_list_store_append (store, inst);
        g_object_unref (inst);
      }
    }
    g_dir_close (d);
  }

  return G_LIST_MODEL (store);
}

McInstance *
mc_instance_manager_create (McInstanceManager *self,
                             const char *name, const char *mc_version,
                             const char *loader, gboolean installed,
                             GError **error) {
  /* 先检查是否有同名且 installed=false 的未完成目录，有就复用 */
  GDir *scan = g_dir_open (self->base_dir, 0, NULL);
  if (scan) {
    const char *entry;
    while ((entry = g_dir_read_name (scan)) != NULL) {
      if (entry[0] == '.') continue;
      g_autofree char *full = g_build_filename (self->base_dir, entry, NULL);
      if (!g_file_test (full, G_FILE_TEST_IS_DIR)) continue;

      g_autofree char *meta_path = g_build_filename (full, "instance.json", NULL);
      g_autofree char *content = NULL;
      if (!g_file_get_contents (meta_path, &content, NULL, NULL)) continue;

      g_autoptr (JsonParser) parser = json_parser_new ();
      if (!json_parser_load_from_data (parser, content, -1, NULL)) continue;
      JsonObject *o = json_node_get_object (json_parser_get_root (parser));
      if (!o) continue;

      const char *existing_name = json_object_has_member (o, "name")
                                    ? json_object_get_string_member (o, "name") : NULL;
      gboolean existing_installed = json_object_has_member (o, "installed")
                                      ? json_object_get_boolean_member (o, "installed")
                                      : TRUE;

      if (g_strcmp0 (existing_name, name) == 0 && !existing_installed) {
        g_dir_close (scan);
        McInstance *inst = mc_instance_new (entry, name, mc_version, loader, full);
        mc_instance_set_installed (inst, installed);
        mc_instance_save (inst, error);
        return inst;
      }
    }
    g_dir_close (scan);
  }

  /* 没有可复用的，创建新目录 */
  g_autofree char *id = unique_dir_name (self, name);
  g_autofree char *dir = g_build_filename (self->base_dir, id, NULL);

  if (g_mkdir_with_parents (dir, 0755) != 0) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                 "无法创建目录 %s", dir);
    return NULL;
  }

  McInstance *inst = mc_instance_new (id, name, mc_version, loader, dir);
  mc_instance_set_installed (inst, installed);
  mc_instance_ensure_layout (inst);

  if (!mc_instance_save (inst, error)) {
    g_object_unref (inst);
    return NULL;
  }
  return inst;
}

/* 递归复制目录：只用 glib 的 API，避免依赖 rsync/cp */
static gboolean
copy_dir_recursive (const char *src, const char *dst, GError **error) {
  GDir *d = g_dir_open (src, 0, error);
  if (!d) return FALSE;

  if (g_mkdir_with_parents (dst, 0755) != 0) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                 "无法创建 %s", dst);
    g_dir_close (d);
    return FALSE;
  }

  const char *name;
  gboolean ok = TRUE;

  while ((name = g_dir_read_name (d)) != NULL) {
    g_autofree char *src_path = g_build_filename (src, name, NULL);
    g_autofree char *dst_path = g_build_filename (dst, name, NULL);

    if (g_file_test (src_path, G_FILE_TEST_IS_DIR)) {
      if (!copy_dir_recursive (src_path, dst_path, error)) { ok = FALSE; break; }
    } else if (g_file_test (src_path, G_FILE_TEST_IS_REGULAR)) {
      g_autofree char *content = NULL;
      gsize len = 0;
      if (!g_file_get_contents (src_path, &content, &len, error)) {
        ok = FALSE; break;
      }
      if (!g_file_set_contents (dst_path, content, len, error)) {
        ok = FALSE; break;
      }
    }
  }

  g_dir_close (d);
  return ok;
}

McInstance *
mc_instance_manager_import (McInstanceManager *self,
                             const char *src_dir, const char *name,
                             GError **error) {
  g_autofree char *vjson = g_build_filename (src_dir, "version.json", NULL);
  if (!g_file_test (vjson, G_FILE_TEST_EXISTS)) {
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                         "所选目录不是有效的 .minecraft（缺少 version.json）");
    return NULL;
  }

  /* 读取 version.json 拿 id 作为 MC 版本 */
  g_autofree char *content = NULL;
  gsize len = 0;
  g_file_get_contents (vjson, &content, &len, NULL);
  g_autofree char *mc_version = NULL;
  if (content) {
    g_autoptr (JsonParser) parser = json_parser_new ();
    if (json_parser_load_from_data (parser, content, len, NULL)) {
      JsonObject *o = json_node_get_object (json_parser_get_root (parser));
      if (o && json_object_has_member (o, "id"))
        mc_version = g_strdup (json_object_get_string_member (o, "id"));
    }
  }

  McInstance *inst = mc_instance_manager_create (self, name,
                                                  mc_version ? mc_version : "unknown",
                                                  "Vanilla", TRUE, error);
  if (!inst) return NULL;

  /* 复制 .minecraft 内容（跳过 instance.json） */
  GDir *d = g_dir_open (src_dir, 0, error);
  if (!d) {
    g_object_unref (inst);
    return NULL;
  }
  const char *entry;
  gboolean ok = TRUE;

  while ((entry = g_dir_read_name (d)) != NULL) {
    if (g_strcmp0 (entry, "instance.json") == 0) continue;

    g_autofree char *src_path = g_build_filename (src_dir, entry, NULL);
    g_autofree char *dst_path = g_build_filename (mc_instance_get_path (inst), entry, NULL);

    if (g_file_test (src_path, G_FILE_TEST_IS_DIR)) {
      if (!copy_dir_recursive (src_path, dst_path, error)) { ok = FALSE; break; }
    } else if (g_file_test (src_path, G_FILE_TEST_IS_REGULAR)) {
      g_autofree char *c = NULL;
      gsize l = 0;
      if (!g_file_get_contents (src_path, &c, &l, error)) { ok = FALSE; break; }
      if (!g_file_set_contents (dst_path, c, l, error)) { ok = FALSE; break; }
    }
  }
  g_dir_close (d);

  if (!ok) {
    g_object_unref (inst);
    return NULL;
  }

  mc_instance_ensure_layout (inst);
  mc_instance_save (inst, NULL);
  return inst;
}