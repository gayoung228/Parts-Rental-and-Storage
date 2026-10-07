#ifndef DOOR_SENSOR_H
#define DOOR_SENSOR_H
#include <stdint.h>

#define DOOR_OPEN_ADC_MAX 1500U
#define DOOR_CLOSED_ADC_MIN 3000U
#define DOOR_CONFIRM_SAMPLES 3U

typedef enum { DOOR_UNKNOWN, DOOR_OPEN, DOOR_CLOSED } door_state_t;
door_state_t door_sensor_update(uint16_t adc);
void door_sensor_discard_sample(void);
const char *door_sensor_state_name(door_state_t state);
#endif
