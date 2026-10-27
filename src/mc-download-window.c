#include "mc-download-window.h"

struct _McDownloadWindow {
  AdwWindow parent_instance;

  McInstaller *installer;

  GtkProgressBar      *total_progress;
  GtkLabel            *total_label;
  AdwPreferencesGroup *active_group;
  GtkWidget           *cancel_button;

  GHashTable *active_rows;

  guint auto_close_timeout;
};

G_DEFINE_FINAL_TYPE (McDownloadWindow, mc_download_window, ADW_TYPE_WINDOW)

static void
on_cancel_clicked (GtkButton *btn, McDownloadWindow *self) {
  gtk_widget_set_sensitive (self->cancel_button, FALSE);
  if (self->installer)
    mc_installer_cancel (self->installer);
}

static char *
format_speed (double bps) {
  if (bps < 1024)         return g_strdup_printf ("%.0f B/s", bps);
  if (bps < 1024 * 1024)  return g_strdup_printf ("%.1f KiB/s", bps / 1024);
  return g_strdup_printf ("%.1f MiB/s", bps / 1048576);
}

static void
progress_cb (const char *filename, gboolean started,
             int completed, int total,
             gint64 received, gint64 total_bytes,
             double speed_bps, gpointer user_data) {
  McDownloadWindow *self = user_data;

  if (total == 0) {
    gtk_progress_bar_set_fraction (self->total_progress, 0.0);
    gtk_label_set_text (self->total_label, filename);
    return;
  }

  gtk_progress_bar_set_fraction (self->total_progress,
                                  (double) completed / total);
  g_autofree char *text = g_strdup_printf ("%d / %d", completed, total);
  gtk_label_set_text (self->total_label, text);

  const char *base = strrchr (filename, '/');
  base = base ? base + 1 : filename;

  AdwActionRow *row = g_hash_table_lookup (self->active_rows, filename);

  if (started) {
    if (!row) {
      row = ADW_ACTION_ROW (adw_action_row_new ());
      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), base);
      adw_preferences_group_add (self->active_group, GTK_WIDGET (row));
      g_hash_table_insert (self->active_rows, g_strdup (filename), row);
    }

    g_autofree char *speed = format_speed (speed_bps);
    g_autofree char *sub;
    if (total_bytes > 0)
      sub = g_strdup_printf ("%s · %.0f%%", speed,
                              (double) received / total_bytes * 100);
    else if (received > 0)
      sub = g_strdup_printf ("%s · %" G_GINT64_FORMAT " B", speed, received);
    else
      sub = g_strdup (speed);
    adw_action_row_set_subtitle (row, sub);
  } else {
    if (row) {
      adw_preferences_group_remove (self->active_group, GTK_WIDGET (row));
      g_hash_table_remove (self->active_rows, filename);
    }
  }
}

static gboolean
close_window_cb (gpointer data) {
  McDownloadWindow *self = data;
  self->auto_close_timeout = 0;
  gtk_window_close (GTK_WINDOW (self));
  return G_SOURCE_REMOVE;
}

static void
done_cb (GError *error, gpointer user_data) {
  McDownloadWindow *self = user_data;

  gtk_widget_set_sensitive (self->cancel_button, FALSE);
  gtk_window_set_deletable (GTK_WINDOW (self), TRUE);

  if (error) {
    if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      gtk_label_set_text (self->total_label, "已取消");
    else
      gtk_label_set_text (self->total_label, error->message);
  } else {
    gtk_progress_bar_set_fraction (self->total_progress, 1.0);
    gtk_label_set_text (self->total_label, "安装完成");
  }

  if (self->auto_close_timeout) {
    g_source_remove (self->auto_close_timeout);
    self->auto_close_timeout = 0;
  }
  self->auto_close_timeout = g_timeout_add_seconds_full (
      G_PRIORITY_DEFAULT, 3, close_window_cb,
      g_object_ref (self), g_object_unref);
}

static gboolean
on_close_request (GtkWindow *win, McDownloadWindow *self) {
  if (self->auto_close_timeout) {
    g_source_remove (self->auto_close_timeout);
    self->auto_close_timeout = 0;
  }
  return FALSE;
}

static void
mc_download_window_dispose (GObject *obj) {
  McDownloadWindow *self = MC_DOWNLOAD_WINDOW (obj);
  if (self->auto_close_timeout) {
    g_source_remove (self->auto_close_timeout);
    self->auto_close_timeout = 0;
  }
  g_clear_pointer (&self->active_rows, g_hash_table_unref);
  G_OBJECT_CLASS (mc_download_window_parent_class)->dispose (obj);
}

static void
mc_download_window_class_init (McDownloadWindowClass *k) {
  G_OBJECT_CLASS (k)->dispose = mc_download_window_dispose;
}

static void
mc_download_window_init (McDownloadWindow *self) {
  self->active_rows = g_hash_table_new_full (g_str_hash, g_str_equal,
                                              g_free, NULL);
}

McDownloadWindow *
mc_download_window_new (GtkWindow *parent, McInstaller *installer) {
  McDownloadWindow *self = g_object_new (MC_TYPE_DOWNLOAD_WINDOW, NULL);

  self->installer = installer;

  gtk_window_set_transient_for (GTK_WINDOW (self), parent);
  gtk_window_set_modal (GTK_WINDOW (self), TRUE);
  gtk_window_set_default_size (GTK_WINDOW (self), 520, 420);
  gtk_window_set_title (GTK_WINDOW (self), "正在安装");
  gtk_window_set_deletable (GTK_WINDOW (self), FALSE);

  self->cancel_button = gtk_button_new_with_label ("取消");
  gtk_widget_add_css_class (self->cancel_button, "destructive-action");
  g_signal_connect (self->cancel_button, "clicked",
                     G_CALLBACK (on_cancel_clicked), self);

  AdwHeaderBar *header = ADW_HEADER_BAR (adw_header_bar_new ());
  adw_header_bar_pack_end (header, self->cancel_button);

  AdwActionRow *total_row = ADW_ACTION_ROW (adw_action_row_new ());
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (total_row), "总进度");
  self->total_label = GTK_LABEL (gtk_label_new ("准备中…"));
  adw_action_row_add_suffix (total_row, GTK_WIDGET (self->total_label));

  self->total_progress = GTK_PROGRESS_BAR (gtk_progress_bar_new ());
  gtk_widget_set_valign (GTK_WIDGET (self->total_progress), GTK_ALIGN_CENTER);
  gtk_widget_set_size_request (GTK_WIDGET (self->total_progress), 200, -1);
  adw_action_row_add_suffix (total_row, GTK_WIDGET (self->total_progress));

  AdwPreferencesGroup *total_group =
      ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
  adw_preferences_group_add (total_group, GTK_WIDGET (total_row));

  self->active_group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
  adw_preferences_group_set_title (self->active_group, "正在下载");

  AdwPreferencesPage *page = ADW_PREFERENCES_PAGE (adw_preferences_page_new ());
  adw_preferences_page_add (page, total_group);
  adw_preferences_page_add (page, self->active_group);

  GtkScrolledWindow *scroll = GTK_SCROLLED_WINDOW (gtk_scrolled_window_new ());
  gtk_scrolled_window_set_child (scroll, GTK_WIDGET (page));

  AdwToolbarView *tv = ADW_TOOLBAR_VIEW (adw_toolbar_view_new ());
  adw_toolbar_view_add_top_bar (tv, GTK_WIDGET (header));
  adw_toolbar_view_set_content (tv, GTK_WIDGET (scroll));
  adw_window_set_content (ADW_WINDOW (self), GTK_WIDGET (tv));

  g_signal_connect (self, "close-request",
                     G_CALLBACK (on_close_request), self);

  return self;
}

void
mc_download_window_on_progress (McDownloadWindow *self,
                                 const char *filename,
                                 gboolean started,
                                 int completed, int total,
                                 gint64 received, gint64 total_bytes,
                                 double speed_bps) {
  progress_cb (filename, started, completed, total,
               received, total_bytes, speed_bps, self);
}

void
mc_download_window_on_done (McDownloadWindow *self, GError *error) {
  done_cb (error, self);
}