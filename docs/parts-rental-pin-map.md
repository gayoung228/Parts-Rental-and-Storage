# Parts Rental and Storage Pin Map

이 문서는 아파트 공구 대여·반납 보관함의 STM32 NUCLEO-F411RE에 사용할 **제안 핀맵**을 정리한다.
현재 프로젝트의 `.ioc` 파일과 실제 배선은 아직 확인하지 않았다. 아래 배정에 맞춰 CubeMX를 설정한 뒤 실제 설정과 문서를 함께 관리한다.

## 하드웨어 구성

- STM32 NUCLEO-F411RE
- RFID 리더기: RC522 SPI 모듈 기준
- Wi-Fi 모듈: ESP8266 AT 펌웨어 / UART 방식 기준
- I²C LCD: 문자 LCD + I²C 백팩 기준
- 서보모터: SG90 등 일반 RC 서보 기준
- 조도 센서(CDS): 아날로그 출력 모듈 또는 CDS 분압 회로
- 보관함 내부 LED 및 구동 회로
- 부저 및 구동 회로
- ST-LINK Virtual COM Port

RFID·Wi-Fi·LCD·서보·부저의 정확한 제품 모델은 미확정이다. 전원, 제어 극성, LCD 주소, UART 속도는 실제 제품에 맞춰 확정한다.

Jetson, 관리실 Arduino, HC-05는 STM32에 직접 연결하지 않는다. 각 동의 카메라 영상은 네트워크 카메라 또는 별도 영상 전송 장치를 통해 관리실 Jetson에 전달한다. STM32는 Wi-Fi로 라즈베리파이 서버와 제어 메시지를 주고받는다.

## 전체 핀맵

| 기능 | 코드 이름 | Nucleo 핀 | STM32 핀 | CubeMX 설정 | 연결 대상 |
|---|---|---|---|---|---|
| RFID SPI Clock | — | Morpho PB13 | PB13 | SPI2_SCK | RC522 SCK |
| RFID SPI 수신 | — | Morpho PB14 | PB14 | SPI2_MISO | RC522 MISO |
| RFID SPI 송신 | — | Morpho PB15 | PB15 | SPI2_MOSI | RC522 MOSI |
| RFID 칩 선택 | `RFID_CS` | Morpho PB12 | PB12 | GPIO Output Push-Pull | RC522 SDA / SS |
| RFID 리셋 | `RFID_RST` | Morpho PC4 | PC4 | GPIO Output Push-Pull | RC522 RST |
| Wi-Fi UART 송신 | — | Morpho PC6 | PC6 | USART6_TX | ESP8266 RX |
| Wi-Fi UART 수신 | — | D9 | PC7 | USART6_RX | ESP8266 TX |
| LCD I²C Clock | — | D15 | PB8 | I2C1_SCL | LCD SCL |
| LCD I²C Data | — | D14 | PB9 | I2C1_SDA | LCD SDA |
| 문 잠금 서보 신호 | — | D7 | PA8 | TIM1_CH1 PWM | 서보 Signal |
| 보관함 밝기 측정 | — | A0 | PA0 | ADC1_IN0 | CDS AO 또는 분압 회로 중간점 |
| 보관함 내부 LED 제어 | `CABINET_LED` | A3 | PB0 | GPIO Output Push-Pull | LED 구동 회로 입력 |
| 무단 개방 부저 제어 | `ALARM_BUZZER`* | Morpho PB1 | PB1 | GPIO Output 또는 TIM3_CH4 PWM | 부저 구동 회로 입력 |
| 디버그 UART 송신 | `USART_TX` | D1 | PA2 | USART2_TX | ST-LINK Virtual COM Port |
| 디버그 UART 수신 | `USART_RX` | D0 | PA3 | USART2_RX | ST-LINK Virtual COM Port |

Morpho는 보드 양옆의 긴 확장 헤더를 뜻한다. 해당 포트 이름을 보드 핀 배치도에서 찾아 연결한다.

*`ALARM_BUZZER`는 GPIO 방식에서 사용할 사용자 라벨이다. PWM 방식에서는 `htim3`와 `TIM_CHANNEL_4`로 제어한다.
PA13·PA14는 SWD 디버깅용으로 유지하고, PA5의 보드 내장 LD2는 외부 모듈에 배정하지 않는다.

## RFID RC522 연결

| RC522 핀 | 연결 |
|---|---|
| SCK | PB13 |
| MISO | PB14 |
| MOSI | PB15 |
| SDA / SS | PB12 |
| RST | PC4 |
| 3.3V | NUCLEO 3.3V |
| GND | 공통 GND |
| IRQ | 연결하지 않음 |

RC522의 SDA 표기는 이번 SPI 연결에서 칩 선택 SS를 의미한다. LCD의 I²C SDA와 연결하지 않는다.

SPI2 설정:

```text
Mode              : Full-Duplex Master
Data Size         : 8 Bits
First Bit         : MSB First
Clock Polarity    : Low
Clock Phase       : 1 Edge
NSS               : Software
Baud Prescaler    : 32 (초기 시험값)
```

실제 SPI2 클럭은 APB1 클럭 / Prescaler로 결정된다. 예를 들어 APB1이 50 MHz면 약 1.56 MHz이다. RC522 드라이버에서 PB12를 LOW로 내려 통신하고, 통신 후 HIGH로 복귀한다.

GPIO 초기 설정:

```text
RFID_CS / PB12  : Output Push-Pull, Initial HIGH, No Pull, Low Speed
RFID_RST / PC4 : Output Push-Pull, Initial HIGH, No Pull, Low Speed
```

## Wi-Fi ESP8266 연결

| ESP8266 핀 | 연결 |
|---|---|
| RX | PC6 / USART6_TX |
| TX | PC7 / USART6_RX |
| GND | 공통 GND |
| VCC | 모듈 규격에 맞는 전원 |

TX와 RX는 교차 연결한다. ESP-01 같은 원 모듈의 VCC는 3.3V이며 5V를 직접 연결하지 않는다. 레귤레이터가 있는 어댑터 보드는 입력 전원이 다를 수 있다. Wi-Fi 송신 시 전류를 감당할 수 있는 별도 3.3V 전원을 준비한다.

ESP-01을 사용할 경우 EN/CH_PD는 HIGH, RST는 정상 동작 시 HIGH로 유지해야 한다. GPIO0·GPIO2도 정상 부팅에 필요한 레벨을 유지하도록 실제 모듈의 부팅 회로를 확인한다. 이 핀들은 현재 핀맵에서 STM32 제어 핀으로 배정하지 않았다.

USART6 설정:

```text
Mode             : Asynchronous
TX               : PC6
RX               : PC7
Baud Rate        : 실제 ESP8266 AT 펌웨어 설정과 일치
Word Length      : 8 Bits
Parity           : None
Stop Bits        : 1
Hardware Flow    : None
USART6 Interrupt : Enabled (인터럽트 수신 구현 기준)
```

9600 또는 115200을 임의로 확정하지 않는다. PC UART 어댑터로 AT 응답을 확인한 뒤 동일한 속도를 설정한다. 인터럽트 수신을 사용하면 초기화에서 수신을 시작하고, 선택한 HAL 수신 방식에 맞춰 재수신을 처리한다.

STM32가 보내는 정보: 카드 UID, 보관함 ID, 이용 회차 ID, CDS로 추정한 문 상태, 경보 상태.
서버에서 받는 정보: 인증 승인·거부, 사용자 표시 정보, 대여·반납 판단 결과, 처리 완료·오류.
카메라 영상은 이 UART/Wi-Fi 경로로 전송하지 않는다.

## I²C LCD 연결

| LCD 백팩 핀 | 연결 |
|---|---|
| SCL | PB8 / D15 |
| SDA | PB9 / D14 |
| GND | 공통 GND |
| VCC | 실제 LCD·백팩의 정격 전원 |

I2C1 설정:

```text
Mode            : I2C
Clock Speed     : 100 kHz
Addressing Mode : 7-bit
SCL             : PB8
SDA             : PB9
```

LCD 주소는 I²C 스캔으로 확인한다. `0x27`, `0x3F` 등은 가능한 예시이며 확정값이 아니다. HAL에 넘기는 주소는 드라이버 구현이 7비트 주소를 받는지, 왼쪽으로 1비트 이동한 값을 받는지 확인한다.

5V LCD 백팩은 SDA/SCL에 5V 풀업이 있을 수 있다. MCU·백팩 양쪽의 입력 조건을 확인하고, 필요하면 양방향 I²C 레벨 변환기를 사용한다. 백팩 전체를 무조건 3.3V로 공급하는 방식도 제품 확인 없이 적용하지 않는다.

일반적인 16×2 문자 LCD는 한글을 그대로 표시할 수 없으므로, 실제 표시 예시는 영문으로 구성한다.

| 상황 | LCD 1행 예시 | LCD 2행 예시 |
|---|---|---|
| 대기 | `TAG YOUR CARD` | `CABINET 101` |
| 인증 완료 | `101-1001` | `SHOW YOUR TOOL` |
| 대여 안내 | `HAMMER` | `RENTAL` |
| 반납 안내 | `HAMMER` | `RETURN` |
| 처리 완료 | `RENTAL COMPLETE` | `FEE: 500 KRW` |
| 무단 개방 | `UNAUTHORIZED` | `DOOR OPEN` |

## 서보모터 연결 및 PWM 설정

| 서보 배선 | 연결 |
|---|---|
| Signal | PA8 / D7 / TIM1_CH1 |
| VCC | 별도 서보 정격 전원 |
| GND | 별도 전원 GND 및 STM32 공통 GND |

TIM1 설정:

```text
Clock Source   : Internal Clock
Channel 1      : PWM Generation CH1
PWM Mode       : PWM Mode 1
PWM Frequency  : 50 Hz
Counter Tick   : 1 us를 목표로 설정
Counter Period : 19999 (1 MHz 카운터 기준)
```

Prescaler는 TIM1 입력 클럭을 확인해 정한다. 서보 신호의 기준은 20 ms 주기이며, 펄스 폭과 잠금·해제 위치는 실제 서보 및 기구에서 확인한다.

| TIM1 입력 클럭 | Prescaler | Counter Period | PWM 주파수 |
|---|---:|---:|---:|
| 84 MHz | 83 | 19999 | 50 Hz |
| 100 MHz | 99 | 19999 | 50 Hz |

일반식: PWM 주파수 = TIM1 입력 클럭 / ((Prescaler + 1) × (Period + 1)).
기존 로버의 84 MHz 설정을 이번 프로젝트에 그대로 적용하지 않는다.

PWM 시작 전에 잠금 위치의 비교값을 설정하고, 초기화에서 한 번 실행한다.

```c
__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, SERVO_LOCK_PULSE_US);
HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
```

`SERVO_LOCK_PULSE_US`는 실제 잠금 위치를 시험한 뒤 정의할 값이다. 비교값이 마이크로초와 일치하는 것은 카운터가 1 MHz일 때이다.
서보가 승인받지 않은 상태에서 잠금을 해제하지 않도록 초기 위치와 장착 방향을 확인한다.

## CDS 연결 및 ADC 설정

아날로그 출력 모듈을 사용할 경우:

| CDS 모듈 핀 | 연결 |
|---|---|
| AO | PA0 / A0 |
| VCC | 3.3V 지원 모듈 기준 3.3V |
| GND | 공통 GND |
| DO | 연결하지 않음 |

CDS 단품을 사용할 경우, 3.3V와 GND 사이에 CDS와 고정 저항을 직렬로 연결하고 중간점을 PA0으로 입력한다. 고정 저항은 10 kΩ 등을 시작값으로 사용하고 실제 밝기 범위에 맞춰 조정한다. ADC 입력은 0~3.3V 범위로 유지한다.

ADC1 설정:

```text
Input           : IN0 / PA0
Resolution      : 12 Bits
Data Alignment  : Right
Scan Mode       : Disabled
Conversion      : 단일 변환을 주기적으로 실행
Sampling Time   : 144 Cycles (초기 시험값)
DMA             : 사용하지 않음 (단일 채널 폴링 기준)
```

ADC 원시값 범위는 0~4095이다. 회로 방향에 따라 밝을 때 값이 커질 수도, 작아질 수도 있으므로 문 열림·닫힘 상태에서 직접 측정한다.

문 상태 판단과 LED 제어:

- 외부 조명이 켜진 동 1층 환경을 기준으로 사용한다.
- 문이 열리면 외부 빛 유입을 감지하고 내부 LED를 켠다.
- 문이 닫히면 외부 빛 차단을 감지하고 내부 LED를 끈다.
- CDS에는 내부 LED 빛이 직접 닿지 않도록 문틈 쪽에 배치하고 차광한다.
- 문 닫힘 + 내부 LED ON 상태에서도 닫힘을 구분할 수 있는지 시험한다.
- 열림과 닫힘의 기준값을 다르게 설정하고, 일정 시간 유지된 값으로 상태를 확정한다.

CDS는 문 상태를 밝기로 추정한다. 서보 위치나 실제 잠금 체결을 확인하는 센서는 아니다.

## 내부 LED 연결

```text
제어 핀 : PB0 / A3 / CABINET_LED
Mode    : GPIO Output Push-Pull
Pull    : No Pull
Speed   : Low
초기값  : 구동 회로 기준 OFF
```

소형 표시 LED 한 개는 전류 제한 저항을 사용한다. 보관함 조명용 LED 또는 LED 스트립은 별도 정격 전원과 트랜지스터/MOSFET 구동 회로를 사용한다. GPIO 핀에서 조명 전력을 직접 공급하지 않는다.

구동 회로가 Active-High이면 초기 LOW, Active-Low이면 초기 HIGH로 OFF를 설정한다.

## 부저 연결 및 무단 개방 경보

부저 제어는 PB1을 사용한다. 제품에 맞춰 다음 두 방식 중 하나만 선택한다.

| 부저 종류 | CubeMX 설정 | 제어 방법 |
|---|---|---|
| 능동 부저 | PB1 GPIO Output Push-Pull | 켜짐·꺼짐 제어 |
| 수동 부저 | PB1 TIM3_CH4 PWM | 제품에 맞는 가청 주파수 PWM |

구동 전류가 GPIO 허용 범위를 넘으면 트랜지스터 등 구동 회로를 사용한다. 3핀 모듈이면 전원·GND·입력 신호와 Active-High/Low 조건을 확인한다. 기본 상태는 OFF로 설정한다.

무단 개방 판단:

| 개방 승인 | CDS로 추정한 문 상태 | 동작 |
|---|---|---|
| 없음 | 닫힘 | 정상 대기 |
| 있음 | 열림 | 내부 LED ON, 정상 이용 |
| 없음 | 열림이 일정 시간 유지 | 부저 ON, 서버에 경보 전송 |

서버 승인 후 서보를 움직이기 전에 STM32의 개방 허용 상태를 먼저 설정한다. 정상 이용 후 닫힘을 확인하고 잠금 동작을 수행한 뒤 허용 상태를 해제한다. 카드 UID를 읽었다는 이유만으로 승인 상태를 설정하지 않는다.

경보는 로컬에서 발생시켜 통신이 끊겨도 부저가 동작하도록 한다. 경보 해제 정책은 관리자 확인 등으로 별도 정의한다. 승인 후 문을 열지 않는 경우의 승인 만료 시간과 장시간 열림 정책도 구현 시 정한다.

## 디버그 UART 및 디버깅 설정

```text
USART2
TX        : PA2 / D1
RX        : PA3 / D0
Baud Rate : 115200
Word      : 8 Bits
Parity    : None
Stop Bits : 1

SYS Debug : Serial Wire
SWDIO     : PA13
SWCLK     : PA14
```

USART2는 ST-LINK Virtual COM Port 로그용으로 유지한다. PA2·PA3에는 Wi-Fi 모듈을 중복 연결하지 않는다. GPIO/USART 로그에서 카드 인식, 서버 승인, CDS 값, 문 상태, 공구 처리 결과, 경보를 확인한다.

## 전원 연결

| 전원 | 공급 대상 |
|---|---|
| NUCLEO USB | STM32 보드 |
| NUCLEO 3.3V | RC522 및 소전류 3.3V 지원 센서 |
| 별도 안정적인 3.3V | 원 ESP8266 모듈 |
| 별도 서보 정격 전원 | 서보모터 |
| 제품 정격 전원 | LCD 백팩, 내부 조명, 부저 |

STM32, RFID, Wi-Fi, LCD, CDS, 서보 전원, LED·부저 구동 회로의 GND를 공통으로 연결한다. 별도 전원의 양극을 보드 전원 레일에 임의로 병렬 연결하지 않는다. 서보 전류 경로는 보드의 신호 배선과 구분해 구성한다.

## 권장 연동 순서

1. USART2: PC에서 디버그 로그 확인.
2. LCD: 초기 화면 출력 및 주소 확인.
3. RC522: 카드 UID 읽기.
4. ESP8266: AT 응답, Wi-Fi 접속, 서버 메시지 송수신.
5. 서보: 단독 잠금·해제 시험 후 서버 승인과 연결.
6. CDS: 밝기 값 측정 및 문 상태 기준 확정.
7. 내부 LED: CDS와 연동하고 내부 조명 간섭 확인.
8. 부저: 승인 없는 열림 경보 시험.
9. 서버·Jetson: 인식 결과에 따른 대여·반납 LCD 안내.

대여·반납은 서버가 공구별 상태와 대여자를 확인해 판단한다. 동일 공구의 연속 인식은 한 이용 회차에서 중복 처리되지 않도록 한다. CDS만으로 실제 공구 반납을 확정하지 않는다.

## 주의 사항

- 실제 `.ioc`와 모듈 모델을 확인한 후 이 제안 핀맵을 확정한다.
- RFID RC522 전원에 5V를 직접 공급하지 않는다.
- PA0의 ADC 입력에는 3.3V를 넘는 신호를 넣지 않는다.
- LCD 전원·I²C 풀업 전압·입력 조건을 함께 확인한다.
- ESP8266 UART 속도와 정상 부팅 조건을 확인한다.
- 서보와 조명은 GPIO 또는 보드 3.3V 핀에서 직접 전력을 공급하지 않는다.
- PB1은 능동 부저 GPIO 또는 수동 부저 PWM 중 하나로 설정한다.
- 일반 문자 LCD의 한글 표시 가능 여부를 확인한다.
- 타이머 Prescaler는 실제 CubeMX 타이머 입력 클럭 기준으로 계산한다.
- 배선은 전원을 끄고 변경하고, 모듈은 하나씩 시험한다.
- CubeMX 핀 변경 시 코드 라벨과 이 문서를 함께 수정한다.

## 참고 문서

- [STM32F411xC/xE 데이터시트 — 핀 기능 및 대체 기능](https://www.st.com/resource/en/datasheet/stm32f411re.pdf)
- [UM1724 — NUCLEO-F411RE 보드 커넥터 및 전원 구성](https://www.st.com/resource/en/user_manual/dm00105823-stm32-nucleo-32-boards-stmicroelectronics.pdf)
- [ESP-AT — ESP8266 연결 및 AT 펌웨어 문서](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/)
