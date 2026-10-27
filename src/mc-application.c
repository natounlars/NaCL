#include "mc-application.h"
#include "mc-instance.h"
#include "mc-version.h"
#include "mc-mod.h"
#include "mc-downloader.h"
#include "mc-version-meta.h"
#include "mc-installer.h"
#include "mc-launcher.h"
#include "mc-download-window.h"
#include "mc-instance-manager.h"
#include "mc-instance-window.h"
#include "mc-auth.h"
#include "mc-auth-window.h"

#include <adwaita.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <string.h>

struct _McApplication {
  AdwApplication parent_instance;
  McConfig  *config;
  GtkWidget *window;

  /* 启动页 */
  AdwComboRow  *instance_combo;
  AdwComboRow  *java_combo;
  AdwSpinRow   *memory_spin;
  AdwSwitchRow *isolation_switch;

  /* Mod 页 */
  AdwActionRow        *mod_current_instance_row;
  AdwEntryRow         *mod_search;
  AdwPreferencesGroup *mod_list_group;

  /* 下载页 */
  AdwEntryRow         *release_search;
  AdwEntryRow         *snapshot_search;
  AdwEntryRow         *old_search;
  AdwEntryRow         *fool_search;
  AdwPreferencesGroup *release_versions_group;
  AdwPreferencesGroup *snapshot_versions_group;
  AdwPreferencesGroup *old_versions_group;
  AdwPreferencesGroup *fool_versions_group;
  AdwToastOverlay     *toast_overlay;

  /* 安装状态 */
  McInstaller      *current_installer;
  McDownloadWindow *download_window;
  char *installing_instance_path;

  /* 数据 */
  GListModel *version_list;
  GPtrArray  *mods;

  McInstanceManager *instance_manager;
  McInstanceWindow  *instance_window;
  char              *current_instance_id;

   /* 微软登录 */
  AdwActionRow *account_row;
  McAuthWindow *auth_window;
  char         *mc_access_token;
  char         *player_uuid;
  char         *player_name;
};

G_DEFINE_FINAL_TYPE (McApplication, mc_application, ADW_TYPE_APPLICATION)

/* ---------- 前向声明 ---------- */
static void refresh_mods                     (McApplication *self);
static void on_instance_selected_from_window (McInstance *inst, McApplication *self);
static void on_install_version_clicked       (GtkButton *btn, McApplication *self);
static void on_version_json_loaded           (GBytes *bytes, GError *error, gpointer user_data);

/* ========== 通用工具 ========== */

static void
clear_preferences_group (AdwPreferencesGroup *group) {
  GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (group));
  while (child) {
    GtkWidget *next = gtk_widget_get_next_sibling (child);
    adw_preferences_group_remove (group, child);
    child = next;
  }
}

/* ========== 启动页 ========== */

static McInstance *
get_selected_instance (McApplication *self) {
  gpointer item = adw_combo_row_get_selected_item (self->instance_combo);
  return item ? MC_INSTANCE (g_object_ref (item)) : NULL;
}

static void
refresh_instance_model (McApplication *self) {
  GListModel *m = mc_instance_manager_scan (self->instance_manager);
  adw_combo_row_set_model (self->instance_combo, m);

  GListModel *model = m;
  guint n = g_list_model_get_n_items (model);
  guint target = GTK_INVALID_LIST_POSITION;

  if (self->current_instance_id) {
    for (guint i = 0; i < n; i++) {
      g_autoptr (McInstance) inst = g_list_model_get_item (model, i);
      if (g_strcmp0 (mc_instance_get_id (inst), self->current_instance_id) == 0) {
        target = i;
        break;
      }
    }
  }
  if (target == GTK_INVALID_LIST_POSITION && n > 0)
    target = 0;

  /* 先清空再设置，强制 AdwComboRow 重新 emit notify::selected */
  adw_combo_row_set_selected (self->instance_combo, GTK_INVALID_LIST_POSITION);
  if (target != GTK_INVALID_LIST_POSITION)
    adw_combo_row_set_selected (self->instance_combo, target);

  g_object_unref (m);
}

static void
on_instance_changed (AdwComboRow *row, GParamSpec *pspec, McApplication *self) {
  McInstance *inst = get_selected_instance (self);
  if (!inst) return;

  const char *id = mc_instance_get_id (inst);

  g_free (self->current_instance_id);
  self->current_instance_id = g_strdup (id);

  adw_switch_row_set_active (self->isolation_switch,
                             mc_config_instance_is_isolated (self->config, id));
  adw_spin_row_set_value (self->memory_spin,
                          mc_config_get_max_memory_for_instance (self->config, id));

  if (self->mod_current_instance_row) {
    g_autofree char *sub = g_strdup_printf ("%s · %s",
                                             mc_instance_get_mc_version (inst),
                                             mc_instance_get_loader (inst));
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->mod_current_instance_row),
                                    mc_instance_get_name (inst));
    adw_action_row_set_subtitle (self->mod_current_instance_row, sub);
  }

  refresh_mods (self);
  g_object_unref (inst);
}

static void
on_manage_instances_activated (AdwActionRow *row, McApplication *self) {
  if (self->instance_window) {
    gtk_window_present (GTK_WINDOW (self->instance_window));
    return;
  }
  self->instance_window = mc_instance_window_new (
      GTK_WINDOW (self->window), self->instance_manager,
      (McInstanceWindowSelectedCb) on_instance_selected_from_window, self);
  /* 窗口销毁时自动把指针置 NULL，防止悬空 */
  g_object_add_weak_pointer (G_OBJECT (self->instance_window),
                              (gpointer *) &self->instance_window);
  gtk_window_present (GTK_WINDOW (self->instance_window));
}

static void
on_instance_selected_from_window (McInstance *inst, McApplication *self) {
  g_free (self->current_instance_id);
  self->current_instance_id = g_strdup (mc_instance_get_id (inst));
  refresh_instance_model (self);
}

static gboolean
free_installer_idle (gpointer data) {
  mc_installer_free (data);
  return G_SOURCE_REMOVE;
}

static void
on_launch_clicked (GtkButton *button, McApplication *self) {
  McInstance *inst = get_selected_instance (self);
  if (!inst) {
    adw_toast_overlay_add_toast (self->toast_overlay,
                                  adw_toast_new ("请先选择一个实例"));
    return;
  }

  const char *id = mc_instance_get_id (inst);
  g_autofree char *java = mc_config_get_java_path_for_instance (self->config, id);
  int mem = mc_config_get_max_memory_for_instance (self->config, id);

  const char *inst_dir = mc_instance_get_path (inst);

  g_autofree char *vjson = g_build_filename (inst_dir, "version.json", NULL);
  if (!g_file_test (vjson, G_FILE_TEST_EXISTS)) {
    adw_toast_overlay_add_toast (self->toast_overlay,
        adw_toast_new ("该实例尚未安装游戏版本，请先在下载页安装"));
    g_object_unref (inst);
    return;
  }

  const char *uuid   = self->player_uuid ? self->player_uuid : "0";
  const char *token  = self->mc_access_token ? self->mc_access_token : "0";

  const char *uname = self->player_name ? self->player_name : "Player";
  McLauncher *launcher = mc_launcher_new (inst_dir, java, mem,
                                           uname, uuid, token);
  g_autoptr (GError) error = NULL;
  GPtrArray *argv = mc_launcher_build_argv (launcher, &error);
  mc_launcher_free (launcher);

  if (!argv) {
    adw_toast_overlay_add_toast (self->toast_overlay,
                                  adw_toast_new (error->message));
    g_object_unref (inst);
    return;
  }

  g_autoptr (GStrvBuilder) b = g_strv_builder_new ();
  for (guint i = 0; i < argv->len - 1; i++)
  g_strv_builder_add (b, g_ptr_array_index (argv, i));
  g_auto (GStrv) strv = g_strv_builder_end (b);
  g_ptr_array_unref (argv);

  GSubprocess *proc = g_subprocess_newv ((const char * const *) strv,
                                          G_SUBPROCESS_FLAGS_NONE, &error);
  if (!proc) {
    adw_toast_overlay_add_toast (self->toast_overlay,
                                  adw_toast_new (error->message));
    g_object_unref (inst);
    return;
  }

  adw_toast_overlay_add_toast (self->toast_overlay,
                                adw_toast_new ("游戏已启动"));
  g_object_unref (proc);
  g_object_unref (inst);
}

/* ========== Mod 页 ========== */

static void
on_mod_switch_changed (GtkSwitch *sw, GParamSpec *pspec, McMod *mod) {
  gboolean active = gtk_switch_get_active (sw);
  if (!mc_mod_set_enabled (mod, active)) {
    g_signal_handlers_block_by_func (sw, on_mod_switch_changed, mod);
    gtk_switch_set_active (sw, !active);
    g_signal_handlers_unblock_by_func (sw, on_mod_switch_changed, mod);
  }
}

static void
append_mod_row (McApplication *self, McMod *mod, const char *filter) {
  if (filter && *filter &&
      !strstr (mc_mod_get_display_name (mod), filter))
    return;

  AdwExpanderRow *row = ADW_EXPANDER_ROW (adw_expander_row_new ());
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row),
                                  mc_mod_get_display_name (mod));
  adw_expander_row_set_subtitle (row,
      mc_mod_get_enabled (mod) ? "已启用" : "已禁用");

  GtkSwitch *sw = GTK_SWITCH (gtk_switch_new ());
  gtk_switch_set_active (sw, mc_mod_get_enabled (mod));
  gtk_widget_set_valign (GTK_WIDGET (sw), GTK_ALIGN_CENTER);
  g_signal_connect (sw, "notify::active",
                    G_CALLBACK (on_mod_switch_changed), mod);
  adw_expander_row_add_prefix (row, GTK_WIDGET (sw));

  AdwActionRow *path_row = ADW_ACTION_ROW (adw_action_row_new ());
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (path_row), "路径");
  adw_action_row_set_subtitle (path_row, mc_mod_get_path (mod));
  adw_expander_row_add_row (row, GTK_WIDGET (path_row));

  adw_preferences_group_add (self->mod_list_group, GTK_WIDGET (row));
}

static void
refresh_mods (McApplication *self) {
  if (!self->mod_list_group) return;

  clear_preferences_group (self->mod_list_group);
  g_clear_pointer (&self->mods, g_ptr_array_unref);
  self->mods = g_ptr_array_new_with_free_func (g_object_unref);

  McInstance *inst = get_selected_instance (self);
  if (!inst) {
    AdwActionRow *row = ADW_ACTION_ROW (adw_action_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "未选择实例");
    adw_preferences_group_add (self->mod_list_group, GTK_WIDGET (row));
    return;
  }

  g_autofree char *dir = g_build_filename (mc_instance_get_path (inst),
                                             "mods", NULL);
  g_mkdir_with_parents (dir, 0755);

  GDir *d = g_dir_open (dir, 0, NULL);
  if (d) {
    const char *name;
    while ((name = g_dir_read_name (d)) != NULL) {
      if (!g_str_has_suffix (name, ".jar") &&
          !g_str_has_suffix (name, ".jar.disabled"))
        continue;
      g_autofree char *full = g_build_filename (dir, name, NULL);
      g_ptr_array_add (self->mods, mc_mod_new_from_path (full));
    }
    g_dir_close (d);
  }

  const char *filter = self->mod_search
      ? gtk_editable_get_text (GTK_EDITABLE (self->mod_search))
      : NULL;

  if (self->mods->len == 0) {
    AdwActionRow *row = ADW_ACTION_ROW (adw_action_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row),
                                    "此实例尚未安装 Mod");
    adw_preferences_group_add (self->mod_list_group, GTK_WIDGET (row));
  } else {
    for (guint i = 0; i < self->mods->len; i++)
      append_mod_row (self, g_ptr_array_index (self->mods, i), filter);
  }

  g_object_unref (inst);
}

static void
on_mod_search_changed (AdwEntryRow *row, McApplication *self) {
  refresh_mods (self);
}

/* ========== 下载页 ========== */

static void
populate_group (McApplication       *self,
                AdwPreferencesGroup *group,
                const char          *filter,
                McVersionCategory    category) {
  clear_preferences_group (group);

  guint added = 0;
  guint n = self->version_list ? g_list_model_get_n_items (self->version_list) : 0;

  for (guint i = 0; i < n; i++) {
    g_autoptr (McVersion) v = g_list_model_get_item (self->version_list, i);
    if (mc_version_get_category (v) != category) continue;
    if (filter && *filter && !strstr (mc_version_get_id (v), filter)) continue;

    AdwActionRow *row = ADW_ACTION_ROW (adw_action_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row),
                                    mc_version_get_id (v));
    g_autofree char *sub = g_strdup_printf ("%s · %s",
                                             mc_version_get_type_str (v),
                                             mc_version_get_release_time (v));
    adw_action_row_set_subtitle (row, sub);

    GtkWidget *btn = gtk_button_new_with_label ("安装");
    gtk_widget_add_css_class (btn, "suggested-action");
    gtk_widget_add_css_class (btn, "pill");
    g_object_set_data_full (G_OBJECT (btn), "mc-version",
                             g_steal_pointer (&v), g_object_unref);
    g_signal_connect (btn, "clicked",
                      G_CALLBACK (on_install_version_clicked), self);
    adw_action_row_add_suffix (row, btn);

    adw_preferences_group_add (group, GTK_WIDGET (row));
    added++;
  }

  if (added == 0) {
    AdwActionRow *row = ADW_ACTION_ROW (adw_action_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row),
                                    filter && *filter ? "无匹配版本" : "暂无版本");
    adw_preferences_group_add (group, GTK_WIDGET (row));
  }
}

static void
populate_version_list (McApplication *self) {
  if (!self->version_list) return;

  populate_group (self, self->release_versions_group,
                   gtk_editable_get_text (GTK_EDITABLE (self->release_search)),
                   MC_VERSION_CATEGORY_RELEASE);
  populate_group (self, self->snapshot_versions_group,
                   gtk_editable_get_text (GTK_EDITABLE (self->snapshot_search)),
                   MC_VERSION_CATEGORY_SNAPSHOT);
  populate_group (self, self->old_versions_group,
                   gtk_editable_get_text (GTK_EDITABLE (self->old_search)),
                   MC_VERSION_CATEGORY_OLD);
  populate_group (self, self->fool_versions_group,
                   gtk_editable_get_text (GTK_EDITABLE (self->fool_search)),
                   MC_VERSION_CATEGORY_FOOL);
}

static void
on_version_search_changed (AdwEntryRow *row, McApplication *self) {
  populate_version_list (self);
}

static void
on_install_progress (const char *filename, gboolean started,
                     int completed, int total,
                     gint64 received, gint64 total_bytes,
                     double speed_bps, gpointer user_data) {
  McApplication *self = MC_APPLICATION (user_data);
  if (self->download_window)
    mc_download_window_on_progress (self->download_window, filename,
                                     started, completed, total,
                                     received, total_bytes, speed_bps);
}

static void
on_install_done (GError *error, gpointer user_data) {
  McApplication *self = MC_APPLICATION (user_data);

  McInstaller *inst = self->current_installer;
  self->current_installer = NULL;

  if (self->download_window)
    mc_download_window_on_done (self->download_window, error);

  if (inst)
    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, free_installer_idle, inst, NULL);

  if (!error) {
    if (self->installing_instance_path) {
      mc_instance_mark_installed (self->installing_instance_path);
      g_clear_pointer (&self->installing_instance_path, g_free);
    }
    refresh_instance_model (self);
  }
}

static gboolean
on_download_window_close (GtkWindow *win, McApplication *self) {
  self->download_window = NULL;
  if (self->current_installer)
    mc_installer_cancel (self->current_installer);
  return FALSE;
}

static void
on_version_json_loaded (GBytes *bytes, GError *error, gpointer user_data) {
  McApplication *self = MC_APPLICATION (user_data);

  if (error) {
    adw_toast_overlay_add_toast (self->toast_overlay, adw_toast_new (error->message));
    return;
  }

  gsize len;
  const char *data = g_bytes_get_data (bytes, &len);

  g_autoptr (GError) perr = NULL;
  McVersionMeta *meta = mc_version_meta_parse (data, &perr);
  if (!meta) {
    adw_toast_overlay_add_toast (self->toast_overlay, adw_toast_new (perr->message));
    return;
  }

  const char *vid = mc_version_meta_get_id (meta);

  g_autoptr (GError) cerr = NULL;
  McInstance *inst = mc_instance_manager_create (self->instance_manager,
                                                   vid, vid, "Vanilla",
                                                   FALSE, &cerr);
  if (!inst) {
    adw_toast_overlay_add_toast (self->toast_overlay, adw_toast_new (cerr->message));
    g_object_unref (meta);
    return;
  }

  const char *inst_dir = mc_instance_get_path (inst);

  g_free (self->installing_instance_path);
  self->installing_instance_path = g_strdup (mc_instance_get_path (inst));

  g_autofree char *vjson_path = g_build_filename (inst_dir, "version.json", NULL);
  g_file_set_contents (vjson_path, data, len, NULL);

  mc_instance_ensure_layout (inst);

  self->current_installer = mc_installer_new (meta, inst_dir);
  g_object_unref (meta);

  self->download_window = mc_download_window_new (GTK_WINDOW (self->window),
                                                    self->current_installer);
  g_object_ref (self->download_window);
  g_signal_connect (self->download_window, "close-request",
                    G_CALLBACK (on_download_window_close), self);
  gtk_window_present (GTK_WINDOW (self->download_window));

  mc_installer_start (self->current_installer,
                       on_install_progress, on_install_done, self);

  g_free (self->current_instance_id);
  self->current_instance_id = g_strdup (mc_instance_get_id (inst));
  g_object_unref (inst);
}

static void
on_install_version_clicked (GtkButton *btn, McApplication *self) {
  McVersion *v = g_object_get_data (G_OBJECT (btn), "mc-version");
  if (!v) return;

  if (self->current_installer) {
    adw_toast_overlay_add_toast (self->toast_overlay,
                                  adw_toast_new ("已有安装任务进行中"));
    return;
  }

  g_autofree char *msg = g_strdup_printf ("正在获取 %s 元数据…",
                                           mc_version_get_id (v));
  adw_toast_overlay_add_toast (self->toast_overlay, adw_toast_new (msg));

  mc_downloader_fetch_string_async (mc_version_get_url (v), NULL,
                                     on_version_json_loaded, self);
}

static void
on_manifest_loaded (GBytes *bytes, GError *error, gpointer user_data) {
  McApplication *self = MC_APPLICATION (user_data);

  if (error) {
    AdwActionRow *row = ADW_ACTION_ROW (adw_action_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "获取版本清单失败");
    adw_action_row_set_subtitle (row, error->message);
    clear_preferences_group (self->release_versions_group);
    adw_preferences_group_add (self->release_versions_group, GTK_WIDGET (row));
    return;
  }

  gsize len;
  const char *data = g_bytes_get_data (bytes, &len);

  g_autoptr (GError) perr = NULL;
  GListModel *model = mc_version_manifest_parse (data, &perr);
  if (!model) {
    g_warning ("解析版本清单失败: %s", perr->message);
    return;
  }

  g_clear_object (&self->version_list);
  self->version_list = model;
  populate_version_list (self);
}

static void
on_auth_window_done (const char *token, const char *uuid, const char *name,
                     GError *error, gpointer user_data) {
  McApplication *self = MC_APPLICATION (user_data);

  if (error) {
    adw_toast_overlay_add_toast (self->toast_overlay,
                                  adw_toast_new (error->message));
    return;
  }

  g_free (self->mc_access_token);
  g_free (self->player_uuid);
  g_free (self->player_name);
  self->mc_access_token = g_strdup (token);
  self->player_uuid     = g_strdup (uuid);
  self->player_name     = g_strdup (name);

  if (self->account_row) {
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->account_row),
                                    name);
    adw_action_row_set_subtitle (self->account_row,
                                  "Microsoft 账户 · 已登录");
  }

  adw_toast_overlay_add_toast (self->toast_overlay,
      adw_toast_new ("Microsoft 登录成功"));
}

static void
on_account_row_activated (AdwActionRow *row, McApplication *self) {
  if (self->auth_window) {
    gtk_window_present (GTK_WINDOW (self->auth_window));
    return;
  }

  self->auth_window = mc_auth_window_new (GTK_WINDOW (self->window),
                                            on_auth_window_done, self);
  g_object_add_weak_pointer (G_OBJECT (self->auth_window),
                              (gpointer *) &self->auth_window);
  gtk_window_present (GTK_WINDOW (self->auth_window));
}

/* ========== 生命周期 ========== */

static void
mc_application_activate (GApplication *application) {
  McApplication *self = MC_APPLICATION (application);

  GtkBuilder *builder = gtk_builder_new_from_resource (
      "/org/example/McLauncher/launcher-window.ui");

  self->window = GTK_WIDGET (gtk_builder_get_object (builder, "launcher_window"));

  self->account_row = ADW_ACTION_ROW (
      gtk_builder_get_object (builder, "account_row"));
  if (self->account_row)
    g_signal_connect (self->account_row, "activated",
                      G_CALLBACK (on_account_row_activated), self);

  /* 启动页 */
  self->instance_combo   = ADW_COMBO_ROW  (gtk_builder_get_object (builder, "instance_row"));
  self->java_combo       = ADW_COMBO_ROW  (gtk_builder_get_object (builder, "java_combo"));
  self->memory_spin      = ADW_SPIN_ROW   (gtk_builder_get_object (builder, "memory_spin"));
  self->isolation_switch = ADW_SWITCH_ROW (gtk_builder_get_object (builder, "instance_isolation_switch"));

  /* Mod 页 */
  self->mod_current_instance_row = ADW_ACTION_ROW (
      gtk_builder_get_object (builder, "mod_current_instance_row"));
  self->mod_search     = ADW_ENTRY_ROW        (gtk_builder_get_object (builder, "mod_search"));
  self->mod_list_group = ADW_PREFERENCES_GROUP (gtk_builder_get_object (builder, "mod_list_group"));

  /* 下载页 */
  self->release_search  = ADW_ENTRY_ROW (gtk_builder_get_object (builder, "release_search"));
  self->snapshot_search = ADW_ENTRY_ROW (gtk_builder_get_object (builder, "snapshot_search"));
  self->old_search      = ADW_ENTRY_ROW (gtk_builder_get_object (builder, "old_search"));
  self->fool_search     = ADW_ENTRY_ROW (gtk_builder_get_object (builder, "fool_search"));

  self->release_versions_group  = ADW_PREFERENCES_GROUP (
      gtk_builder_get_object (builder, "release_versions_group"));
  self->snapshot_versions_group = ADW_PREFERENCES_GROUP (
      gtk_builder_get_object (builder, "snapshot_versions_group"));
  self->old_versions_group      = ADW_PREFERENCES_GROUP (
      gtk_builder_get_object (builder, "old_versions_group"));
  self->fool_versions_group     = ADW_PREFERENCES_GROUP (
      gtk_builder_get_object (builder, "fool_versions_group"));

  self->toast_overlay = ADW_TOAST_OVERLAY (
      gtk_builder_get_object (builder, "global_toast_overlay"));

  /* 实例下拉框 */
  GtkExpression *expr = gtk_property_expression_new (MC_TYPE_INSTANCE, NULL, "name");
  adw_combo_row_set_expression (self->instance_combo, expr);
  refresh_instance_model (self);

  g_signal_connect (self->instance_combo, "notify::selected",
                    G_CALLBACK (on_instance_changed), self);

  GtkWidget *manage_row = GTK_WIDGET (
      gtk_builder_get_object (builder, "manage_instances_row"));
  if (manage_row)
    g_signal_connect (manage_row, "activated",
                      G_CALLBACK (on_manage_instances_activated), self);

  /* Java 下拉框占位 */
  const char *java_items[] = { "系统默认", "Java 17", "Java 8", NULL };
  adw_combo_row_set_model (self->java_combo,
                            G_LIST_MODEL (gtk_string_list_new (java_items)));

  /* Mod 搜索 */
  if (self->mod_search)
    g_signal_connect (self->mod_search, "changed",
                      G_CALLBACK (on_mod_search_changed), self);

  /* 版本搜索 */
  g_signal_connect (self->release_search, "changed",
                    G_CALLBACK (on_version_search_changed), self);
  g_signal_connect (self->snapshot_search, "changed",
                    G_CALLBACK (on_version_search_changed), self);
  g_signal_connect (self->old_search, "changed",
                    G_CALLBACK (on_version_search_changed), self);
  g_signal_connect (self->fool_search, "changed",
                    G_CALLBACK (on_version_search_changed), self);

  /* 启动按钮 */
  GtkWidget *launch_btn = GTK_WIDGET (gtk_builder_get_object (builder, "launch_button"));
  if (launch_btn)
    g_signal_connect (launch_btn, "clicked",
                      G_CALLBACK (on_launch_clicked), self);

  gtk_window_set_application (GTK_WINDOW (self->window),
                                GTK_APPLICATION (application));
  gtk_window_present (GTK_WINDOW (self->window));

  /* 首次触发当前实例填充 */
  on_instance_changed (self->instance_combo, NULL, self);

  /* 拉取版本清单 */
  mc_downloader_fetch_string_async (
      "https://launchermeta.mojang.com/mc/game/version_manifest_v2.json",
      NULL, on_manifest_loaded, self);

  g_object_unref (builder);
}

static void
mc_application_init (McApplication *self) {
  self->config           = mc_config_new ();
  self->mods             = g_ptr_array_new_with_free_func (g_object_unref);
  self->instance_manager = mc_instance_manager_new ();
}

static void
mc_application_dispose (GObject *object) {
  McApplication *self = MC_APPLICATION (object);

  if (self->current_installer) {
    mc_installer_cancel (self->current_installer);
    mc_installer_free (self->current_installer);
    self->current_installer = NULL;
  }

  /* auth_window 用弱指针自动置 NULL，这里只需清数据 */
  g_free (self->mc_access_token);
  g_free (self->player_uuid);
  g_free (self->player_name);

  g_clear_object (&self->version_list);
  g_clear_pointer (&self->mods, g_ptr_array_unref);
  g_clear_pointer (&self->instance_manager, mc_instance_manager_free);
  g_free (self->installing_instance_path);
  g_free (self->current_instance_id);

  G_OBJECT_CLASS (mc_application_parent_class)->dispose (object);
}

static void
mc_application_class_init (McApplicationClass *klass) {
  G_APPLICATION_CLASS (klass)->activate = mc_application_activate;
  G_OBJECT_CLASS (klass)->dispose       = mc_application_dispose;
}

McConfig *
mc_application_get_config (McApplication *self) {
  return self->config;
}