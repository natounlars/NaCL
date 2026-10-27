#include "mc-config.h"

#include <glib/gstdio.h>

struct _McConfig {
    GKeyFile *kf;
    char     *path;
};

#define GROUP_GLOBAL "global"
#define KEY_JAVA_PATH "java-path"
#define KEY_MAX_MEMORY "max-memory"
#define KEY_ISOLATED "isolated"

static char *instance_group (const char *id) {
    return g_strdup_printf ("instance:%s", id);
}

McConfig *mc_config_new (void) {
    McConfig *self = g_new0 (McConfig, 1);
    self->kf = g_key_file_new ();
    self->path = g_build_filename (g_get_user_config_dir (),
                                   "mc-launcher", "config.ini", NULL);
    g_mkdir_with_parents (g_path_get_dirname (self->path), 0755);

    g_key_file_load_from_file (self->kf, self->path, G_KEY_FILE_NONE, NULL);

    if (!g_key_file_has_key (self->kf, GROUP_GLOBAL, KEY_JAVA_PATH, NULL))
        g_key_file_set_string (self->kf, GROUP_GLOBAL, KEY_JAVA_PATH, "java");
    if (!g_key_file_has_key (self->kf, GROUP_GLOBAL, KEY_MAX_MEMORY, NULL))
        g_key_file_set_integer (self->kf, GROUP_GLOBAL, KEY_MAX_MEMORY, 2048);

    return self;
}

void mc_config_free (McConfig *self) {
    if (!self) return;
    g_key_file_free (self->kf);
    g_free (self->path);
    g_free (self);
}

/* 改动 1：返回值由 const char * 改为 char *，调用方负责 g_free */
char *mc_config_get_default_java_path (McConfig *self) {
    return g_key_file_get_string (self->kf, GROUP_GLOBAL, KEY_JAVA_PATH, NULL);
}

int mc_config_get_default_max_memory (McConfig *self) {
    return g_key_file_get_integer (self->kf, GROUP_GLOBAL, KEY_MAX_MEMORY, NULL);
}

/* 改动 2：同上 */
char *mc_config_get_java_path_for_instance (McConfig *self,
                                            const char *instance_id) {
    g_autofree char *group = instance_group (instance_id);
    if (g_key_file_has_key (self->kf, group, KEY_JAVA_PATH, NULL))
        return g_key_file_get_string (self->kf, group, KEY_JAVA_PATH, NULL);
    return mc_config_get_default_java_path (self);
                                            }

                                            int mc_config_get_max_memory_for_instance (McConfig *self,
                                                                                       const char *instance_id) {
                                                g_autofree char *group = instance_group (instance_id);
                                                if (g_key_file_has_key (self->kf, group, KEY_MAX_MEMORY, NULL))
                                                    return g_key_file_get_integer (self->kf, group, KEY_MAX_MEMORY, NULL);
                                                return mc_config_get_default_max_memory (self);
                                                                                       }

                                                                                       gboolean mc_config_instance_is_isolated (McConfig *self,
                                                                                                                                const char *instance_id) {
                                                                                           g_autofree char *group = instance_group (instance_id);
                                                                                           if (g_key_file_has_key (self->kf, group, KEY_ISOLATED, NULL))
                                                                                               return g_key_file_get_boolean (self->kf, group, KEY_ISOLATED, NULL);
                                                                                           return TRUE;
                                                                                                                                }

                                                                                                                                void mc_config_set_java_path_for_instance (McConfig *self,
                                                                                                                                                                           const char *instance_id,
                                                                                                                                                                           const char *java_path) {
                                                                                                                                    g_autofree char *group = instance_group (instance_id);
                                                                                                                                    g_key_file_set_string (self->kf, group, KEY_JAVA_PATH, java_path);
                                                                                                                                    g_key_file_save_to_file (self->kf, self->path, NULL);
                                                                                                                                                                           }

                                                                                                                                                                           void mc_config_set_max_memory_for_instance (McConfig *self,
                                                                                                                                                                                                                       const char *instance_id,
                                                                                                                                                                                                                       int max_memory_mb) {
                                                                                                                                                                               g_autofree char *group = instance_group (instance_id);
                                                                                                                                                                               g_key_file_set_integer (self->kf, group, KEY_MAX_MEMORY, max_memory_mb);
                                                                                                                                                                               g_key_file_save_to_file (self->kf, self->path, NULL);
                                                                                                                                                                                                                       }
