#ifndef LCD1602_H
#define LCD1602_H
#include <stdint.h>

/* 0: probe common PCF8574/PCF8574A addresses. Otherwise use a 7-bit address. */
#define LCD1602_ADDRESS 0U
#define LCD1602_COLUMNS 16U
#define LCD1602_SCROLL_MS 350U
/* Common backpack: P0 RS, P1 RW, P2 EN, P3 backlight, P4..P7 data. */
int lcd1602_init(uint8_t *address);
int lcd1602_show(const char *first, const char *second, uint8_t scroll_second);
void lcd1602_poll(void);
#endif
