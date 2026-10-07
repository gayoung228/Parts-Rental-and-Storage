#include "lcd1602.h"
#include "i2c.h"
#include <stddef.h>
#include <string.h>

#define LCD_RS 0x01U
#define LCD_EN 0x04U
#define LCD_LIGHT 0x08U
static uint8_t device, ready, scrolling;
static char row1[17], row2[64];
static size_t row2_length, offset;
static uint32_t scroll_tick;

static int nibble(uint8_t value)
{
    uint8_t bytes[3] = {(uint8_t)(value | LCD_LIGHT),
                        (uint8_t)(value | LCD_LIGHT | LCD_EN), (uint8_t)(value | LCD_LIGHT)};
    return HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)(device << 1U), bytes, 3, 10) == HAL_OK ? 0 : -1;
}
static int byte(uint8_t value, uint8_t data)
{
    uint8_t rs = data ? LCD_RS : 0;
    uint8_t high = (uint8_t)((value & 0xF0U) | LCD_LIGHT | rs);
    uint8_t low = (uint8_t)(((value << 4U) & 0xF0U) | LCD_LIGHT | rs);
    uint8_t bytes[6] = {high, (uint8_t)(high | LCD_EN), high, low, (uint8_t)(low | LCD_EN), low};
    if (HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)(device << 1U), bytes, 6, 10) != HAL_OK) return -1;
    if (!data && (value == 1U || value == 2U)) HAL_Delay(2);
    return 0;
}
static int write_row(uint8_t row, const char text[16])
{
    if (byte(row ? 0xC0U : 0x80U, 0)) return -1;
    for (uint8_t i = 0; i < 16U; ++i) if (byte((uint8_t)text[i], 1)) return -1;
    return 0;
}
static int render_second(void)
{
    char visible[16];
    for (size_t i = 0; i < sizeof(visible); ++i) {
        size_t position = scrolling ? (offset + i) % (row2_length + 4U) : i;
        visible[i] = position < row2_length ? row2[position] : ' ';
    }
    return write_row(1, visible);
}
int lcd1602_init(uint8_t *address)
{
    ready = scrolling = 0;
    device = 0;
    if (address) *address = 0;
    if (LCD1602_ADDRESS != 0U) {
        if (HAL_I2C_IsDeviceReady(&hi2c1, LCD1602_ADDRESS << 1U, 2, 10) == HAL_OK)
            device = LCD1602_ADDRESS;
    } else {
        const uint8_t preferred[] = {0x27, 0x3F};
        for (unsigned i = 0; i < sizeof(preferred) && !device; ++i)
            if (HAL_I2C_IsDeviceReady(&hi2c1, preferred[i] << 1U, 2, 10) == HAL_OK) device = preferred[i];
        for (uint8_t candidate = 0x20; candidate <= 0x3F && !device; ++candidate) {
            if (candidate > 0x27 && candidate < 0x38) continue;
            if (HAL_I2C_IsDeviceReady(&hi2c1, candidate << 1U, 2, 10) == HAL_OK) device = candidate;
        }
    }
    if (!device) return -1;
    if (address) *address = device;
    HAL_Delay(50);
    if (nibble(0x30)) return -1;
    HAL_Delay(5);
    if (nibble(0x30)) return -1;
    HAL_Delay(1);
    if (nibble(0x30)) return -1;
    HAL_Delay(1);
    if (nibble(0x20)) return -1;
    if (byte(0x28, 0) || byte(0x08, 0) || byte(0x01, 0) || byte(0x06, 0) || byte(0x0C, 0)) return -1;
    ready = 1;
    return 0;
}
int lcd1602_show(const char *first, const char *second, uint8_t scroll_second)
{
    if (!ready || !first || !second) return -1;
    size_t first_length = strlen(first), second_length = strlen(second);
    if (first_length > 16U || second_length >= sizeof(row2)) return -1;
    memset(row1, ' ', 16);
    memcpy(row1, first, first_length);
    row1[16] = '\0';
    memcpy(row2, second, second_length + 1U);
    row2_length = second_length;
    scrolling = scroll_second && row2_length > 16U;
    offset = 0;
    scroll_tick = HAL_GetTick();
    if (write_row(0, row1) || render_second()) { ready = 0; return -1; }
    return 0;
}
void lcd1602_poll(void)
{
    if (!ready || !scrolling) return;
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - scroll_tick) < LCD1602_SCROLL_MS) return;
    offset = (offset + 1U) % (row2_length + 4U);
    scroll_tick = now;
    if (render_second()) ready = 0;
}
