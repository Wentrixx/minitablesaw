#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "esp_timer.h"
#include "motor.h"
#include "hall.h"
#include "protocol.h"
static int motor_mode;
static int gui_session;
static int64_t heartbeat;
static int64_t report;
static int last_hall = -1;
static void status(void)
{
    printf("STATE PWM=%d HALL=%d MODE=%s\n", motor_get_speed(),
           hall_get_state(), motor_mode ? "MOTOR" : "HALL");
    fflush(stdout);
}
void protocol_init(void)
{
    puts("Mini Table Saw: m / h / s / 0-100; GUI: HELLO / PING / PWM n / STOP / BYE");
    status();
}
void protocol_command(const char *line)
{
    protocol_tick();
    if (!strcmp(line, "HELLO")) {
        motor_stop(); motor_mode = 0; gui_session = 1;
        heartbeat = esp_timer_get_time(); puts("OK HELLO");
    } else if (!strcmp(line, "PING")) {
        if (gui_session) heartbeat = esp_timer_get_time();
        else puts("ERR SESSION");
    } else if (!strcmp(line, "STOP") || !strcmp(line, "s")) {
        motor_stop();
    } else if (!strcmp(line, "BYE")) {
        motor_stop(); gui_session = 0; motor_mode = 0;
    } else if (!strcmp(line, "h")) {
        motor_stop(); motor_mode = 0;
    } else if (!strcmp(line, "m")) {
        motor_mode = 1;
    } else {
        int structured = !strncmp(line, "PWM ", 4);
        const char *number = structured ? line + 4 : line;
        char *end;
        errno = 0;
        long percent = strtol(number, &end, 10);
        if (errno || end == number || *end || percent < 0 || percent > 100 ||
            *number < '0' || *number > '9' ||
            (structured ? !gui_session : (!motor_mode || gui_session))) {
            puts("ERR COMMAND");
        } else {
            if (structured) motor_mode = 1;
            motor_set_speed((int)percent);
        }
    }
    status();
}
void protocol_tick(void)
{
    int64_t now = esp_timer_get_time();
    if (gui_session && now - heartbeat > 2000000) {
        motor_stop(); gui_session = 0; motor_mode = 0;
        puts("ERR TIMEOUT");
        status();
    }
    int hall = hall_get_state();
    if (hall != last_hall || now - report >= 500000) {
        status(); last_hall = hall; report = now;
    }
}
