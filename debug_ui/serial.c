#include "serial.h"
#include <windows.h>
#include <string.h>
struct Serial {
    GThread *thread;
    GAsyncQueue *tx, *rx;
    char *port;
    gint stop;
    gboolean monitor;
};
static gboolean write_command(HANDLE h, const char *text)
{
    DWORD written;
    DWORD length = (DWORD)strlen(text);
    return WriteFile(h, text, length, &written, NULL) && written == length;
}
static gpointer worker(gpointer data)
{
    Serial *s = data;
    const char *failure = NULL;
    char *path = g_strdup_printf("\\\\.\\%s", s->port);
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, 0, NULL);
    g_free(path);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        failure = error == ERROR_ACCESS_DENIED ?
            "Port is busy or access denied. Close ESP-IDF monitor and other serial apps." :
            "Cannot open port. Check the COM port and USB connection.";
        goto failed;
    }
    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) goto close_failed;
    dcb.BaudRate = CBR_115200; dcb.ByteSize = 8;
    dcb.Parity = NOPARITY; dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE; dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE; dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE; dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE; dcb.fOutX = FALSE; dcb.fInX = FALSE;
    dcb.fAbortOnError = FALSE;
    COMMTIMEOUTS timeouts = {MAXDWORD, 0, 0, 0, 100};
    if (!SetCommState(h, &dcb) || !SetCommTimeouts(h, &timeouts)) goto close_failed;
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    char line[256]; size_t used = 0; gboolean overflow = FALSE;
    gint64 hello = 0, ping = 0, last_state = g_get_monotonic_time();
    gboolean ready = FALSE;
    while (!g_atomic_int_get(&s->stop)) {
        gint64 now = g_get_monotonic_time();
        if (!s->monitor && !ready && now - hello >= 500000) {
            if (!write_command(h, "HELLO\n")) goto close_failed;
            hello = now;
        }
        if (!s->monitor && ready && now - ping >= 400000) {
            if (!write_command(h, "PING\n")) goto close_failed;
            ping = now;
        }
        char *command;
        while ((command = g_async_queue_try_pop(s->tx))) {
            gboolean allowed = ready || s->monitor;
            gboolean ok = allowed && write_command(h, command);
            if (ok && s->monitor && !strcmp(command, "STOP\n"))
                g_async_queue_push(s->rx, g_strdup("STOP_SENT"));
            g_free(command);
            if (allowed && !ok) goto close_failed;
        }
        char bytes[256]; DWORD count;
        if (!ReadFile(h, bytes, sizeof(bytes), &count, NULL)) goto close_failed;
        for (DWORD i = 0; i < count; ++i) {
            char c = bytes[i];
            if (c == '\n') {
                line[used] = 0;
                if (!overflow) {
                    if (!s->monitor && !strcmp(line, "OK HELLO")) {
                        ready = TRUE;
                        g_async_queue_push(s->rx, g_strdup("CONNECTED"));
                    }
                    if (g_str_has_prefix(line, "STATE ")) last_state = now;
                    if (!s->monitor && !strcmp(line, "ERR TIMEOUT")) goto close_failed;
                    g_async_queue_push(s->rx, g_strdup(line));
                }
                used = 0; overflow = FALSE;
            } else if (c != '\r') {
                if (used < sizeof(line) - 1 && c) line[used++] = c;
                else overflow = TRUE;
            }
        }
        if (!s->monitor && now - last_state > 5000000) {
            failure = ready ? "Device stopped responding." :
                "No HELLO response. Flash the current firmware, then reconnect.";
            goto close_failed;
        }
        g_usleep(10000);
    }
    if (!s->monitor) write_command(h, "STOP\nBYE\n");
    CloseHandle(h);
    return NULL;
close_failed:
    if (!s->monitor) write_command(h, "STOP\nBYE\n");
    CloseHandle(h);
failed:
    if (!failure) failure = "Serial communication failed. Check the USB connection.";
    g_async_queue_push(s->rx, g_strdup_printf("CONNECTION_ERROR %s", failure));
    g_async_queue_push(s->rx, g_strdup("DISCONNECTED"));
    return NULL;
}
static Serial *open_port(const char *port, gboolean monitor)
{
    Serial *s = g_new0(Serial, 1);
    s->port = g_strdup(port);
    s->monitor = monitor;
    s->tx = g_async_queue_new_full(g_free);
    s->rx = g_async_queue_new_full(g_free);
    s->thread = g_thread_new("serial", worker, s);
    return s;
}
Serial *serial_open(const char *port) { return open_port(port, FALSE); }
Serial *serial_monitor(const char *port) { return open_port(port, TRUE); }
void serial_send(Serial *s, const char *command)
{
    if (s) g_async_queue_push(s->tx, g_strdup(command));
}
char *serial_receive(Serial *s) { return g_async_queue_try_pop(s->rx); }
void serial_close(Serial *s)
{
    if (!s) return;
    g_atomic_int_set(&s->stop, 1);
    g_thread_join(s->thread);
    g_async_queue_unref(s->tx); g_async_queue_unref(s->rx);
    g_free(s->port); g_free(s);
}
GStrv serial_ports(void)
{
    GPtrArray *ports = g_ptr_array_new();
    for (int i = 1; i <= 256; ++i) {
        char name[16], target[2048];
        g_snprintf(name, sizeof(name), "COM%d", i);
        if (QueryDosDeviceA(name, target, sizeof(target)))
            g_ptr_array_add(ports, g_strdup(name));
    }
    g_ptr_array_add(ports, NULL);
    return (GStrv)g_ptr_array_free(ports, FALSE);
}
