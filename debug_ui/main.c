#include "window.h"
int main(int argc, char **argv)
{
    GtkApplication *app = gtk_application_new("com.minitablesaw.gui", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(window_activate), NULL);
    int result = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return result;
}
