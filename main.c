#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "core/motor.h"
#include "core/hall.h"
#include "core/protocol.h"

void app_main(void)
{
    motor_init();
    hall_init();
    setvbuf(stdout, NULL, _IONBF, 0);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags < 0 || fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) {
        puts("ERR CONSOLE: motor remains stopped");
        return;
    }
    protocol_init();
    char line[64];
    size_t used = 0;
    int overflow = 0;
    for (;;) {
        char ch;
        /* Bounded input work ensures the heartbeat timeout always runs. */
        for (int n = 0; n < 128 && read(STDIN_FILENO, &ch, 1) == 1; ++n) {
            if (ch == '\n' || ch == '\r') {
                if (overflow) puts("ERR LINE_TOO_LONG");
                else if (used) { line[used] = 0; protocol_command(line); }
                used = 0; overflow = 0;
            } else if (ch == '\0') {
                overflow = 1;
            } else if (!overflow) {
                if (used < sizeof(line) - 1) line[used++] = ch;
                else overflow = 1;
            }
        }
        protocol_tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
