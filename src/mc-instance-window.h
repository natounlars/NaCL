#pragma once

#include <adwaita.h>
#include "mc-instance.h"
#include "mc-instance-manager.h"

G_BEGIN_DECLS

#define MC_TYPE_INSTANCE_WINDOW (mc_instance_window_get_type())
G_DECLARE_FINAL_TYPE (McInstanceWindow, mc_instance_window, MC,
                       INSTANCE_WINDOW, AdwWindow)

typedef void (*McInstanceWindowSelectedCb) (McInstance *instance, gpointer user_data);

McInstanceWindow *mc_instance_window_new (GtkWindow                 *parent,
                                          McInstanceManager         *manager,
                                          McInstanceWindowSelectedCb cb,
                                          gpointer                   user_data);

G_END_DECLS