#include "mc-application.h"

int
main (int argc, char *argv[]) {
    g_autoptr (McApplication) app =
    g_object_new (MC_TYPE_APPLICATION,
                  "application-id", "org.example.McLauncher",
                  "flags", G_APPLICATION_DEFAULT_FLAGS,
                  NULL);

    return g_application_run (G_APPLICATION (app), argc, argv);
}
