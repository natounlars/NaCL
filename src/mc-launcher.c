#include "mc-launcher.h"
#include "mc-version-meta.h"

struct _McLauncher {
  char *instance_dir;
  char *java_path;
  int   max_memory_mb;
  char *username;
  char *uuid;
  char *access_token;
};

McLauncher *
mc_launcher_new (const char *instance_dir, const char *java_path,
                  int max_memory_mb, const char *username,
                  const char *uuid, const char *access_token) {
  McLauncher *self = g_new0 (McLauncher, 1);
  self->instance_dir  = g_strdup (instance_dir);
  self->java_path     = g_strdup (java_path);
  self->max_memory_mb = max_memory_mb;
  self->username      = g_strdup (username);
  self->uuid          = g_strdup (uuid);
  self->access_token  = g_strdup (access_token);
  return self;
}

void
mc_launcher_free (McLauncher *self) {
  if (!self) return;
  g_free (self->instance_dir);
  g_free (self->java_path);
  g_free (self->username);
  g_free (self->uuid);
  g_free (self->access_token);
  g_free (self);
}

GPtrArray *
mc_launcher_build_argv (McLauncher *self, GError **error) {
  g_autofree char *vjson_path = g_build_filename (self->instance_dir,
                                                    "version.json", NULL);
  g_autofree char *content = NULL;
  if (!g_file_get_contents (vjson_path, &content, NULL, error))
    return NULL;

  g_autoptr (GError) perr = NULL;
  McVersionMeta *meta = mc_version_meta_parse (content, &perr);
  if (!meta) {
    g_propagate_error (error, g_steal_pointer (&perr));
    return NULL;
  }

  g_autoptr (GString) cp = g_string_new (NULL);
  g_autofree char *client_jar = g_build_filename (self->instance_dir,
                                                    "client.jar", NULL);
  g_string_append (cp, client_jar);

  GPtrArray *libs = mc_version_meta_get_libraries (meta);
  for (guint i = 0; i < libs->len; i++) {
    McLibrary *l = g_ptr_array_index (libs, i);
    g_autofree char *jar = g_build_filename (self->instance_dir,
                                               "libraries",
                                               mc_library_get_path (l), NULL);
    g_string_append_c (cp, ':');
    g_string_append (cp, jar);
  }

  GPtrArray *argv = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (argv, g_strdup (self->java_path));
  g_ptr_array_add (argv, g_strdup_printf ("-Xmx%dm", self->max_memory_mb));
  g_ptr_array_add (argv, g_strdup_printf ("-Xms%dm", self->max_memory_mb / 2));

  g_autofree char *natives = g_build_filename (self->instance_dir, "natives", NULL);
  g_ptr_array_add (argv, g_strdup_printf ("-Djava.library.path=%s", natives));

  g_ptr_array_add (argv, g_strdup ("-cp"));
  g_ptr_array_add (argv, g_strdup (cp->str));

  const char *main_class = mc_version_meta_get_main_class (meta);
  g_ptr_array_add (argv, g_strdup (main_class ? main_class
                                               : "net.minecraft.client.main.Main"));

  g_ptr_array_add (argv, g_strdup ("--username"));
  g_ptr_array_add (argv, g_strdup (self->username ? self->username : "Player"));
  g_ptr_array_add (argv, g_strdup ("--version"));
  const char *vid = mc_version_meta_get_id (meta);
  g_ptr_array_add (argv, g_strdup (vid ? vid : "unknown"));

  g_ptr_array_add (argv, g_strdup ("--gameDir"));
  g_ptr_array_add (argv, g_strdup (self->instance_dir));

  g_autofree char *assets = g_build_filename (self->instance_dir, "assets", NULL);
  g_ptr_array_add (argv, g_strdup ("--assetsDir"));
  g_ptr_array_add (argv, g_strdup (assets));

  const char *aidx = mc_version_meta_get_assets_id (meta);
  if (aidx) {
    g_ptr_array_add (argv, g_strdup ("--assetIndex"));
    g_ptr_array_add (argv, g_strdup (aidx));
  }

  g_ptr_array_add (argv, g_strdup ("--uuid"));
  g_ptr_array_add (argv, g_strdup (self->uuid ? self->uuid : "0"));
  g_ptr_array_add (argv, g_strdup ("--accessToken"));
  g_ptr_array_add (argv, g_strdup (self->access_token ? self->access_token : "0"));
  g_ptr_array_add (argv, g_strdup ("--userType"));
  g_ptr_array_add (argv, g_strdup ("legacy"));
  g_ptr_array_add (argv, NULL);

  g_object_unref (meta);
  return argv;
}