#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _McLauncher McLauncher;

McLauncher *mc_launcher_new (const char *instance_dir,
                              const char *java_path,
                              int         max_memory_mb,
                              const char *username,
                              const char *uuid,
                              const char *access_token);

void mc_launcher_free (McLauncher *self);

GPtrArray *mc_launcher_build_argv (McLauncher *self, GError **error);

G_END_DECLS