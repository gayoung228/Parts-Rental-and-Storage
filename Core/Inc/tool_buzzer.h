#ifndef TOOL_BUZZER_H
#define TOOL_BUZZER_H
#include <stdint.h>
#define TOOL_BUZZER_DURATION_MS 150U
int tool_buzzer_init(void);
void tool_buzzer_poll(void);
void tool_buzzer_stop(void);
/* 1: new event chirped, 0: duplicate, -1: invalid event or PWM unavailable. */
int tool_buzzer_event(const char *session, const char *event);
#endif
