#include "intrusion_alarm.h"
static uint8_t active, candidate;
static uint32_t candidate_tick;
uint8_t intrusion_alarm_update(uint8_t locked, door_state_t door, uint32_t now)
{
    if (!locked || door == DOOR_CLOSED) {
        active = candidate = 0;
    } else if (door == DOOR_OPEN) {
        if (!candidate) { candidate = 1; candidate_tick = now; }
        if ((uint32_t)(now - candidate_tick) >= INTRUSION_CONFIRM_MS) active = 1;
    } else {
        candidate = 0; /* Unknown measurement must not clear an existing alarm. */
    }
    return active;
}
