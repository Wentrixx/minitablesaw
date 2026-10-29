#pragma once
#include <glib.h>
typedef struct Serial Serial;
Serial *serial_open(const char *port);
Serial *serial_monitor(const char *port);
void serial_send(Serial *serial, const char *command);
char *serial_receive(Serial *serial);
void serial_close(Serial *serial);
GStrv serial_ports(void);
