/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "esp.h"
#include "rc522.h"
#include "servo_lock.h"
#include "door_sensor.h"
#include "intrusion_alarm.h"
#include "lcd1602.h"
#include "tool_buzzer.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define LOCKER_LED_GPIO_PORT GPIOB
#define LOCKER_LED_GPIO_PIN  GPIO_PIN_0
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint32_t wifi_check_tick;
static uint8_t wifi_connected;
static char wifi_message[ESP_MESSAGE_SIZE];
static uint8_t rfid_ready;
static uint32_t rfid_poll_tick;
static uint32_t rfid_error_tick;
static rc522_uid_t last_uid;
static uint8_t rfid_misses;
static uint8_t card_receipt_pending;
static uint32_t card_send_tick;
static char pending_card_uid[21];
static uint8_t card_auth_pending;
static uint32_t card_request_counter;
static char pending_card_request[17];
static uint32_t cds_sample_tick;
static uint32_t cds_send_tick;
static uint32_t cds_request_counter;
static uint8_t locker_led_on;
static door_state_t latest_door_state = DOOR_UNKNOWN;
static uint8_t alarm_state;
static uint8_t alarm_sent_state = 2U;
static uint32_t alarm_send_tick;
static char pending_room[11], active_room[11];
static uint8_t lcd_temporary_message;
static uint32_t lcd_message_tick;
static char session_uid[21], session_id[17], active_tool[32];
static char active_tool_result[9];
static uint8_t session_dirty = 1;
static uint32_t session_send_tick;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void wifi_connect(void);
static void esp_event(char *message);
static void rfid_poll(void);
static void cds_sample(void);
static void alarm_poll(void);
static void lcd_prompt(void);
static void lcd_status(const char *first, const char *second);
static void camera_session_poll(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_SPI2_Init();
  MX_TIM1_Init();
  MX_TIM3_Init();
  MX_USART2_UART_Init();
  MX_USART6_UART_Init();
  /* USER CODE BEGIN 2 */
  printf("\r\nParts locker - Wi-Fi start (USART6 baud probe)\r\n");
  printf("[BUZZER] %s\r\n", tool_buzzer_init() == 0 ? "PB1 PWM ready (2kHz)" : "Init FAILED");
  uint8_t lcd_address;
  if (lcd1602_init(&lcd_address) == 0)
  {
    printf("[LCD] Ready at I2C address=0x%02X\r\n", lcd_address);
    lcd_prompt();
  }
  else printf("[LCD] Init FAILED; check PB8/PB9, address and backpack wiring\r\n");
  printf("[SERVO] %s\r\n", servo_lock_init() == 0 ?
         "State=LOCK (initial position)" : "State=UNKNOWN; init FAILED; check TIM1 50Hz settings");
  uint8_t rfid_version;
  rfid_ready = rc522_init(&rfid_version) == 0;
  printf("[RFID] Version=0x%02X; %s\r\n", rfid_version,
         rfid_ready ? "ready - tag one card" : "init FAILED; check SPI/CS/RST/3.3V");
  if (ESP_WIFI_SSID[0] != '\0' && ESP_SERVER_HOST[0] != '\0')
  {
    wifi_connect();
  }
  else
  {
    printf("Set Wi-Fi SSID/password and server address in esp.h first\r\n");
  }
  wifi_check_tick = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    esp_poll();
    if (lcd_temporary_message && !card_auth_pending &&
        (uint32_t)(HAL_GetTick() - lcd_message_tick) >= 3000U) lcd_prompt();
    if (rfid_ready && !card_auth_pending &&
        (uint32_t)(HAL_GetTick() - rfid_poll_tick) >= 200U)
    {
      rfid_poll();
      rfid_poll_tick = HAL_GetTick();
    }
    while (esp_read_message(wifi_message, sizeof(wifi_message)) > 0)
    {
      esp_event(wifi_message);
    }
    if ((uint32_t)(HAL_GetTick() - cds_sample_tick) >= 500U)
    {
      cds_sample();
      cds_sample_tick = HAL_GetTick();
    }
    alarm_poll();
    camera_session_poll();
    if (card_receipt_pending && (uint32_t)(HAL_GetTick() - card_send_tick) >= 5000U)
    {
      printf("[RFID] Server receipt TIMEOUT for UID=%s; delivery not confirmed\r\n",
             pending_card_uid);
      esp_print_rx_diagnostic();
      card_receipt_pending = 0;
    }
    if (card_auth_pending && (uint32_t)(HAL_GetTick() - card_send_tick) >= 10000U)
    {
      printf("[AUTH] No authorization response for UID=%s; access not approved\r\n", pending_card_uid);
      card_auth_pending = 0;
      lcd_status("Server timeout", "Try card again");
    }
    if (!card_auth_pending &&
        (uint32_t)(HAL_GetTick() - wifi_check_tick) >= 10000U &&
        ESP_WIFI_SSID[0] != '\0' && ESP_SERVER_HOST[0] != '\0')
    {
      if (!wifi_connected || esp_get_status() != 0) wifi_connect();
      wifi_check_tick = HAL_GetTick();
    }
    HAL_Delay(1);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
static void lcd_prompt(void)
{
  lcd_temporary_message = 0;
  if (servo_lock_is_unlocked())
  {
    char first[17];
    snprintf(first, sizeof(first), "Room %s", active_room[0] ? active_room : "?");
    if (active_tool[0])
    {
      char second[64];
      if (!strcmp(active_tool_result,"RENTED")) strcpy(second,"Rental complete");
      else if (!strcmp(active_tool_result,"RETURNED")) strcpy(second,"Return complete");
      else if (!strcmp(active_tool_result,"BUSY")) strcpy(second,"Tool in use");
      else snprintf(second, sizeof(second), "Tool %s detected", active_tool);
      lcd1602_show(first, second, 1);
    }
    else lcd1602_show(first, "Show tool to camera", 1);
  }
  else lcd1602_show("Tap RFID card", "", 0);
}

static void lcd_status(const char *first, const char *second)
{
  lcd1602_show(first, second, 0);
  lcd_message_tick = HAL_GetTick();
  lcd_temporary_message = 1;
}

static void cds_sample(void)
{
  uint32_t sum = 0;
  /* Average eight PA0 samples; bounded waits keep UART and servo responsive. */
  for (uint8_t i = 0; i < 8U; ++i)
  {
    HAL_StatusTypeDef status = HAL_ADC_Start(&hadc1);
    if (status == HAL_OK) status = HAL_ADC_PollForConversion(&hadc1, 2);
    if (status != HAL_OK)
    {
      HAL_ADC_Stop(&hadc1);
      door_sensor_discard_sample();
      printf("[CDS] ADC read FAILED (HAL status=%u)\r\n", (unsigned)status);
      return;
    }
    sum += HAL_ADC_GetValue(&hadc1);
    if (HAL_ADC_Stop(&hadc1) != HAL_OK)
    {
      door_sensor_discard_sample();
      printf("[CDS] ADC stop FAILED\r\n");
      return;
    }
    esp_poll();
  }
  uint32_t average = (sum + 4U) / 8U;
  /* Approximate voltage assumes a 3.3V ADC reference. */
  uint32_t millivolts = (average * 3300U + 2047U) / 4095U;
  door_state_t door = door_sensor_update((uint16_t)average);
  latest_door_state = door;
  uint8_t led_on = door == DOOR_OPEN;
  HAL_GPIO_WritePin(LOCKER_LED_GPIO_PORT, LOCKER_LED_GPIO_PIN,
                   led_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
  if (led_on != locker_led_on)
  {
    locker_led_on = led_on;
    printf("[LED] State=%s (door=%s)\r\n", led_on ? "ON" : "OFF",
           door_sensor_state_name(door));
  }
  printf("[CDS] ADC=%lu voltage=%lu.%03luV door=%s led=%s\r\n", (unsigned long)average,
         (unsigned long)(millivolts / 1000U), (unsigned long)(millivolts % 1000U),
         door_sensor_state_name(door), led_on ? "ON" : "OFF");
  if (wifi_connected && !card_auth_pending &&
      (uint32_t)(HAL_GetTick() - cds_send_tick) >= 5000U)
  {
    char message[96];
    ++cds_request_counter;
    snprintf(message, sizeof(message), "[" ESP_DEVICE_ID "]SENSOR@%lu@%08lX%08lX\n",
             (unsigned long)average, (unsigned long)HAL_GetTick(), (unsigned long)cds_request_counter);
    cds_send_tick = HAL_GetTick();
    if (esp_send_data(message) != 0)
    {
      wifi_connected = 0;
      printf("[CDS] Server send FAILED; sample not queued for retry\r\n");
    }
  }
}

static void alarm_poll(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t active = intrusion_alarm_update(!servo_lock_is_unlocked(), latest_door_state, now);
  if (active != alarm_state)
  {
    alarm_state = active;
    printf("[ALARM] %s locker=" ESP_DEVICE_ID "\r\n", active ? "ACTIVE" : "CLEAR");
  }
  if (wifi_connected && latest_door_state != DOOR_UNKNOWN &&
      (alarm_sent_state != active || (uint32_t)(now - alarm_send_tick) >= 5000U))
  {
    char message[80];
    snprintf(message, sizeof(message), "[" ESP_DEVICE_ID "]ALARM@%s\n", active ? "ACTIVE" : "CLEAR");
    if (esp_send_data(message) == 0)
    {
      alarm_sent_state = active;
      alarm_send_tick = HAL_GetTick();
    }
    else wifi_connected = 0;
  }
}

static void rfid_poll(void)
{
  rc522_uid_t uid;
  int result = rc522_read_uid(&uid);
  if (result == 0)
  {
    if (rfid_misses < 3U) ++rfid_misses;
    if (rfid_misses >= 3U) last_uid.length = 0;
    return;
  }
  if (result < 0)
  {
    if ((uint32_t)(HAL_GetTick() - rfid_error_tick) >= 2000U)
    {
      const rc522_diagnostic_t *info = rc522_diagnostic();
      printf("[RFID] %s FAILED: %s\r\n", info->stage, info->reason);
      printf("[RFID] irq=0x%02X error=0x%02X fifo=%u bits=%u\r\n",
             info->irq, info->error, info->fifo_count, info->last_bits);
      rfid_error_tick = HAL_GetTick();
    }
    return;
  }
  rfid_misses = 0;
  if (uid.length == last_uid.length &&
      memcmp(uid.bytes, last_uid.bytes, uid.length) == 0) return;
  last_uid = uid;
  char uid_hex[21];
  static const char hex[] = "0123456789ABCDEF";
  for (uint8_t i = 0; i < uid.length; ++i)
  {
    uid_hex[2U * i] = hex[uid.bytes[i] >> 4U];
    uid_hex[2U * i + 1U] = hex[uid.bytes[i] & 0x0FU];
  }
  uid_hex[2U * uid.length] = '\0';
  printf("[RFID] UID=%s\r\n", uid_hex);
  int currently_unlocked = servo_lock_is_unlocked();
  printf("[SERVO] State=%s (card tagged)\r\n", currently_unlocked ? "UNLOCK" : "LOCK");
  if (!servo_lock_card_can_operate(uid_hex))
  {
    lcd_status("Locker in use", "Use same card");
    printf("[RFID] Session=ACTIVE owner=%s; another card cannot toggle this locker\r\n",
           servo_lock_active_uid());
    return;
  }
  if (!wifi_connected)
  {
    lcd_status("Server offline", "Try card again");
    printf("[RFID] Server offline; card not sent. Retag after reconnect.\r\n");
    return;
  }
  char message[ESP_MESSAGE_SIZE];
  pending_room[0] = '\0';
  lcd_status("Checking card", "Please wait");
  ++card_request_counter;
  snprintf(pending_card_request, sizeof(pending_card_request), "%08lX%08lX",
           (unsigned long)HAL_GetTick(), (unsigned long)card_request_counter);
  int length = snprintf(message, sizeof(message), "[" ESP_DEVICE_ID "]CARD@%s@%s\n",
                        uid_hex, pending_card_request);
  if (length <= 0 || (size_t)length >= sizeof(message))
  {
    printf("[RFID] Card message too long\r\n");
    return;
  }
  printf("[RFID] TX (%d bytes): %.*s\\n\r\n", length, length - 1, message);
  if (esp_send_data(message) != 0)
  {
    wifi_connected = 0;
    lcd_status("Send failed", "Try card again");
    printf("[RFID] Card send FAILED; retag after reconnect\r\n");
  }
  else
  {
    strcpy(pending_card_uid, uid_hex);
    card_send_tick = HAL_GetTick();
    card_receipt_pending = 1;
    card_auth_pending = 1;
    servo_lock_expect(uid_hex, pending_card_request);
    printf("[RFID] ESP accepted card data; waiting for server receipt\r\n");
  }
}

static void camera_session_poll(void)
{
  uint32_t now = HAL_GetTick();
  if (!wifi_connected || card_auth_pending ||
      (!session_dirty && (uint32_t)(now - session_send_tick) < 5000U)) return;
  char message[112];
  if (!session_id[0]) snprintf(message, sizeof(message), "[" ESP_DEVICE_ID "]SESSION@CLOSED\n");
  else snprintf(message, sizeof(message), "[" ESP_DEVICE_ID "]SESSION@%s@%s@%s\n",
                session_uid, session_id, servo_lock_is_unlocked() ? "OPEN" : "CLOSED");
  if (esp_send_data(message) == 0)
  {
    session_send_tick = HAL_GetTick();
    session_dirty = 0;
  }
  else wifi_connected = 0;
}

static void wifi_connect(void)
{
  wifi_connected = 0;
  tool_buzzer_stop();
  alarm_sent_state = 2U; /* Report current alarm after every reconnect. */
  session_dirty = 1;
  card_receipt_pending = 0;
  card_auth_pending = 0;
  servo_lock_cancel_request();
  printf("Connecting Wi-Fi and TCP server...\r\n");
  if (drv_esp_init() != 0 || esp_client_conn() != 0)
  {
    printf("Wi-Fi connection failed; retry in 10 seconds\r\n");
    return;
  }
  wifi_connected = 1;
  printf("TCP server connected\r\n");
  if (esp_send_data("[" ESP_DEVICE_ID "]HELLO\n") != 0)
  {
    wifi_connected = 0;
    printf("HELLO send failed\r\n");
  }
}

static void esp_event(char *message)
{
  char *sender, *command_name, *value;
  char reply[ESP_MESSAGE_SIZE];
  int length;
  printf("Server: %s\r\n", message);
  /* Preserve the example's [sender]PLUG@ON/OFF protocol for an LD2 test. */
  if (message[0] != '[') return;
  sender = message + 1;
  command_name = strchr(sender, ']');
  if (command_name == NULL || command_name == sender) return;
  *command_name++ = '\0';
  value = strchr(command_name, '@');
  if (value == NULL) return;
  *value++ = '\0';
  if (strcmp(sender, "SERVER") == 0 && strcmp(command_name, "TOOL") == 0)
  {
    char *label = strchr(value, '@');
    if (!label) return;
    *label++ = '\0';
    char *event = strchr(label, '@');
    if (!event) return;
    *event++ = '\0';
    char *status = strchr(event, '@');
    if (!status) return;
    *status++ = '\0';
    size_t length = strlen(label);
    if (servo_lock_is_unlocked() && strcmp(value, session_id) == 0 &&
        (!strcmp(status,"SEEN") || !strcmp(status,"RENTED") ||
         !strcmp(status,"RETURNED") || !strcmp(status,"BUSY")) && length > 0 && length <= 31U)
    {
      uint8_t valid = 1;
      for (size_t i = 0; i < length; ++i)
        if (!((label[i] >= '0' && label[i] <= '9') || (label[i] >= 'A' && label[i] <= 'Z') ||
              (label[i] >= 'a' && label[i] <= 'z') || label[i] == '_')) valid = 0;
      if (valid)
      {
        strcpy(active_tool, label);
        strcpy(active_tool_result,status);
        lcd_prompt();
        printf("[RENTAL] tool=%s result=%s\r\n",label,status);
        if (strcmp(status,"BUSY") && tool_buzzer_event(value, event) == 1)
          printf("[BUZZER] Beep 150ms - tool=%s event=%s\r\n", label, event);
      }
    }
    return;
  }
  if (strcmp(sender, "SERVER") == 0 && strcmp(command_name, "USER") == 0)
  {
    char *request = strchr(value, '@');
    if (!request) return;
    *request++ = '\0';
    char *room = strchr(request, '@');
    if (!room) return;
    *room++ = '\0';
    size_t room_length = strlen(room);
    if (card_auth_pending && strcmp(value, pending_card_uid) == 0 &&
        strcmp(request, pending_card_request) == 0 && room_length > 0 && room_length <= 10U &&
        (uint32_t)(HAL_GetTick() - card_send_tick) < SERVO_AUTH_TIMEOUT_MS)
    {
      uint8_t valid = 1;
      for (size_t i = 0; i < room_length; ++i)
        if (!((room[i] >= '0' && room[i] <= '9') || (room[i] >= 'A' && room[i] <= 'Z') ||
              (room[i] >= 'a' && room[i] <= 'z') || room[i] == '-' || room[i] == '_')) valid = 0;
      if (valid) strcpy(pending_room, room);
    }
    return;
  }
  if (strcmp(sender, "SERVER") == 0 && strcmp(command_name, "SENSOR") == 0)
  {
    printf("[DB] CDS save result: %s\r\n", value);
    return;
  }
  if (strcmp(command_name, "AUTH") == 0)
  {
    char *request_id = strchr(value, '@');
    if (request_id == NULL) return;
    *request_id++ = '\0';
    char *status = strchr(request_id, '@');
    if (status == NULL) return;
    *status++ = '\0';
    if (!card_auth_pending || strcmp(sender, "SERVER") != 0 ||
        strcmp(value, pending_card_uid) != 0 ||
        strcmp(request_id, pending_card_request) != 0 ||
        (uint32_t)(HAL_GetTick() - card_send_tick) >= 10000U)
    {
      printf("[AUTH] Ignored unmatched/expired response\r\n");
      return;
    }
    int applied = servo_lock_authorize(value, request_id, status);
    if (applied == -2)
    {
      printf("[RFID] Another card owns this session; state unchanged\r\n");
      card_auth_pending = 0;
      return;
    }
    if (applied < 0)
    {
      printf("[AUTH] Ignored invalid response or servo unavailable\r\n");
      return;
    }
    if (applied == 1)
    {
      strcpy(session_uid, value);
      strcpy(session_id, request_id);
      active_tool[0] = '\0';
      session_dirty = 1;
      strcpy(active_room, pending_room);
      if (!active_room[0]) printf("[LCD] Room info missing; update relay and SQL_CLIENT\r\n");
      lcd_prompt();
      printf("[AUTH] APPROVED UID=%s\r\n", value);
      printf("[SERVO] State=UNLOCK (approved first tag)\r\n");
      printf("[RFID] Session=ACTIVE owner=%s; retag the same card to lock\r\n", value);
    }
    else if (applied == 2)
    {
      active_tool[0] = '\0';
      session_dirty = 1;
      active_room[0] = '\0';
      lcd_prompt();
      printf("[AUTH] APPROVED UID=%s\r\n", value);
      printf("[SERVO] State=LOCK (approved second tag)\r\n");
      printf("[RFID] Session=IDLE\r\n");
    }
    else if (strcmp(status, "DENIED") == 0)
    {
      lcd_status("Access denied", "Try card again");
      printf("[AUTH] DENIED UID=%s\r\n", value);
      printf("[SERVO] State=%s (access denied; unchanged)\r\n",
             servo_lock_is_unlocked() ? "UNLOCK" : "LOCK");
    }
    else if (strcmp(status, "ERROR") == 0)
    {
      lcd_status("Server error", "Try card again");
      printf("[AUTH] DB ERROR UID=%s; access not approved\r\n", value);
      printf("[SERVO] State=%s (DB error; unchanged)\r\n",
             servo_lock_is_unlocked() ? "UNLOCK" : "LOCK");
    }
    else return;
    card_auth_pending = 0;
    return;
  }
  if (strcmp(command_name, "CARD") == 0)
  {
    char expected[64];
    snprintf(expected, sizeof(expected), "%s@%s@RECEIVED", pending_card_uid, pending_card_request);
    if (card_receipt_pending && strcmp(sender, "SERVER") == 0 && strcmp(value, expected) == 0)
    {
      printf("[RFID] Server receipt confirmed: UID=%s\r\n", pending_card_uid);
      card_receipt_pending = 0;
    }
    return;
  }
  if (strcmp(command_name, "PLUG") != 0) return;
  if (strcmp(value, "ON") == 0)
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
  else if (strcmp(value, "OFF") == 0)
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
  else return;
  length = snprintf(reply, sizeof(reply), "[%s]PLUG@%s\n", sender, value);
  if (length > 0 && (size_t)length < sizeof(reply) && esp_send_data(reply) != 0)
    wifi_connected = 0;
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
