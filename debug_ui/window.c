#include "window.h"
#include "serial.h"
#include <string.h>
#include <stdarg.h>
typedef struct {
    Serial *serial;
    GtkWidget *port, *connect, *status;
    GtkWidget *ports, *build, *run, *stop;
    GtkWidget *log_view;
    GSubprocess *process;
    GDataInputStream *output;
    gboolean healthy, built, busy, flashing, monitoring;
    gboolean stopping, stop_sent, disconnect_after_stop;
    gint64 stop_deadline;
    guint timer;
} Window;
static void controls(Window *w);
static void stop_clicked(GtkButton *button, gpointer data);
static void log_event(Window *w, const char *level, const char *format, ...)
{
    if (!w->log_view) return;
    va_list args;
    va_start(args, format);
    char *message = g_strdup_vprintf(format, args);
    va_end(args);
    char *valid = g_utf8_make_valid(message, -1);
    GDateTime *now = g_date_time_new_now_local();
    char *stamp = g_date_time_format(now, "%H:%M:%S");
    char *line = g_strdup_printf("[%s] %-7s %s\n", stamp, level, valid);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->log_view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert_with_tags_by_name(buffer, &end, line, -1, level, NULL);
    int excess = gtk_text_buffer_get_line_count(buffer) - 2001;
    if (excess > 0) {
        GtkTextIter start, cutoff;
        gtk_text_buffer_get_start_iter(buffer, &start);
        gtk_text_buffer_get_iter_at_line(buffer, &cutoff, excess);
        gtk_text_buffer_delete(buffer, &start, &cutoff);
    }
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_place_cursor(buffer, &end);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(w->log_view), gtk_text_buffer_get_insert(buffer));
    g_free(line); g_free(stamp); g_date_time_unref(now); g_free(valid); g_free(message);
}
static void health_status(Window *w, gboolean success, const char *detail)
{
    w->healthy = success;
    controls(w);
    if (success) gtk_widget_add_css_class(w->status, "healthy");
    else gtk_widget_remove_css_class(w->status, "healthy");
    gtk_widget_set_tooltip_text(w->status, detail);
}
static void controls(Window *w)
{
    if (!w->connect) return;
    gboolean idle = !w->busy && !w->serial && !w->stopping;
    gtk_widget_set_sensitive(w->port, idle && !w->healthy);
    gtk_widget_set_sensitive(w->ports, idle && !w->healthy);
    gtk_button_set_label(GTK_BUTTON(w->connect), w->healthy ? "Disconnect" : "Connect");
    gtk_widget_set_tooltip_text(w->connect, w->healthy ? "Disconnect ESP" : "Check ESP health (115200 baud)");
    gtk_widget_set_sensitive(w->connect, !w->busy && !w->stopping && (idle || w->monitoring));
    if (w->build) gtk_widget_set_sensitive(w->build, w->healthy && idle);
    if (w->run) gtk_widget_set_sensitive(w->run, w->healthy && w->built && idle);
    if (w->stop) gtk_widget_set_sensitive(w->stop, w->monitoring && !w->busy && !w->stopping);
}
static void stop_monitor(Window *w)
{
    if (!w->monitoring) return;
    serial_close(w->serial);
    w->serial = NULL;
    w->monitoring = FALSE;
    w->stopping = FALSE;
    w->stop_sent = FALSE;
    w->disconnect_after_stop = FALSE;
    log_event(w, "INFO", "Serial monitor stopped; port released.");
}
static void finish_check(Window *w, gboolean success, const char *detail)
{
    serial_close(w->serial);
    w->serial = NULL;
    w->monitoring = FALSE;
    w->stopping = FALSE;
    w->stop_sent = FALSE;
    controls(w);
    health_status(w, success, detail);
    log_event(w, success ? "SUCCESS" : "ERROR", "%s", detail);
}
static void port_changed(GtkEditable *editable, gpointer data)
{
    stop_monitor(data);
    health_status(data, FALSE, "ESP health check has not been run for this port.");
    log_event(data, "INFO", "Port changed: %s. Health check required.", gtk_editable_get_text(editable));
}
static void port_selected(GObject *object, GParamSpec *pspec, gpointer data)
{
    (void)pspec;
    Window *w = data;
    GtkStringObject *item = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(object));
    if (item && !w->serial)
        gtk_editable_set_text(GTK_EDITABLE(w->port), gtk_string_object_get_string(item));
}
static void connect_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    Window *w = data;
    if (w->busy || w->stopping) return;
    if (w->healthy) {
        if (w->monitoring) {
            w->disconnect_after_stop = TRUE;
            log_event(w, "INFO", "Disconnect requested; stopping ESP first.");
            stop_clicked(NULL, w);
            return;
        }
        health_status(w, FALSE, "Disconnected.");
        log_event(w, "INFO", "Disconnected. Connect is available again.");
        return;
    }
    stop_monitor(w);
    if (w->serial) return;
    const char *port = gtk_editable_get_text(GTK_EDITABLE(w->port));
    if (!*port) {
        health_status(w, FALSE, "Select a COM port first.");
        log_event(w, "ERROR", "Connect: select a COM port first.");
        return;
    }
    log_event(w, "INFO", "Connecting to %s at 115200 baud; checking ESP response...", port);
    w->serial = serial_open(port);
    controls(w);
    health_status(w, FALSE, "Checking ESP response...");
}
static gboolean poll_serial(gpointer data)
{
    Window *w = data;
    if (!w->serial) return G_SOURCE_CONTINUE;
    if (w->stopping && g_get_monotonic_time() > w->stop_deadline) {
        finish_check(w, FALSE, "Stop not confirmed by ESP. Check device connection.");
        return G_SOURCE_CONTINUE;
    }
    char *line;
    while ((line = serial_receive(w->serial))) {
        if (!strcmp(line, "STOP_SENT")) {
            w->stop_sent = TRUE;
            log_event(w, "INFO", "STOP sent; waiting for PWM=0 confirmation.");
            g_free(line);
            continue;
        }
        if (w->stopping && w->stop_sent && g_str_has_prefix(line, "STATE PWM=0 ")) {
            log_event(w, "RX", "%s", line);
            gboolean disconnect = w->disconnect_after_stop;
            stop_monitor(w);
            if (disconnect) health_status(w, FALSE, "Disconnected.");
            controls(w);
            log_event(w, "SUCCESS", disconnect ? "Stop confirmed. Disconnected." : "Stop confirmed by ESP. Play is available again.");
            g_free(line);
            return G_SOURCE_CONTINUE;
        }
        if (g_str_has_prefix(line, "CONNECTION_ERROR ")) {
            w->monitoring = FALSE;
            finish_check(w, FALSE, line + strlen("CONNECTION_ERROR "));
            g_free(line);
            return G_SOURCE_CONTINUE;
        }
        if (!g_strcmp0(line, "DISCONNECTED")) {
            finish_check(w, FALSE, "ESP health check failed.");
            g_free(line);
            return G_SOURCE_CONTINUE;
        }
        if (!g_strcmp0(line, "CONNECTED")) {
            finish_check(w, TRUE, "Last ESP health check succeeded. Port released.");
            g_free(line);
            return G_SOURCE_CONTINUE;
        }
        log_event(w, g_str_has_prefix(line, "ERR ") ? "ERROR" : "RX", "%s", line);
        g_free(line);
    }
    return G_SOURCE_CONTINUE;
}
static void destroy_window(gpointer data)
{
    Window *w = data;
    g_source_remove(w->timer);
    serial_close(w->serial);
    g_free(w);
}
static GtkWidget *padded_box(GtkOrientation orientation, int spacing, int margin)
{
    GtkWidget *box = gtk_box_new(orientation, spacing);
    gtk_widget_set_margin_top(box, margin);
    gtk_widget_set_margin_bottom(box, margin);
    gtk_widget_set_margin_start(box, margin);
    gtk_widget_set_margin_end(box, margin);
    return box;
}
static void close_action(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void)action; (void)parameter;
    gtk_window_close(GTK_WINDOW(data));
}
static gboolean closing(GtkWindow *window, gpointer data)
{
    (void)window;
    Window *w = data;
    if (w->busy) {
        log_event(w, "WARN", "Wait for the active build/upload to finish before closing.");
        return TRUE;
    }
    if (w->monitoring || w->stopping) {
        log_event(w, "WARN", "Use Stop and wait for confirmation before closing.");
        return TRUE;
    }
    log_event(data, "INFO", "Closing application.");
    return FALSE;
}
static void process_finished(GObject *source, GAsyncResult *result, gpointer data)
{
    Window *w = data;
    GError *error = NULL;
    gboolean ok = g_subprocess_wait_finish(G_SUBPROCESS(source), result, &error);
    ok = ok && g_subprocess_get_successful(w->process);
    if (!ok && !error) {
        if (g_subprocess_get_if_exited(w->process))
            log_event(w, "ERROR", "Process exited with code %d.", g_subprocess_get_exit_status(w->process));
        else log_event(w, "ERROR", "Process terminated unexpectedly.");
    }
    log_event(w, ok ? "SUCCESS" : "ERROR", "%s %s%s%s", w->flashing ? "Upload" : "Build",
        ok ? "finished successfully." : "failed.", error ? " " : "", error ? error->message : "");
    g_clear_error(&error);
    g_clear_object(&w->output);
    g_clear_object(&w->process);
    w->busy = FALSE;
    if (!w->flashing) w->built = ok;
    if (ok && w->flashing) {
        const char *port = gtk_editable_get_text(GTK_EDITABLE(w->port));
        w->monitoring = TRUE;
        w->serial = serial_monitor(port);
        log_event(w, "INFO", "ESP restarted. Listening on %s at 115200 baud.", port);
    }
    controls(w);
}
static void read_output(GObject *source, GAsyncResult *result, gpointer data)
{
    Window *w = data;
    GError *error = NULL;
    gsize length;
    char *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(source), result, &length, &error);
    if (line) {
        g_strchomp(line);
        if (*line) log_event(w, (g_str_has_prefix(line, "ERROR") || strstr(line, "fatal error")) ? "ERROR" : "INFO", "%s", line);
        g_free(line);
        g_data_input_stream_read_line_async(w->output, G_PRIORITY_DEFAULT, NULL, read_output, w);
        return;
    }
    if (error) {
        log_event(w, "ERROR", "Process output: %s", error->message);
        g_clear_error(&error);
    }
    g_subprocess_wait_async(w->process, NULL, process_finished, w);
}
static void start_process(Window *w, gboolean flash)
{
    if (w->busy || w->monitoring || w->stopping || !w->healthy || (flash && !w->built)) return;
    stop_monitor(w);
    if (w->serial) return;
    w->flashing = flash;
    if (!flash) w->built = FALSE;
    const char *port = gtk_editable_get_text(GTK_EDITABLE(w->port));
    log_event(w, "INFO", flash ? "Uploading firmware to %s..." : "Building firmware (core) for %s...", port);
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
    g_subprocess_launcher_set_cwd(launcher, FIRMWARE_ROOT);
    GError *error = NULL;
    w->process = g_subprocess_launcher_spawn(launcher, &error, "powershell.exe", "-NoProfile",
        "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", FIRMWARE_ROOT "/firmware.ps1",
        "-Action", flash ? "flash" : "build", "-Port", port, NULL);
    g_object_unref(launcher);
    if (!w->process) {
        log_event(w, "ERROR", "%s", error->message);
        g_clear_error(&error);
        controls(w);
        return;
    }
    w->busy = TRUE;
    controls(w);
    w->output = g_data_input_stream_new(g_subprocess_get_stdout_pipe(w->process));
    g_data_input_stream_set_newline_type(w->output, G_DATA_STREAM_NEWLINE_TYPE_ANY);
    g_data_input_stream_read_line_async(w->output, G_PRIORITY_DEFAULT, NULL, read_output, w);
}
static void build_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    start_process(data, FALSE);
}
static void run_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    start_process(data, TRUE);
}
static void stop_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    Window *w = data;
    if (!w->monitoring || w->busy || w->stopping) return;
    w->stopping = TRUE;
    w->stop_sent = FALSE;
    w->stop_deadline = g_get_monotonic_time() + 3000000;
    serial_send(w->serial, "STOP\n");
    controls(w);
    log_event(w, "INFO", "Stopping firmware output...");
}
static void clear_log(GtkButton *button, gpointer data)
{
    (void)button;
    Window *w = data;
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->log_view)), "", -1);
    log_event(w, "INFO", "Debug log cleared.");
}
static void about_action(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void)action; (void)parameter;
    log_event(g_object_get_data(G_OBJECT(data), "state"), "INFO", "Help > About opened.");
    gtk_show_about_dialog(GTK_WINDOW(data), "program-name", "Mini Table Saw - Debug",
        "comments", "ESP32 control and diagnostics", NULL);
}
static void apply_style(GtkWidget *window)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        ".engineering { background: #d4d0c8; color: #202020; font-family: 'Segoe UI', sans-serif; font-size: 12px; }"
        ".engineering button, .engineering entry { min-height: 22px; padding: 0 7px; border-radius: 0; box-shadow: none; border: 1px solid #8b8b83; }"
        ".engineering button { background: linear-gradient(#f6f5f1, #d4d0c8); color: #202020; }"
        ".engineering button:hover { background: #e8e6df; }"
        ".engineering button:active { background: #c1beb6; }"
        ".engineering entry { background: #ffffff; color: #202020; }"
        ".engineering :disabled { color: #777770; }"
        ".engineering menubar { background: #d4d0c8; padding: 0; }"
        ".engineering menubar > item { padding: 3px 8px; min-height: 18px; }"
        ".engineering popover contents { background: #eceae4; color: #202020; border-radius: 0; }"
        ".engineering .workspace { background: #a9adaf; border: 1px solid #85898b; }"
        ".engineering .health-led { min-width: 10px; min-height: 10px; border-radius: 50%; background: #bf3030; border: 1px solid #752020; }"
        ".engineering .health-led.healthy { background: #2eaa47; border-color: #216e30; }"
        ".engineering .statusbar { background: #d4d0c8; font-size: 11px; }"
        ".engineering textview, .engineering textview text { background: #f5f4ef; color: #202020; font-family: Consolas, monospace; font-size: 12px; }"
        ".engineering separator { background: #aaa79e; min-width: 1px; min-height: 1px; }");
    gtk_style_context_add_provider_for_display(gtk_widget_get_display(window),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_widget_add_css_class(window, "engineering");
    g_object_unref(provider);
}
void window_activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;
    if (gtk_application_get_active_window(app)) {
        gtk_window_present(gtk_application_get_active_window(app)); return;
    }
    Window *w = g_new0(Window, 1);
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Mini Table Saw - Debug");
    gtk_window_set_default_size(GTK_WINDOW(window), 960, 640);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Header and menu bar. */
    GSimpleAction *quit = g_simple_action_new("close", NULL);
    g_signal_connect(quit, "activate", G_CALLBACK(close_action), window);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(quit));
    g_object_unref(quit);
    GSimpleAction *about = g_simple_action_new("about", NULL);
    g_signal_connect(about, "activate", G_CALLBACK(about_action), window);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(about));
    g_object_unref(about);
    GMenu *menu = g_menu_new(), *file = g_menu_new(), *help = g_menu_new();
    g_menu_append(file, "Quit", "win.close");
    g_menu_append(help, "About", "win.about");
    g_menu_append_submenu(menu, "File", G_MENU_MODEL(file));
    g_menu_append_submenu(menu, "Help", G_MENU_MODEL(help));
    gtk_box_append(GTK_BOX(box), gtk_popover_menu_bar_new_from_model(G_MENU_MODEL(menu)));
    g_object_unref(file); g_object_unref(help); g_object_unref(menu);
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* Connection strip stays visible on every page. */
    GtkWidget *connection = padded_box(GTK_ORIENTATION_HORIZONTAL, 6, 4);
    gtk_box_append(GTK_BOX(connection), gtk_label_new("ESP32  |  Port"));
    GStrv ports = serial_ports();
    GtkWidget *combo = gtk_drop_down_new_from_strings((const char *const *)ports);
    w->ports = combo;
    w->port = gtk_entry_new();
    gtk_editable_set_width_chars(GTK_EDITABLE(w->port), 7);
    gtk_editable_set_text(GTK_EDITABLE(w->port), "COM3");
    for (guint i = 0; ports[i]; ++i)
        if (!g_strcmp0(ports[i], "COM3")) gtk_drop_down_set_selected(GTK_DROP_DOWN(combo), i);
    g_strfreev(ports);
    g_signal_connect(combo, "notify::selected", G_CALLBACK(port_selected), w);
    w->connect = gtk_button_new_with_label("Connect");
    gtk_widget_set_tooltip_text(w->connect, "Check ESP health (115200 baud)");
    w->status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(w->status, "health-led");
    gtk_widget_set_valign(w->status, GTK_ALIGN_CENTER);
    health_status(w, FALSE, "ESP health check has not been run.");
    g_signal_connect(w->port, "changed", G_CALLBACK(port_changed), w);
    gtk_box_append(GTK_BOX(connection), combo);
    gtk_box_append(GTK_BOX(connection), w->port);
    gtk_box_append(GTK_BOX(connection), w->connect);
    gtk_box_append(GTK_BOX(connection), w->status);
    gtk_box_append(GTK_BOX(connection), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
    GtkWidget *build = gtk_button_new_from_icon_name("applications-engineering-symbolic");
    w->build = build;
    gtk_widget_set_tooltip_text(build, "Build core firmware");
    gtk_accessible_update_property(GTK_ACCESSIBLE(build), GTK_ACCESSIBLE_PROPERTY_LABEL, "Build", -1);
    gtk_widget_set_sensitive(build, FALSE);
    gtk_box_append(GTK_BOX(connection), build);
    GtkWidget *run = gtk_button_new_from_icon_name("media-playback-start-symbolic");
    w->run = run;
    gtk_widget_set_tooltip_text(run, "Upload firmware and run on ESP");
    gtk_accessible_update_property(GTK_ACCESSIBLE(run), GTK_ACCESSIBLE_PROPERTY_LABEL, "Run", -1);
    gtk_widget_set_sensitive(run, FALSE);
    gtk_box_append(GTK_BOX(connection), run);
    w->stop = gtk_button_new_from_icon_name("media-playback-stop-symbolic");
    gtk_widget_set_tooltip_text(w->stop, "Stop motor and serial monitoring");
    gtk_accessible_update_property(GTK_ACCESSIBLE(w->stop), GTK_ACCESSIBLE_PROPERTY_LABEL, "Stop", -1);
    gtk_widget_set_sensitive(w->stop, FALSE);
    gtk_box_append(GTK_BOX(connection), w->stop);
    gtk_box_append(GTK_BOX(box), connection);
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* Empty work area, reserved for future pages. */
    GtkWidget *workspace = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(workspace, "workspace");
    gtk_widget_set_vexpand(workspace, TRUE);
    gtk_widget_set_hexpand(workspace, TRUE);
    GtkWidget *split = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_vexpand(split, TRUE);
    gtk_paned_set_start_child(GTK_PANED(split), workspace);
    gtk_paned_set_resize_start_child(GTK_PANED(split), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(split), FALSE);
    gtk_paned_set_wide_handle(GTK_PANED(split), TRUE);
    gtk_paned_set_position(GTK_PANED(split), 360);
    GtkWidget *debug = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *debug_header = padded_box(GTK_ORIENTATION_HORIZONTAL, 8, 3);
    GtkWidget *debug_title = gtk_label_new("DEBUG OUTPUT");
    gtk_widget_set_hexpand(debug_title, TRUE);
    gtk_label_set_xalign(GTK_LABEL(debug_title), 0);
    gtk_box_append(GTK_BOX(debug_header), debug_title);
    GtkWidget *clear = gtk_button_new_from_icon_name("edit-clear-symbolic");
    gtk_widget_set_tooltip_text(clear, "Clear debug log");
    gtk_box_append(GTK_BOX(debug_header), clear);
    gtk_box_append(GTK_BOX(debug), debug_header);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(scroll, -1, 100);
    gtk_widget_set_vexpand(scroll, TRUE);
    w->log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(w->log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(w->log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(w->log_view), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(w->log_view), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(w->log_view), 4);
    GtkTextBuffer *log = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->log_view));
    gtk_text_buffer_create_tag(log, "INFO", NULL);
    gtk_text_buffer_create_tag(log, "RX", "foreground", "#555555", NULL);
    gtk_text_buffer_create_tag(log, "SUCCESS", "foreground", "#216e30", NULL);
    gtk_text_buffer_create_tag(log, "ERROR", "foreground", "#b02020", NULL);
    gtk_text_buffer_create_tag(log, "WARN", "foreground", "#885500", NULL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), w->log_view);
    gtk_box_append(GTK_BOX(debug), scroll);
    gtk_paned_set_end_child(GTK_PANED(split), debug);
    gtk_paned_set_resize_end_child(GTK_PANED(split), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(split), FALSE);
    gtk_box_append(GTK_BOX(box), split);
    g_signal_connect(clear, "clicked", G_CALLBACK(clear_log), w);
    g_signal_connect(build, "clicked", G_CALLBACK(build_clicked), w);
    g_signal_connect(run, "clicked", G_CALLBACK(run_clicked), w);
    g_signal_connect(w->stop, "clicked", G_CALLBACK(stop_clicked), w);
    g_signal_connect(window, "close-request", G_CALLBACK(closing), w);
    log_event(w, "INFO", "Application started. Selected port: %s.", gtk_editable_get_text(GTK_EDITABLE(w->port)));

    /* Persistent bottom status bar. */
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    GtkWidget *footer = padded_box(GTK_ORIENTATION_HORIZONTAL, 8, 3);
    gtk_widget_add_css_class(footer, "statusbar");
    GtkWidget *message = gtk_label_new("Mini Table Saw - Debug");
    gtk_widget_set_hexpand(message, TRUE);
    gtk_label_set_xalign(GTK_LABEL(message), 0);
    gtk_box_append(GTK_BOX(footer), message);
    gtk_box_append(GTK_BOX(box), footer);
    g_signal_connect(w->connect, "clicked", G_CALLBACK(connect_clicked), w);
    controls(w);
    w->timer = g_timeout_add(50, poll_serial, w);
    g_object_set_data_full(G_OBJECT(window), "state", w, destroy_window);
    gtk_window_set_child(GTK_WINDOW(window), box);
    apply_style(window);
    gtk_window_present(GTK_WINDOW(window));
}
