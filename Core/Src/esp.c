#include "esp.h"
#include "usart.h"
#include "servo_lock.h"
#include "lcd1602.h"
#include "tool_buzzer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX_SIZE 2048U
#define QUEUE_SIZE 8U
#define EVENT_OK 1U
#define EVENT_ERROR 2U
#define EVENT_PROMPT 4U
#define EVENT_SENT 8U

/* ISR writes bytes; only the main context parses AT replies and TCP data. */
static uint8_t rx_byte;
static uint8_t rx_buffer[RX_SIZE];
static volatile uint16_t rx_head, rx_tail;
static volatile uint8_t rx_fault;
static volatile uint32_t rx_count;
static volatile uint32_t uart_error;
static uint32_t events;
static uint8_t connected;
static char at_line[256];
static size_t at_length;
static uint32_t payload_remaining;
static char payload_line[ESP_MESSAGE_SIZE];
static size_t payload_length;
static uint8_t payload_discard;
static char messages[QUEUE_SIZE][ESP_MESSAGE_SIZE];
static uint8_t queue_head, queue_tail;
static const char *wait_failure;
static uint32_t ipd_frames, payload_bytes, queued_lines, dropped_lines, ipd_resyncs;

static void finish_at_line(void)
{
    at_line[at_length] = '\0';
    if (strcmp(at_line, "OK") == 0) events |= EVENT_OK;
    if (strcmp(at_line, "SEND OK") == 0) events |= EVENT_SENT;
    if (strcmp(at_line, "ERROR") == 0 || strcmp(at_line, "FAIL") == 0 ||
        strcmp(at_line, "SEND FAIL") == 0 || strncmp(at_line, "busy", 4) == 0)
        events |= EVENT_ERROR;
    if (strcmp(at_line, "CONNECT") == 0) connected = 1;
    if (strcmp(at_line, "CLOSED") == 0 ||
        strcmp(at_line, "WIFI DISCONNECT") == 0 || strcmp(at_line, "ready") == 0)
    {
        connected = 0;
        payload_length = 0;
        payload_discard = 0;
    }
    /* AT+CIPSTATUS is authoritative for half-open connections. */
    if (strncmp(at_line, "STATUS:", 7) == 0)
        connected = (strcmp(at_line, "STATUS:3") == 0);
    at_length = 0;
}

void esp_poll(void)
{
    /* Expire pending authorization requests even during blocking AT waits. */
    servo_lock_update();
    tool_buzzer_poll();
    while (rx_tail != rx_head)
    {
        uint8_t ch = rx_buffer[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1U) % RX_SIZE);
        if (payload_remaining != 0)
        {
            --payload_remaining;
            ++payload_bytes;
            if (ch == '\n')
            {
                uint8_t next = (uint8_t)((queue_head + 1U) % QUEUE_SIZE);
                if (!payload_discard && payload_length != 0)
                {
                    payload_line[payload_length] = '\0';
                    if (next != queue_tail)
                    {
                        memcpy(messages[queue_head], payload_line, payload_length + 1U);
                        queue_head = next;
                        ++queued_lines;
                    }
                    else {
                        ++dropped_lines;
                        printf("Wi-Fi message queue full; message dropped\r\n");
                    }
                }
                else if (payload_discard) ++dropped_lines;
                payload_length = 0;
                payload_discard = 0;
            }
            else if (ch != '\r')
            {
                if (ch == 0 || payload_length >= sizeof(payload_line) - 1U)
                    payload_discard = 1;
                else if (!payload_discard) payload_line[payload_length++] = (char)ch;
            }
            continue;
        }
        if (ch == '>' && at_length == 0)
        {
            events |= EVENT_PROMPT;
            continue;
        }
        if (ch == '\r') continue;
        if (ch == '\n')
        {
            finish_at_line();
            continue;
        }
        /* CIPSEND prompt may leave a single space before the next reply. */
        if (ch == ' ' && at_length == 0) continue;
        if (at_length >= sizeof(at_line) - 1U)
        {
            rx_fault = 1;
            continue;
        }
        at_line[at_length++] = (char)ch;
        at_line[at_length] = '\0';
        char *ipd = ch == ':' ? strstr(at_line, "+IPD,") : NULL;
        if (ipd != NULL)
        {
            char *end;
            at_line[at_length - 1U] = '\0';
            if (ipd != at_line) {
                /* Preserve a preceding AT completion token when no CRLF separates it. */
                at_length = (size_t)(ipd - at_line);
                finish_at_line();
                *ipd = '+';
                ++ipd_resyncs;
            }
            char *length_start = ipd + 5;
            unsigned long count = strtoul(length_start, &end, 10);
            /* Also accept the link-ID form +IPD,0,<length>: used by some firmware. */
            if (end != length_start && *end == ',' && count <= 4UL) {
                length_start = end + 1;
                count = strtoul(length_start, &end, 10);
            }
            if (end == length_start || *end != '\0' || count > 65535UL) {
                rx_fault = 1;
                printf("[ESP RX] Invalid IPD header\r\n");
            }
            else {
                payload_remaining = (uint32_t)count;
                ++ipd_frames;
            }
            at_length = 0;
        }
    }
    if (rx_fault) connected = 0;
    lcd1602_poll();
}

void esp_print_rx_diagnostic(void)
{
    printf("[ESP RX] uart=%lu ipd=%lu payload=%lu queued=%lu dropped=%lu resync=%lu\r\n",
           (unsigned long)rx_count, (unsigned long)ipd_frames, (unsigned long)payload_bytes,
           (unsigned long)queued_lines, (unsigned long)dropped_lines, (unsigned long)ipd_resyncs);
    printf("[ESP RX] remaining=%lu partial=%u fault=%u uart_error=0x%08lX\r\n",
           (unsigned long)payload_remaining, (unsigned)payload_length,
           (unsigned)rx_fault, (unsigned long)uart_error);
}

static int wait_event(uint32_t expected, uint32_t timeout)
{
    uint32_t start = HAL_GetTick();
    do
    {
        esp_poll();
        if (rx_fault) {
            wait_failure = "UART receive/parser fault";
            return -1;
        }
        if (events & EVENT_ERROR) {
            wait_failure = "ESP returned ERROR/FAIL/busy";
            return -1;
        }
        if (events & expected) return 0;
        HAL_Delay(1);
    } while ((uint32_t)(HAL_GetTick() - start) < timeout);
    wait_failure = "response timeout";
    return -1;
}

static int command(const char *text, uint32_t expected, uint32_t timeout)
{
    /* Print only the command name, never SSID/password/login arguments. */
    char name[32];
    size_t name_length = strcspn(text, "=\r\n");
    if (name_length >= sizeof(name)) name_length = sizeof(name) - 1U;
    memcpy(name, text, name_length);
    name[name_length] = '\0';
    esp_poll();
    events = 0;
    uint32_t before = rx_count;
    printf("[ESP] %s ...\r\n", name);
    if (HAL_UART_Transmit(&huart6, (uint8_t *)text, (uint16_t)strlen(text), 1000) != HAL_OK) {
        printf("[ESP] %s FAILED: UART transmit\r\n", name);
        return -1;
    }
    int result = wait_event(expected, timeout);
    if (result == 0) printf("[ESP] %s OK\r\n", name);
    else printf("[ESP] %s FAILED: %s; rx=%lu, uart_error=0x%08lX\r\n",
                name, wait_failure, (unsigned long)(rx_count - before),
                (unsigned long)uart_error);
    return result;
}

static int start_reception(void)
{
    if (HAL_UART_AbortReceive(&huart6) != HAL_OK) {
        printf("[ESP] FAILED: cannot stop USART6 reception\r\n");
        return -1;
    }
    /* F4: reading SR then DR clears pending RXNE/ORE/FE/NE/PE flags. */
    __HAL_UART_CLEAR_OREFLAG(&huart6);
    rx_head = rx_tail = 0;
    rx_fault = 0;
    rx_count = 0;
    uart_error = 0;
    connected = 0;
    events = 0;
    at_length = payload_length = payload_remaining = 0;
    payload_discard = 0;
    queue_head = queue_tail = 0;
    ipd_frames = payload_bytes = queued_lines = dropped_lines = ipd_resyncs = 0;
    if (HAL_UART_Receive_IT(&huart6, &rx_byte, 1) != HAL_OK) {
        printf("[ESP] FAILED: cannot start USART6 interrupt reception\r\n");
        return -1;
    }
    printf("[ESP] USART6 baud=%lu\r\n", (unsigned long)huart6.Init.BaudRate);
    return 0;
}

int drv_esp_init(void)
{
    if (start_reception() != 0) return -1;
    if (command("AT\r\n", EVENT_OK, 2000) != 0) {
        uint32_t alternate = huart6.Init.BaudRate == 38400U ? 115200U : 38400U;
        printf("[ESP] Trying alternate UART baud=%lu\r\n", (unsigned long)alternate);
        HAL_UART_AbortReceive(&huart6);
        huart6.Init.BaudRate = alternate;
        if (HAL_UART_Init(&huart6) != HAL_OK) {
            printf("[ESP] FAILED: cannot change USART6 baud\r\n");
            return -1;
        }
        if (start_reception() != 0) return -1;
        if (command("AT\r\n", EVENT_OK, 2000) != 0) {
            printf("[ESP] No AT response at 38400 or 115200; check module power/AT firmware/UART path\r\n");
            return -1;
        }
    }
    printf("[ESP] AT response received at baud=%lu\r\n", (unsigned long)huart6.Init.BaudRate);
    if (command("ATE0\r\n", EVENT_OK, 1000) != 0) return -1;
    /* Start from a known module state once AT communication is available. */
    if (command("AT+RST\r\n", EVENT_OK, 2000) != 0) return -1;
    /* ESP8266 ROM prints at 74880 baud. Do not parse that at the AT baud:
       boot bytes can raise framing/overrun faults and stop HAL reception. */
    if (HAL_UART_AbortReceive(&huart6) != HAL_OK) return -1;
    printf("[ESP] Waiting for reboot; boot UART output discarded\r\n");
    HAL_Delay(2000);
    if (start_reception() != 0) return -1;
    if (command("AT\r\n", EVENT_OK, 2000) != 0) return -1;
    if (command("ATE0\r\n", EVENT_OK, 1000) != 0) return -1;
    if (command("AT+CWMODE=1\r\n", EVENT_OK, 1000) != 0) return -1;
    if (command("AT+CIPMODE=0\r\n", EVENT_OK, 1000) != 0) return -1;
    if (command("AT+CIPMUX=0\r\n", EVENT_OK, 1000) != 0) return -1;
    return command("AT+CIPDINFO=0\r\n", EVENT_OK, 1000);
}

/* Reject AT delimiters to keep configuration from changing the command. */
static int valid_parameter(const char *value)
{
    return strpbrk(value, "\"\\\r\n") == NULL;
}

int esp_client_conn(void)
{
    char buffer[256];
    int length;
    if (ESP_WIFI_SSID[0] == '\0' || ESP_SERVER_HOST[0] == '\0' ||
        !valid_parameter(ESP_WIFI_SSID) || !valid_parameter(ESP_WIFI_PASSWORD) ||
        !valid_parameter(ESP_SERVER_HOST)) {
        printf("[ESP] FAILED: missing/invalid Wi-Fi or server configuration\r\n");
        return -1;
    }
    connected = 0;
    length = snprintf(buffer, sizeof(buffer), "AT+CWJAP=\"%s\",\"%s\"\r\n",
                      ESP_WIFI_SSID, ESP_WIFI_PASSWORD);
    if (length < 0 || (size_t)length >= sizeof(buffer)) {
        printf("[ESP] FAILED: Wi-Fi configuration too long\r\n");
        return -1;
    }
    if (command(buffer, EVENT_OK, 20000) != 0) return -1;
    printf("[ESP] Wi-Fi joined\r\n");
    length = snprintf(buffer, sizeof(buffer), "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n",
                      ESP_SERVER_HOST, (unsigned)ESP_SERVER_PORT);
    if (length < 0 || (size_t)length >= sizeof(buffer)) {
        printf("[ESP] FAILED: server configuration too long\r\n");
        return -1;
    }
    if (command(buffer, EVENT_OK, 10000) != 0) return -1;
    if (!connected) {
        printf("[ESP] FAILED: TCP CONNECT confirmation missing\r\n");
        return -1;
    }
    if (ESP_SERVER_LOGIN[0] != '\0' && esp_send_data(ESP_SERVER_LOGIN) != 0)
        return -1;
    return 0;
}

int esp_get_status(void)
{
    if (command("AT+CIPSTATUS\r\n", EVENT_OK, 2000) != 0) connected = 0;
    return connected ? 0 : -1;
}

int esp_send_data(const char *message)
{
    char buffer[40];
    size_t length;
    if (!connected || message == NULL) return -1;
    length = strlen(message);
    if (length == 0 || length > 1460U) return -1;
    printf("[ESP] TCP payload length=%u\r\n", (unsigned)length);
    snprintf(buffer, sizeof(buffer), "AT+CIPSEND=%u\r\n", (unsigned)length);
    /* Wait for '>' before sending bytes, then wait specifically for SEND OK. */
    if (command(buffer, EVENT_PROMPT, 2000) != 0) goto failed;
    events = 0;
    if (HAL_UART_Transmit(&huart6, (uint8_t *)message, (uint16_t)length, 1000) != HAL_OK)
        goto failed;
    if (wait_event(EVENT_SENT, 5000) == 0) return 0;
    printf("[ESP] Payload FAILED: %s\r\n", wait_failure);
failed:
    connected = 0;
    rx_fault = 1;
    return -1;
}

int esp_read_message(char *message, uint16_t capacity)
{
    if (queue_tail == queue_head) return 0;
    size_t length = strlen(messages[queue_tail]);
    if (message == NULL || capacity <= length) return -1;
    memcpy(message, messages[queue_tail], length + 1U);
    queue_tail = (uint8_t)((queue_tail + 1U) % QUEUE_SIZE);
    return 1;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART6) return;
    ++rx_count;
    uint16_t next = (uint16_t)((rx_head + 1U) % RX_SIZE);
    if (next == rx_tail) rx_fault = 1;
    else
    {
        rx_buffer[rx_head] = rx_byte;
        rx_head = next;
    }
    if (HAL_UART_Receive_IT(huart, &rx_byte, 1) != HAL_OK) rx_fault = 1;
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART6) {
        uart_error = huart->ErrorCode;
        rx_fault = 1;
    }
}

int __io_putchar(int ch)
{
    uint8_t byte = (uint8_t)ch;
    return HAL_UART_Transmit(&huart2, &byte, 1, 100) == HAL_OK ? ch : -1;
}
