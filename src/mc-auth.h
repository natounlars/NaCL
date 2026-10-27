#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _McAuth McAuth;

typedef void (*McAuthDeviceCodeCb) (const char *user_code,
                                    const char *verification_uri,
                                    gpointer    user_data);
typedef void (*McAuthDoneCb)       (const char *access_token,
                                    const char *uuid,
                                    const char *name,
                                    GError     *error,
                                    gpointer    user_data);

McAuth *mc_auth_new    (void);
void    mc_auth_free   (McAuth *self);
void    mc_auth_cancel (McAuth *self);

void mc_auth_start (McAuth             *self,
                    McAuthDeviceCodeCb  device_cb,
                    McAuthDoneCb        done_cb,
                    gpointer            user_data);

G_END_DECLS