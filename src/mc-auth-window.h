#pragma once

#include <adwaita.h>
#include "mc-auth.h"

G_BEGIN_DECLS

#define MC_TYPE_AUTH_WINDOW (mc_auth_window_get_type())
G_DECLARE_FINAL_TYPE (McAuthWindow, mc_auth_window, MC,
                       AUTH_WINDOW, AdwWindow)

typedef void (*McAuthWindowDoneCb) (const char *token, const char *uuid,
                                     const char *name, GError *error,
                                     gpointer user_data);

McAuthWindow *mc_auth_window_new (GtkWindow          *parent,
                                   McAuthWindowDoneCb  cb,
                                   gpointer            user_data);

G_END_DECLS