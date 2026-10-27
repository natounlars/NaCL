#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct _McConfig McConfig;

McConfig *mc_config_new  (void);
void      mc_config_free (McConfig *self);

/* 返回值均由调用方 g_free */
char *mc_config_get_default_java_path  (McConfig *self);
int   mc_config_get_default_max_memory (McConfig *self);

/* 实例未覆盖时回退到全局默认值；返回值由调用方 g_free */
char *mc_config_get_java_path_for_instance  (McConfig *self,
                                             const char *instance_id);
int   mc_config_get_max_memory_for_instance (McConfig *self,
                                             const char *instance_id);
gboolean mc_config_instance_is_isolated     (McConfig *self,
                                             const char *instance_id);

void mc_config_set_java_path_for_instance  (McConfig *self,
                                            const char *instance_id,
                                            const char *java_path);
void mc_config_set_max_memory_for_instance (McConfig *self,
                                            const char *instance_id,
                                            int max_memory_mb);

G_END_DECLS
