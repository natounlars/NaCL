#include "mc-auth.h"

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <string.h>

/* 填入你在 Azure Portal 注册的 Application (client) ID */
#define MS_CLIENT_ID   "f3e0c2b9-bd6c-4723-add2-6bfa738c4596"
#define MS_SCOPE       "XboxLive.signin offline_access"

#define MS_DEVICE_URL  "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode"
#define MS_TOKEN_URL   "https://login.microsoftonline.com/consumers/oauth2/v2.0/token"
#define XBL_URL        "https://user.auth.xboxlive.com/user/authenticate"
#define XSTS_URL       "https://xsts.auth.xboxlive.com/xsts/authorize"
#define MC_LOGIN_URL   "https://api.minecraftservices.com/authentication/login_with_xbox"
#define MC_PROFILE_URL "https://api.minecraftservices.com/minecraft/profile"

struct _McAuth {
  SoupSession        *session;
  GCancellable       *cancellable;

  char               *device_code;
  char               *ms_access_token;
  char               *xbl_token;
  char               *uhs;
  char               *xsts_token;
  char               *mc_access_token;

  guint               poll_source;
  McAuthDeviceCodeCb  device_cb;
  McAuthDoneCb        done_cb;
  gpointer            user_data;
};

static void auth_step_xbl     (McAuth *self);
static void auth_step_xsts    (McAuth *self);
static void auth_step_mc      (McAuth *self);
static void auth_step_profile (McAuth *self);
static gboolean poll_ms_token (gpointer data);

/* ---------- 通用错误出口 ---------- */

static void
finish_error (McAuth *self, const char *msg) {
  if (self->done_cb) {
    GError *e = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_FAILED, msg);
    self->done_cb (NULL, NULL, NULL, e, self->user_data);
    g_error_free (e);
  }
}

/* ---------- 生命周期 ---------- */

McAuth *
mc_auth_new (void) {
  McAuth *self = g_new0 (McAuth, 1);
  self->session     = soup_session_new ();
  self->cancellable = g_cancellable_new ();
  return self;
}

void
mc_auth_free (McAuth *self) {
  if (!self) return;
  if (self->poll_source) {
    g_source_remove (self->poll_source);
    self->poll_source = 0;
  }
  g_free (self->device_code);
  g_free (self->ms_access_token);
  g_free (self->xbl_token);
  g_free (self->uhs);
  g_free (self->xsts_token);
  g_free (self->mc_access_token);
  g_clear_object (&self->session);
  g_clear_object (&self->cancellable);
  g_free (self);
}

void
mc_auth_cancel (McAuth *self) {
  if (self->poll_source) {
    g_source_remove (self->poll_source);
    self->poll_source = 0;
  }
  g_cancellable_cancel (self->cancellable);
}

/* ---------- POST / GET 辅助 ---------- */

static void
post_json_async (McAuth *self, const char *url,
                  const char *body, GAsyncReadyCallback cb, gpointer data) {
  SoupMessage *msg = soup_message_new ("POST", url);
  soup_message_set_request_body_from_bytes (msg, "application/json",
                                             g_bytes_new (body, strlen (body)));
  SoupMessageHeaders *h = soup_message_get_request_headers (msg);
  soup_message_headers_replace (h, "Accept", "application/json");
  soup_session_send_and_read_async (self->session, msg, G_PRIORITY_DEFAULT,
                                     self->cancellable, cb, data);
  g_object_unref (msg);
}

static void
post_form_async (McAuth *self, const char *url,
                  const char *body, GAsyncReadyCallback cb, gpointer data) {
  SoupMessage *msg = soup_message_new ("POST", url);
  soup_message_set_request_body_from_bytes (
      msg, "application/x-www-form-urlencoded",
      g_bytes_new (body, strlen (body)));
  soup_session_send_and_read_async (self->session, msg, G_PRIORITY_DEFAULT,
                                     self->cancellable, cb, data);
  g_object_unref (msg);
}

static void
get_with_bearer_async (McAuth *self, const char *url,
                        const char *token, GAsyncReadyCallback cb, gpointer data) {
  SoupMessage *msg = soup_message_new ("GET", url);
  g_autofree char *auth = g_strdup_printf ("Bearer %s", token);
  soup_message_headers_replace (soup_message_get_request_headers (msg),
                                 "Authorization", auth);
  soup_session_send_and_read_async (self->session, msg, G_PRIORITY_DEFAULT,
                                     self->cancellable, cb, data);
  g_object_unref (msg);
}

/* ============================================================
 * Step 0：请求设备码
 * ============================================================ */

static void
on_device_code (GObject *src, GAsyncResult *res, gpointer data) {
  McAuth *self = data;
  GError *error = NULL;
  GBytes *bytes = soup_session_send_and_read_finish (SOUP_SESSION (src), res, &error);

  if (error) {
    finish_error (self, error->message);
    g_error_free (error);
    return;
  }

  gsize len;
  const char *raw = g_bytes_get_data (bytes, &len);
  g_autoptr (JsonParser) parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, raw, len, NULL)) {
    finish_error (self, "解析设备码响应失败");
    g_bytes_unref (bytes);
    return;
  }

  JsonObject *o = json_node_get_object (json_parser_get_root (parser));
  self->device_code = g_strdup (json_object_get_string_member (o, "device_code"));
  const char *ucode = json_object_get_string_member (o, "user_code");
  const char *vuri  = json_object_get_string_member (o, "verification_uri");
  int interval      = json_object_get_int_member (o, "interval");

  if (self->device_cb)
    self->device_cb (ucode, vuri, self->user_data);

  /* 启动轮询：默认 5 秒，设备码响应里通常也会给 interval */
  self->poll_source = g_timeout_add_seconds (
      interval > 0 ? interval : 5, poll_ms_token, self);

  g_bytes_unref (bytes);
}

/* ============================================================
 * Step 1：轮询 MS Token
 * ============================================================ */

static void
on_ms_token (GObject *src, GAsyncResult *res, gpointer data) {
  McAuth *self = data;
  GError *error = NULL;
  GBytes *bytes = soup_session_send_and_read_finish (SOUP_SESSION (src), res, &error);

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    if (error) g_error_free (error);
    if (bytes) g_bytes_unref (bytes);
    return;
  }

  gsize len = 0;
  const char *raw = bytes ? g_bytes_get_data (bytes, &len) : NULL;
  g_autoptr (JsonParser) parser = json_parser_new ();
  JsonObject *o = NULL;

  if (raw && json_parser_load_from_data (parser, raw, len, NULL))
    o = json_node_get_object (json_parser_get_root (parser));

  if (o && json_object_has_member (o, "error")) {
    const char *code = json_object_get_string_member (o, "error");
    if (g_strcmp0 (code, "authorization_pending") == 0) {
      /* 用户还没完成浏览器授权，继续轮询 */
      self->poll_source = g_timeout_add_seconds (5, poll_ms_token, self);
    } else if (g_strcmp0 (code, "slow_down") == 0) {
      self->poll_source = g_timeout_add_seconds (10, poll_ms_token, self);
    } else {
      finish_error (self, code);
    }
  } else if (o && json_object_has_member (o, "access_token")) {
    self->ms_access_token = g_strdup (
        json_object_get_string_member (o, "access_token"));

    /* 正确顺序：MS Token → Xbox Live → XSTS → MC Login → Profile */
    auth_step_xbl (self);
  } else {
    finish_error (self, "获取 MS Token 失败");
  }

  if (error) g_error_free (error);
  if (bytes) g_bytes_unref (bytes);
}

static gboolean
poll_ms_token (gpointer data) {
  McAuth *self = data;
  self->poll_source = 0;

  if (g_cancellable_is_cancelled (self->cancellable))
    return G_SOURCE_REMOVE;

  g_autofree char *body = g_strdup_printf (
      "client_id=%s&grant_type=urn:ietf:params:oauth:grant-type:device_code"
      "&device_code=%s",
      MS_CLIENT_ID, self->device_code);

  post_form_async (self, MS_TOKEN_URL, body, on_ms_token, self);
  return G_SOURCE_REMOVE;
}

/* ============================================================
 * Step 2：Xbox Live 认证
 * ============================================================ */

static void
on_xbl (GObject *src, GAsyncResult *res, gpointer data) {
  McAuth *self = data;
  GError *error = NULL;
  GBytes *bytes = soup_session_send_and_read_finish (SOUP_SESSION (src), res, &error);

  if (error) {
    finish_error (self, error->message);
    g_error_free (error);
    return;
  }

  gsize len;
  const char *raw = g_bytes_get_data (bytes, &len);
  g_autoptr (JsonParser) parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, raw, len, NULL)) {
    finish_error (self, "解析 Xbox Live 响应失败");
    g_bytes_unref (bytes);
    return;
  }

  JsonObject *o = json_node_get_object (json_parser_get_root (parser));
  if (!json_object_has_member (o, "Token")) {
    finish_error (self, "Xbox Live 认证失败");
    g_bytes_unref (bytes);
    return;
  }

  self->xbl_token = g_strdup (json_object_get_string_member (o, "Token"));
  g_bytes_unref (bytes);

  auth_step_xsts (self);
}

static void
auth_step_xbl (McAuth *self) {
  /* RpsTicket 必须带 d= 前缀，否则 Xbox Live 返回 400 */
  g_autofree char *rps = g_strdup_printf ("d=%s", self->ms_access_token);

  g_autoptr (JsonBuilder) b = json_builder_new ();
  json_builder_begin_object (b);

  json_builder_set_member_name (b, "Properties");
  json_builder_begin_object (b);
  json_builder_set_member_name (b, "AuthMethod");
  json_builder_add_string_value (b, "RPS");
  json_builder_set_member_name (b, "SiteName");
  json_builder_add_string_value (b, "user.auth.xboxlive.com");
  json_builder_set_member_name (b, "RpsTicket");
  json_builder_add_string_value (b, rps);
  json_builder_end_object (b);

  json_builder_set_member_name (b, "RelyingParty");
  json_builder_add_string_value (b, "http://auth.xboxlive.com");
  json_builder_set_member_name (b, "TokenType");
  json_builder_add_string_value (b, "JWT");

  json_builder_end_object (b);

  g_autoptr (JsonGenerator) gen = json_generator_new ();
  json_generator_set_root (gen, json_builder_get_root (b));
  g_autofree char *payload = json_generator_to_data (gen, NULL);

  post_json_async (self, XBL_URL, payload, on_xbl, self);
}

/* ============================================================
 * Step 3：XSTS 授权
 * ============================================================ */

static void
on_xsts (GObject *src, GAsyncResult *res, gpointer data) {
  McAuth *self = data;
  GError *error = NULL;
  GBytes *bytes = soup_session_send_and_read_finish (SOUP_SESSION (src), res, &error);

  if (error) {
    finish_error (self, error->message);
    g_error_free (error);
    return;
  }

  gsize len;
  const char *raw = g_bytes_get_data (bytes, &len);
  g_autoptr (JsonParser) parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, raw, len, NULL)) {
    finish_error (self, "解析 XSTS 响应失败");
    g_bytes_unref (bytes);
    return;
  }

  JsonObject *o = json_node_get_object (json_parser_get_root (parser));

  /* 先检查 XErr，错误响应不会带 DisplayClaims */
  if (json_object_has_member (o, "XErr")) {
    gint64 xerr = json_object_get_int_member (o, "XErr");
    g_autofree char *msg = g_strdup_printf (
        "XSTS 授权失败 (XErr=0x%" G_GINT64_MODIFIER "x)："
        "该账户可能未加入 Xbox 网络、没有 Minecraft Java 版，或需要先在 xbox.com 同意条款",
        xerr);
    finish_error (self, msg);
    g_bytes_unref (bytes);
    return;
  }

  /* 解析 Token */
  if (!json_object_has_member (o, "Token")) {
    finish_error (self, "XSTS 响应缺少 Token");
    g_bytes_unref (bytes);
    return;
  }
  self->xsts_token = g_strdup (json_object_get_string_member (o, "Token"));

  /* 解析 DisplayClaims → xui[0].uhs */
  if (!json_object_has_member (o, "DisplayClaims")) {
    finish_error (self, "XSTS 响应缺少 DisplayClaims（账户可能未完成 Xbox 注册）");
    g_bytes_unref (bytes);
    return;
  }

  JsonObject *dc = json_object_get_object_member (o, "DisplayClaims");
  if (!dc || !json_object_has_member (dc, "xui")) {
    finish_error (self, "XSTS 响应中 DisplayClaims 缺少 xui 数组");
    g_bytes_unref (bytes);
    return;
  }

  JsonArray *xui = json_object_get_array_member (dc, "xui");
  if (json_array_get_length (xui) == 0) {
    finish_error (self, "XSTS 响应的 xui 数组为空");
    g_bytes_unref (bytes);
    return;
  }

  JsonObject *first = json_array_get_object_element (xui, 0);
  if (!json_object_has_member (first, "uhs")) {
    finish_error (self, "XSTS 响应的 xui[0] 缺少 uhs 字段");
    g_bytes_unref (bytes);
    return;
  }

  self->uhs = g_strdup (json_object_get_string_member (first, "uhs"));

  g_bytes_unref (bytes);
  auth_step_mc (self);
}

static void
auth_step_xsts (McAuth *self) {
  g_autoptr (JsonBuilder) b = json_builder_new ();
  json_builder_begin_object (b);

  json_builder_set_member_name (b, "Properties");
  json_builder_begin_object (b);
  json_builder_set_member_name (b, "SandboxId");
  json_builder_add_string_value (b, "RETAIL");
  json_builder_set_member_name (b, "UserTokens");
  json_builder_begin_array (b);
  json_builder_add_string_value (b, self->xbl_token);
  json_builder_end_array (b);
  json_builder_end_object (b);

  json_builder_set_member_name (b, "RelyingParty");
  json_builder_add_string_value (b, "rp://api.minecraftservices.com/");
  json_builder_set_member_name (b, "TokenType");
  json_builder_add_string_value (b, "JWT");

  json_builder_end_object (b);

  g_autoptr (JsonGenerator) gen = json_generator_new ();
  json_generator_set_root (gen, json_builder_get_root (b));
  g_autofree char *payload = json_generator_to_data (gen, NULL);

  post_json_async (self, XSTS_URL, payload, on_xsts, self);
}

/* ============================================================
 * Step 4：Minecraft 登录
 * ============================================================ */

static void
on_mc_login (GObject *src, GAsyncResult *res, gpointer data) {
  McAuth *self = data;
  GError *error = NULL;
  GBytes *bytes = soup_session_send_and_read_finish (SOUP_SESSION (src), res, &error);

  if (error) {
    finish_error (self, error->message);
    g_error_free (error);
    return;
  }

  gsize len;
  const char *raw = g_bytes_get_data (bytes, &len);
  g_autoptr (JsonParser) parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, raw, len, NULL)) {
    finish_error (self, "解析 Minecraft 登录响应失败");
    g_bytes_unref (bytes);
    return;
  }

  JsonObject *o = json_node_get_object (json_parser_get_root (parser));

  if (!json_object_has_member (o, "access_token")) {
    /* 常见原因：没有 Java 版、Azure 应用未获 Minecraft API 审批、账户被限制 */
    finish_error (self, "Minecraft 登录失败：该账户未拥有 Java 版，或应用未获 Minecraft API 权限");
    g_bytes_unref (bytes);
    return;
  }

  self->mc_access_token = g_strdup (
      json_object_get_string_member (o, "access_token"));
  g_bytes_unref (bytes);

  auth_step_profile (self);
}

static void
auth_step_mc (McAuth *self) {
  /* identityToken 格式：XBL3.0 x={uhs};{xsts_token} */
  g_autofree char *identity = g_strdup_printf ("XBL3.0 x=%s;%s",
                                                self->uhs, self->xsts_token);

  g_autoptr (JsonBuilder) b = json_builder_new ();
  json_builder_begin_object (b);
  json_builder_set_member_name (b, "identityToken");
  json_builder_add_string_value (b, identity);
  json_builder_end_object (b);

  g_autoptr (JsonGenerator) gen = json_generator_new ();
  json_generator_set_root (gen, json_builder_get_root (b));
  g_autofree char *payload = json_generator_to_data (gen, NULL);

  post_json_async (self, MC_LOGIN_URL, payload, on_mc_login, self);
}

/* ============================================================
 * Step 5：获取 Player Profile
 * ============================================================ */

static void
on_profile (GObject *src, GAsyncResult *res, gpointer data) {
  McAuth *self = data;
  GError *error = NULL;
  GBytes *bytes = soup_session_send_and_read_finish (SOUP_SESSION (src), res, &error);

  if (error) {
    finish_error (self, error->message);
    g_error_free (error);
    return;
  }

  gsize len;
  const char *raw = g_bytes_get_data (bytes, &len);
  g_autoptr (JsonParser) parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, raw, len, NULL)) {
    finish_error (self, "解析 Profile 失败");
    g_bytes_unref (bytes);
    return;
  }

  JsonObject *o = json_node_get_object (json_parser_get_root (parser));

  if (!json_object_has_member (o, "id") ||
      !json_object_has_member (o, "name")) {
    finish_error (self, "该账户没有 Minecraft Profile（可能未购买 Java 版）");
    g_bytes_unref (bytes);
    return;
  }

  const char *uuid = json_object_get_string_member (o, "id");
  const char *name = json_object_get_string_member (o, "name");

  if (self->done_cb)
    self->done_cb (self->mc_access_token, uuid, name, NULL, self->user_data);

  g_bytes_unref (bytes);
}

static void
auth_step_profile (McAuth *self) {
  get_with_bearer_async (self, MC_PROFILE_URL, self->mc_access_token,
                          on_profile, self);
}

/* ============================================================
 * 入口
 * ============================================================ */

void
mc_auth_start (McAuth *self,
                McAuthDeviceCodeCb device_cb,
                McAuthDoneCb done_cb,
                gpointer user_data) {
  self->device_cb = device_cb;
  self->done_cb   = done_cb;
  self->user_data = user_data;

  g_autofree char *body = g_strdup_printf (
      "client_id=%s&scope=%s", MS_CLIENT_ID, MS_SCOPE);

  post_form_async (self, MS_DEVICE_URL, body, on_device_code, self);
}