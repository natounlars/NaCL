#pragma once

#include <gio/gio.h>
#include "mc-instance.h"

G_BEGIN_DECLS

typedef struct _McInstanceManager McInstanceManager;

McInstanceManager *mc_instance_manager_new  (void);
void               mc_instance_manager_free (McInstanceManager *self);

const char  *mc_instance_manager_get_base_dir (McInstanceManager *self);
GListModel  *mc_instance_manager_scan         (McInstanceManager *self);

McInstance  *mc_instance_manager_create (McInstanceManager *self,
                                          const char        *name,
                                          const char        *mc_version,
                                          const char        *loader,
                                          gboolean           installed,
                                          GError           **error);

McInstance  *mc_instance_manager_import (McInstanceManager *self,
                                          const char        *src_dir,
                                          const char        *name,
                                          GError           **error);

G_END_DECLS