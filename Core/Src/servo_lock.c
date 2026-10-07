#include "servo_lock.h"
#include "tim.h"
#include <string.h>

static uint8_t ready, unlocked, awaiting_auth;
static uint32_t request_tick;
static char expected_uid[21], expected_request[17];
static char active_uid[21];

int servo_lock_init(void)
{
    ready = unlocked = awaiting_auth = 0;
    active_uid[0] = '\0';
    if (htim1.Init.Prescaler != 83U || htim1.Init.Period != 19999U) return -1;
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, SERVO_LOCK_PULSE_US);
    if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK) return -1;
    ready = 1;
    return 0;
}

void servo_lock_update(void)
{
    uint32_t now = HAL_GetTick();
    if (awaiting_auth && (uint32_t)(now - request_tick) >= SERVO_AUTH_TIMEOUT_MS)
        awaiting_auth = 0;
}

int servo_lock_is_unlocked(void)
{
    servo_lock_update();
    return unlocked;
}

const char *servo_lock_active_uid(void)
{
    return active_uid;
}

int servo_lock_card_can_operate(const char *uid)
{
    return uid != NULL && (!unlocked || strcmp(uid, active_uid) == 0);
}

void servo_lock_cancel_request(void)
{
    awaiting_auth = 0;
}

void servo_lock_expect(const char *uid, const char *request_id)
{
    awaiting_auth = 0;
    if (uid == NULL || request_id == NULL || strlen(uid) >= sizeof(expected_uid) ||
        strlen(request_id) != 16U) return;
    strcpy(expected_uid, uid);
    strcpy(expected_request, request_id);
    request_tick = HAL_GetTick();
    awaiting_auth = 1;
}

int servo_lock_authorize(const char *uid, const char *request_id, const char *status)
{
    servo_lock_update();
    if (!awaiting_auth || uid == NULL || request_id == NULL || status == NULL ||
        strcmp(uid, expected_uid) != 0 || strcmp(request_id, expected_request) != 0) return -1;
    if (strcmp(status, "DENIED") == 0 || strcmp(status, "ERROR") == 0) {
        awaiting_auth = 0;
        return 0;
    }
    if (strcmp(status, "APPROVED") != 0 || !ready) return -1;
    awaiting_auth = 0; /* Consume once: duplicate responses must never toggle again. */
    if (unlocked) {
        if (strcmp(uid, active_uid) != 0) return -2;
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, SERVO_LOCK_PULSE_US);
        unlocked = 0;
        active_uid[0] = '\0';
        return 2;
    }
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, SERVO_UNLOCK_PULSE_US);
    strcpy(active_uid, uid);
    unlocked = 1;
    return 1;
}
