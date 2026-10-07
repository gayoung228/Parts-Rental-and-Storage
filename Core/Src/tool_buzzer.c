#include "tool_buzzer.h"
#include "tim.h"
#include <string.h>
#define EVENT_CACHE 8U
static uint8_t ready, sounding, next_event;
static uint32_t started;
static char current_session[17], events[EVENT_CACHE][17];

static int valid_id(const char *id)
{
    return id && strlen(id) == 16U && strspn(id, "0123456789ABCDEF") == 16U;
}
int tool_buzzer_init(void)
{
    ready = sounding = next_event = 0;
    current_session[0] = '\0';
    memset(events, 0, sizeof(events));
    if (htim3.Init.Prescaler != 83U || htim3.Init.Period != 499U) return -1;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, 0);
    if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4) != HAL_OK) return -1;
    ready = 1;
    return 0;
}
void tool_buzzer_stop(void)
{
    if (ready) __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, 0);
    sounding = 0;
}
void tool_buzzer_poll(void)
{
    if (sounding && (uint32_t)(HAL_GetTick() - started) >= TOOL_BUZZER_DURATION_MS) tool_buzzer_stop();
}
int tool_buzzer_event(const char *session, const char *event)
{
    if (!ready || !valid_id(session) || !valid_id(event)) return -1;
    if (strcmp(current_session, session)) {
        strcpy(current_session, session);
        memset(events, 0, sizeof(events));
        next_event = 0;
    }
    for (uint8_t i = 0; i < EVENT_CACHE; ++i) if (!strcmp(events[i], event)) return 0;
    strcpy(events[next_event], event);
    next_event = (uint8_t)((next_event + 1U) % EVENT_CACHE);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, 250);
    started = HAL_GetTick();
    sounding = 1;
    return 1;
}
