#ifndef ESP_H
#define ESP_H

#include "stm32f4xx_hal.h"

/* USART6 probes 38400/115200 baud for AT replies; module settings stay unchanged. */
#define ESP_WIFI_SSID       "kcci603"
#define ESP_WIFI_PASSWORD   "@kcci603!"
#define ESP_SERVER_HOST     "10.10.16.80" /*라즈베리파이 서버 IP 주소*/
#define ESP_SERVER_PORT     5000
/* Optional legacy server login, e.g. "[device:password]". */
#define ESP_SERVER_LOGIN    ""
#define ESP_DEVICE_ID       "LOCKER_101"
#define ESP_MESSAGE_SIZE    256

int drv_esp_init(void);
int esp_client_conn(void);
int esp_get_status(void);
int esp_send_data(const char *message);
void esp_poll(void);
void esp_print_rx_diagnostic(void);
int esp_read_message(char *message, uint16_t capacity);

#endif
