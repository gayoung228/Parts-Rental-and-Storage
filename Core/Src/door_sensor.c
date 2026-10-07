#include "door_sensor.h"

static door_state_t state = DOOR_UNKNOWN;
static door_state_t candidate = DOOR_UNKNOWN;
static uint8_t confirmations;

void door_sensor_discard_sample(void)
{
    candidate = DOOR_UNKNOWN;
    confirmations = 0;
}

door_state_t door_sensor_update(uint16_t adc)
{
    door_state_t observed = DOOR_UNKNOWN;
    if (adc <= DOOR_OPEN_ADC_MAX) observed = DOOR_OPEN;
    else if (adc <= 4095U && adc >= DOOR_CLOSED_ADC_MIN) observed = DOOR_CLOSED;
    if (observed == DOOR_UNKNOWN) {
        door_sensor_discard_sample();
        return state; /* Between thresholds, retain the last confirmed state. */
    }
    if (candidate != observed) {
        candidate = observed;
        confirmations = 1;
    } else if (confirmations < DOOR_CONFIRM_SAMPLES) ++confirmations;
    if (confirmations >= DOOR_CONFIRM_SAMPLES) state = candidate;
    return state;
}

const char *door_sensor_state_name(door_state_t value)
{
    if (value == DOOR_OPEN) return "OPEN";
    if (value == DOOR_CLOSED) return "CLOSED";
    return "UNKNOWN";
}
