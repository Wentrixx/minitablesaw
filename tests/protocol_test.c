#include <assert.h>
#include <stdint.h>
#include "protocol.h"
static int speed;
static int64_t now;
int64_t esp_timer_get_time(void) { return now; }
void motor_set_speed(int percent) { speed = percent; }
void motor_stop(void) { speed = 0; }
int motor_get_speed(void) { return speed; }
int hall_get_state(void) { return 1; }
int main(void)
{
    protocol_init();
    protocol_command("PWM 50"); assert(speed == 0);
    protocol_command("50"); assert(speed == 0);
    protocol_command("m"); protocol_command("30"); assert(speed == 30);
    protocol_command("h"); assert(speed == 0);
    protocol_command("HELLO"); assert(speed == 0);
    protocol_command("PWM 40"); assert(speed == 40);
    const char *invalid[] = {"PWM 101", "PWM -1", "PWM 4x", "PWM ",
        "PWM 999999999999999999999999", "PWM +50", "50", "junk"};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        protocol_command(invalid[i]); assert(speed == 40);
    }
    now = 1500000; protocol_command("PING");
    now = 3000000; protocol_tick(); assert(speed == 40);
    now = 3500001; protocol_tick(); assert(speed == 0);
    protocol_command("PWM 60"); assert(speed == 0);
    protocol_command("HELLO"); protocol_command("PWM 60"); assert(speed == 60);
    protocol_command("STOP"); assert(speed == 0);
    protocol_command("PWM 60"); protocol_command("BYE"); assert(speed == 0);
    protocol_command("PWM 60"); assert(speed == 0);
    protocol_command("HELLO"); protocol_command("PWM 20"); assert(speed == 20);
    now += 2000001;
    protocol_command("PWM 70"); assert(speed == 0);
    protocol_command("PING"); protocol_command("PWM 70"); assert(speed == 0);
    return 0;
}