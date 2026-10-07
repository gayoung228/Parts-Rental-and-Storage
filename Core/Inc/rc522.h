#ifndef RC522_H
#define RC522_H

#include "main.h"

#define RC522_CS_PORT GPIOB
#define RC522_CS_PIN  GPIO_PIN_12
#define RC522_RST_PORT GPIOC
#define RC522_RST_PIN  GPIO_PIN_4

typedef struct {
    uint8_t bytes[10];
    uint8_t length;
} rc522_uid_t;

typedef struct {
    const char *stage;
    const char *reason;
    uint8_t irq;
    uint8_t error;
    uint8_t fifo_count;
    uint8_t last_bits;
} rc522_diagnostic_t;

/* Initialization returns 0 on success; version is also returned for diagnostics. */
int rc522_init(uint8_t *version);
/* 1: UID read, 0: no card response, -1: SPI/protocol error or multiple cards. */
int rc522_read_uid(rc522_uid_t *uid);
const rc522_diagnostic_t *rc522_diagnostic(void);

#endif
