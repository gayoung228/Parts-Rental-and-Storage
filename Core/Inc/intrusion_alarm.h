#ifndef INTRUSION_ALARM_H
#define INTRUSION_ALARM_H
#include "door_sensor.h"
#include <stdint.h>
#define INTRUSION_CONFIRM_MS 1000U
uint8_t intrusion_alarm_update(uint8_t locked, door_state_t door, uint32_t now);
#endif
