#pragma once

#include <adwaita.h>
#include "mc-config.h"

G_BEGIN_DECLS

#define MC_TYPE_APPLICATION (mc_application_get_type())
G_DECLARE_FINAL_TYPE (McApplication, mc_application, MC, APPLICATION, AdwApplication)

McConfig *mc_application_get_config (McApplication *self);

G_END_DECLS
