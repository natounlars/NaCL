#include "mc-auth-window.h"

struct _McAuthWindow {
  AdwWindow parent_instance;

  McAuth            *auth;
  McAuthWindowDoneCb cb;
  gpointer           user_data;

  GtkWidget       *intro_box;
  GtkWidget       *status_box;
  GtkLabel        *code_label;
  GtkLinkButton   *link_button;
  GtkLabel        *status_label;
  GtkWidget       *start_button;
  GtkWidget       *cancel_button;

  guint            close_timeout;
};

G_DEFINE_FINAL_TYPE (McAuthWindow, mc_auth_window, ADW_TYPE_WINDOW)

/* ========== 关闭辅助 ========== */

static gboolean
close_window_cb (gpointer data) {
  McAuthWindow *self = data;
  self->close_timeout = 0;
  gtk_window_close (GTK_WINDOW (self));
  return G_SOURCE_REMOVE;
}

static gboolean
on_close_request (GtkWindow *win, McAuthWindow *self) {
  if (self->close_timeout) {
    g_source_remove (self->close_timeout);
    self->close_timeout = 0;
  }
  if (self->auth) {
    mc_auth_cancel (self->auth);
    mc_auth_free (self->auth);
    self->auth = NULL;
  }
  return FALSE;
}

/* ========== 回调 ========== */

static void
on_device_code (const char *code, const char *uri, gpointer data) {
  McAuthWindow *self = data;
  gtk_label_set_text (self->code_label, code);
  gtk_link_button_set_uri (self->link_button, uri);
  gtk_button_set_label (GTK_BUTTON (self->link_button), uri);
  gtk_label_set_text (self->status_label,
                       "请在浏览器中打开以上链接并输入验证码。\n本窗口会在授权完成后自动关闭。");
}

static void
on_done (const char *token, const char *uuid, const char *name,
          GError *error, gpointer data) {
  McAuthWindow *self = data;

  if (error) {
    gtk_label_set_text (self->status_label, error->message);
    gtk_widget_set_sensitive (self->start_button, TRUE);
    if (self->auth) {
      mc_auth_free (self->auth);
      self->auth = NULL;
    }
    return;
  }

  gtk_label_set_text (self->status_label, "登录成功");
  if (self->cb)
    self->cb (token, uuid, name, NULL, self->user_data);

  self->close_timeout = g_timeout_add_seconds_full (
      G_PRIORITY_DEFAULT, 2, close_window_cb,
      g_object_ref (self), g_object_unref);
}

static void
on_start_clicked (GtkButton *btn, McAuthWindow *self) {
  gtk_widget_set_sensitive (self->start_button, FALSE);
  gtk_widget_set_visible (self->intro_box, FALSE);
  gtk_widget_set_visible (self->status_box, TRUE);
  gtk_label_set_text (self->status_label, "正在获取设备码…");

  self->auth = mc_auth_new ();
  mc_auth_start (self->auth, on_device_code, on_done, self);
}

static void
on_cancel_clicked (GtkButton *btn, McAuthWindow *self) {
  gtk_window_close (GTK_WINDOW (self));
}

/* ========== 生命周期 ========== */

static void
mc_auth_window_dispose (GObject *obj) {
  McAuthWindow *self = MC_AUTH_WINDOW (obj);
  if (self->close_timeout) {
    g_source_remove (self->close_timeout);
    self->close_timeout = 0;
  }
  if (self->auth) {
    mc_auth_cancel (self->auth);
    mc_auth_free (self->auth);
    self->auth = NULL;
  }
  G_OBJECT_CLASS (mc_auth_window_parent_class)->dispose (obj);
}

static void
mc_auth_window_class_init (McAuthWindowClass *k) {
  G_OBJECT_CLASS (k)->dispose = mc_auth_window_dispose;
}

static void
mc_auth_window_init (McAuthWindow *self) {}

McAuthWindow *
mc_auth_window_new (GtkWindow *parent, McAuthWindowDoneCb cb, gpointer user_data) {
  McAuthWindow *self = g_object_new (MC_TYPE_AUTH_WINDOW, NULL);
  self->cb        = cb;
  self->user_data = user_data;

  gtk_window_set_transient_for (GTK_WINDOW (self), parent);
  gtk_window_set_modal (GTK_WINDOW (self), TRUE);
  gtk_window_set_default_size (GTK_WINDOW (self), 520, 360);
  gtk_window_set_title (GTK_WINDOW (self), "登录 Microsoft 账户");

  /* HeaderBar + 取消 */
  AdwHeaderBar *header = ADW_HEADER_BAR (adw_header_bar_new ());
  self->cancel_button = gtk_button_new_with_label ("取消");
  g_signal_connect (self->cancel_button, "clicked",
                     G_CALLBACK (on_cancel_clicked), self);
  adw_header_bar_pack_end (header, self->cancel_button);

  /* 说明区 */
  GtkWidget *intro_label = gtk_label_new (
      "登录 Microsoft 账户后即可在线游戏、加入服务器。\n"
      "点击下方按钮将生成一次性验证码，需要在浏览器中输入。");
  gtk_label_set_wrap (GTK_LABEL (intro_label), TRUE);
  gtk_label_set_xalign (GTK_LABEL (intro_label), 0.0);
  gtk_widget_set_margin_top (intro_label, 18);
  gtk_widget_set_margin_bottom (intro_label, 12);
  gtk_widget_set_margin_start (intro_label, 18);
  gtk_widget_set_margin_end (intro_label, 18);

  self->start_button = gtk_button_new_with_label ("开始登录");
  gtk_widget_add_css_class (self->start_button, "suggested-action");
  gtk_widget_add_css_class (self->start_button, "pill");
  gtk_widget_set_halign (self->start_button, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_bottom (self->start_button, 18);
  g_signal_connect (self->start_button, "clicked",
                     G_CALLBACK (on_start_clicked), self);

  self->intro_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append (GTK_BOX (self->intro_box), intro_label);
  gtk_box_append (GTK_BOX (self->intro_box), self->start_button);

  /* 状态区（开始登录后显示） */
  self->code_label = GTK_LABEL (gtk_label_new (""));
  PangoAttrList *attrs = pango_attr_list_new ();
  pango_attr_list_insert (attrs, pango_attr_scale_new (2.4));
  pango_attr_list_insert (attrs, pango_attr_family_new ("Monospace"));
  pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes (self->code_label, attrs);
  pango_attr_list_unref (attrs);
  gtk_label_set_selectable (self->code_label, TRUE);
  gtk_widget_set_halign (GTK_WIDGET (self->code_label), GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top (GTK_WIDGET (self->code_label), 12);
  gtk_widget_set_margin_bottom (GTK_WIDGET (self->code_label), 12);

  self->link_button = GTK_LINK_BUTTON (gtk_link_button_new (""));
  gtk_widget_set_halign (GTK_WIDGET (self->link_button), GTK_ALIGN_CENTER);
  gtk_widget_set_margin_bottom (GTK_WIDGET (self->link_button), 12);

  self->status_label = GTK_LABEL (gtk_label_new (""));
  gtk_label_set_wrap (GTK_LABEL (self->status_label), TRUE);
  gtk_label_set_xalign (GTK_LABEL (self->status_label), 0.0);
  gtk_widget_set_margin_start (GTK_WIDGET (self->status_label), 18);
  gtk_widget_set_margin_end (GTK_WIDGET (self->status_label), 18);
  gtk_widget_set_margin_bottom (GTK_WIDGET (self->status_label), 18);

  self->status_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append (GTK_BOX (self->status_box), GTK_WIDGET (self->code_label));
  gtk_box_append (GTK_BOX (self->status_box), GTK_WIDGET (self->link_button));
  gtk_box_append (GTK_BOX (self->status_box), GTK_WIDGET (self->status_label));
  gtk_widget_set_visible (self->status_box, FALSE);

  GtkBox *content = GTK_BOX (gtk_box_new (GTK_ORIENTATION_VERTICAL, 0));
  gtk_box_append (content, self->intro_box);
  gtk_box_append (content, self->status_box);

  GtkScrolledWindow *scroll = GTK_SCROLLED_WINDOW (gtk_scrolled_window_new ());
  gtk_scrolled_window_set_child (scroll, GTK_WIDGET (content));

  AdwToolbarView *tv = ADW_TOOLBAR_VIEW (adw_toolbar_view_new ());
  adw_toolbar_view_add_top_bar (tv, GTK_WIDGET (header));
  adw_toolbar_view_set_content (tv, GTK_WIDGET (scroll));
  adw_window_set_content (ADW_WINDOW (self), GTK_WIDGET (tv));

  g_signal_connect (self, "close-request",
                     G_CALLBACK (on_close_request), self);

  return self;
}