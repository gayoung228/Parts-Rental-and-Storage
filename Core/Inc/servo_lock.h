#ifndef SERVO_LOCK_H
#define SERVO_LOCK_H
#include <stdint.h>

/* Bench-test positions. Adjust to the actual lock mechanism after testing. */
#define SERVO_LOCK_PULSE_US   1000U
#define SERVO_UNLOCK_PULSE_US 2000U
#define SERVO_AUTH_TIMEOUT_MS 10000U

int servo_lock_init(void);
void servo_lock_update(void);
int servo_lock_is_unlocked(void);
const char *servo_lock_active_uid(void);
int servo_lock_card_can_operate(const char *uid);
void servo_lock_expect(const char *uid, const char *request_id);
void servo_lock_cancel_request(void);
/* 1: unlocked, 2: locked, 0: denied/error, -1: invalid/expired, -2: another card owns the session. */
int servo_lock_authorize(const char *uid, const char *request_id, const char *status);
#endif
