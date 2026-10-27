#include "mc-instance-window.h"

#include <json-glib/json-glib.h>
#include <string.h>

struct _McInstanceWindow {
  AdwWindow parent_instance;

  McInstanceManager *manager;
  McInstanceWindowSelectedCb cb;
  gpointer user_data;

  GtkWidget           *search_entry;
  AdwPreferencesGroup *list_group;

  GListModel *instances;
};

G_DEFINE_FINAL_TYPE (McInstanceWindow, mc_instance_window, ADW_TYPE_WINDOW)

static void
on_row_activated (AdwActionRow *row, McInstanceWindow *self) {
  McInstance *inst = g_object_get_data (G_OBJECT (row), "mc-instance");
  if (!inst) return;
  if (self->cb) self->cb (inst, self->user_data);
  gtk_window_close (GTK_WINDOW (self));
}

static void
clear_group (AdwPreferencesGroup *g) {
  GtkWidget *c = gtk_widget_get_first_child (GTK_WIDGET (g));
  while (c) {
    GtkWidget *n = gtk_widget_get_next_sibling (c);
    adw_preferences_group_remove (g, c);
    c = n;
  }
}

static void
refresh_list (McInstanceWindow *self) {
  clear_group (self->list_group);
  if (!self->instances) return;

  const char *filter = gtk_editable_get_text (GTK_EDITABLE (self->search_entry));
  gboolean has_filter = filter && *filter;

  guint n = g_list_model_get_n_items (self->instances);
  guint added = 0;

  for (guint i = 0; i < n; i++) {
    g_autoptr (McInstance) inst = g_list_model_get_item (self->instances, i);

    if (has_filter) {
      g_autofree char *name_l = g_utf8_strdown (mc_instance_get_name (inst), -1);
      g_autofree char *filt_l = g_utf8_strdown (filter, -1);
      if (!strstr (name_l, filt_l) &&
          !strstr (mc_instance_get_mc_version (inst), filter))
        continue;
    }

    AdwActionRow *row = ADW_ACTION_ROW (adw_action_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row),
                                    mc_instance_get_name (inst));
    g_autofree char *sub = g_strdup_printf ("%s · %s · %s",
                                             mc_instance_get_mc_version (inst),
                                             mc_instance_get_loader (inst),
                                             mc_instance_get_path (inst));
    adw_action_row_set_subtitle (row, sub);
    g_object_set_data_full (G_OBJECT (row), "mc-instance",
                             g_object_ref (inst), g_object_unref);
    g_signal_connect (row, "activated", G_CALLBACK (on_row_activated), self);

    adw_preferences_group_add (self->list_group, GTK_WIDGET (row));
    added++;
  }

  if (added == 0) {
    AdwActionRow *row = ADW_ACTION_ROW (adw_action_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row),
                                    has_filter ? "无匹配实例" : "暂无实例");
    adw_preferences_group_add (self->list_group, GTK_WIDGET (row));
  }
}

static void
rescan (McInstanceWindow *self) {
  g_clear_object (&self->instances);
  self->instances = mc_instance_manager_scan (self->manager);
  refresh_list (self);
}

static void
on_search_changed (AdwEntryRow *row, McInstanceWindow *self) {
  refresh_list (self);
}

/* ========== 新建对话框 ========== */

static void
on_create_confirm (AdwAlertDialog *dialog, const char *response, McInstanceWindow *self) {
  if (g_strcmp0 (response, "create") != 0) return;

  AdwEntryRow *name_row = g_object_get_data (G_OBJECT (dialog), "name-row");
  AdwEntryRow *ver_row  = g_object_get_data (G_OBJECT (dialog), "ver-row");

  const char *name = gtk_editable_get_text (GTK_EDITABLE (name_row));
  const char *ver  = gtk_editable_get_text (GTK_EDITABLE (ver_row));
  if (!*name || !*ver) return;

  g_autoptr (GError) err = NULL;
  McInstance *inst = mc_instance_manager_create (self->manager,
                                                   name, ver, "Vanilla",
                                                   TRUE, &err);
  if (!inst) {
    g_warning ("创建实例失败: %s", err->message);
    return;
  }
  g_object_unref (inst);
  rescan (self);
}

static void
on_create_clicked (GtkButton *btn, McInstanceWindow *self) {
  AdwAlertDialog *dlg = ADW_ALERT_DIALOG (adw_alert_dialog_new ("新建实例", NULL));

  AdwEntryRow *name_row = ADW_ENTRY_ROW (adw_entry_row_new ());
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (name_row), "名称");
  gtk_editable_set_text (GTK_EDITABLE (name_row), "新实例");

  AdwEntryRow *ver_row = ADW_ENTRY_ROW (adw_entry_row_new ());
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (ver_row), "MC 版本");
  gtk_editable_set_text (GTK_EDITABLE (ver_row), "1.21.4");

  GtkBox *box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_VERTICAL, 6));
  gtk_widget_set_margin_top (GTK_WIDGET (box), 12);
  gtk_widget_set_margin_bottom (GTK_WIDGET (box), 12);
  gtk_widget_set_margin_start (GTK_WIDGET (box), 12);
  gtk_widget_set_margin_end (GTK_WIDGET (box), 12);
  gtk_box_append (box, GTK_WIDGET (name_row));
  gtk_box_append (box, GTK_WIDGET (ver_row));

  adw_alert_dialog_set_extra_child (dlg, GTK_WIDGET (box));
  adw_alert_dialog_add_response (dlg, "cancel", "取消");
  adw_alert_dialog_add_response (dlg, "create", "创建");
  adw_alert_dialog_set_response_appearance (dlg, "create", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response (dlg, "create");

  g_object_set_data (G_OBJECT (dlg), "name-row", name_row);
  g_object_set_data (G_OBJECT (dlg), "ver-row",  ver_row);

  g_signal_connect (dlg, "response", G_CALLBACK (on_create_confirm), self);
  adw_dialog_present (ADW_DIALOG (dlg), GTK_WIDGET (self));
}

/* ========== 导入 ========== */

static void
on_folder_selected (GObject *src, GAsyncResult *res, McInstanceWindow *self) {
  g_autoptr (GError) err = NULL;
  GFile *folder = gtk_file_dialog_select_folder_finish (GTK_FILE_DIALOG (src),
                                                         res, &err);
  if (!folder) return;

  g_autofree char *path = g_file_get_path (folder);
  g_autofree char *base = g_path_get_basename (path);

  McInstance *inst = mc_instance_manager_import (self->manager, path,
                                                   base, &err);
  if (!inst) {
    g_warning ("导入失败: %s", err->message);
    g_object_unref (folder);
    return;
  }
  g_object_unref (inst);
  rescan (self);
  g_object_unref (folder);
}

static void
on_import_clicked (GtkButton *btn, McInstanceWindow *self) {
  GtkFileDialog *dlg = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dlg, "选择 .minecraft 目录");
  gtk_file_dialog_select_folder (dlg, GTK_WINDOW (self), NULL,
                                  (GAsyncReadyCallback) on_folder_selected, self);
  g_object_unref (dlg);
}

/* ========== 生命周期 ========== */

static void
mc_instance_window_dispose (GObject *obj) {
  McInstanceWindow *self = MC_INSTANCE_WINDOW (obj);
  g_clear_object (&self->instances);
  G_OBJECT_CLASS (mc_instance_window_parent_class)->dispose (obj);
}

static void
mc_instance_window_class_init (McInstanceWindowClass *k) {
  G_OBJECT_CLASS (k)->dispose = mc_instance_window_dispose;
}

static void
mc_instance_window_init (McInstanceWindow *self) {}

McInstanceWindow *
mc_instance_window_new (GtkWindow *parent, McInstanceManager *manager,
                         McInstanceWindowSelectedCb cb, gpointer user_data) {
  McInstanceWindow *self = g_object_new (MC_TYPE_INSTANCE_WINDOW, NULL);
  self->manager   = manager;
  self->cb        = cb;
  self->user_data = user_data;

  gtk_window_set_transient_for (GTK_WINDOW (self), parent);
  gtk_window_set_modal (GTK_WINDOW (self), TRUE);
  gtk_window_set_default_size (GTK_WINDOW (self), 640, 560);
  gtk_window_set_title (GTK_WINDOW (self), "管理实例");

  AdwHeaderBar *header = ADW_HEADER_BAR (adw_header_bar_new ());

  GtkWidget *create_btn = gtk_button_new_with_label ("新建");
  gtk_widget_add_css_class (create_btn, "suggested-action");
  g_signal_connect (create_btn, "clicked", G_CALLBACK (on_create_clicked), self);
  adw_header_bar_pack_end (header, create_btn);

  GtkWidget *import_btn = gtk_button_new_with_label ("导入");
  g_signal_connect (import_btn, "clicked", G_CALLBACK (on_import_clicked), self);
  adw_header_bar_pack_end (header, import_btn);

  self->search_entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (self->search_entry), "搜索实例名称或版本");
  GtkWidget *search_wrap = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_margin_top (search_wrap, 12);
  gtk_widget_set_margin_start (search_wrap, 12);
  gtk_widget_set_margin_end (search_wrap, 12);
  gtk_box_append (GTK_BOX (search_wrap), self->search_entry);
  g_signal_connect (self->search_entry, "changed",
                    G_CALLBACK (on_search_changed), self);

  self->list_group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
  adw_preferences_group_set_title (self->list_group, "实例");

  AdwPreferencesPage *page = ADW_PREFERENCES_PAGE (adw_preferences_page_new ());
  adw_preferences_page_add (page, self->list_group);

  GtkBox *vbox = GTK_BOX (gtk_box_new (GTK_ORIENTATION_VERTICAL, 0));
  gtk_box_append (vbox, search_wrap);
  gtk_box_append (vbox, GTK_WIDGET (page));

  GtkScrolledWindow *scroll = GTK_SCROLLED_WINDOW (gtk_scrolled_window_new ());
  gtk_scrolled_window_set_child (scroll, GTK_WIDGET (vbox));

  AdwToolbarView *tv = ADW_TOOLBAR_VIEW (adw_toolbar_view_new ());
  adw_toolbar_view_add_top_bar (tv, GTK_WIDGET (header));
  adw_toolbar_view_set_content (tv, GTK_WIDGET (scroll));
  adw_window_set_content (ADW_WINDOW (self), GTK_WIDGET (tv));

  rescan (self);
  return self;
}