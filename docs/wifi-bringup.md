# STM32 ↔ ESP8266 ↔ Raspberry Pi 통신 확인

## 설정

`Core/Inc/esp.h`에서 다음 값을 입력하고 다시 빌드합니다.

| 설정 | 값 |
| --- | --- |
| `ESP_WIFI_SSID` | 공유기 SSID |
| `ESP_WIFI_PASSWORD` | 공유기 비밀번호 |
| `ESP_SERVER_HOST` | 라즈베리파이 내부 IP 주소 |
| `ESP_SERVER_PORT` | TCP 서버 포트. 테스트 서버 기본값 5000 |
| `ESP_DEVICE_ID` | 보관함 식별자. 기본값 `LOCKER_101` |
| `ESP_SERVER_LOGIN` | 기존 예제 서버에서 요구하는 로그인 문자열. 테스트 서버는 빈 문자열 유지 |

SSID 또는 서버 주소가 비어 있으면 자동 연결을 실행하지 않습니다.
실제 비밀번호를 입력한 파일은 공개 저장소에 올리지 마세요.
현재 AT 파라미터는 큰따옴표, 역슬래시, 줄바꿈을 포함하지 않는 값을 지원합니다.

## 연결

- STM32 PC6 (USART6 TX) → ESP8266 RX
- STM32 PC7 (USART6 RX) ← ESP8266 TX
- GND 공통. ESP8266 전원은 실제 모듈 보드 사양에 맞춰 공급합니다.
- USART6 시작 설정은 **38400 baud, 8N1**입니다. `AT` 응답이 없으면
  **115200**으로도 확인합니다. 재연결 시에는 현재 속도를 먼저 시도합니다.
  `[ESP] AT response received at baud=...`로 실제 응답 속도를 확인하세요.
  이 과정은 STM32 UART 속도만 변경하며 ESP8266의 저장된 속도는 변경하지 않습니다.
  두 속도 모두 실패하면 모듈 전원/활성화, AT 펌웨어와 UART 경로를 확인해야 합니다.
- PC 디버그 로그: ST-LINK 가상 COM 포트, USART2 **115200 baud, 8N1**.
- Raspberry Pi와 ESP8266은 서로 접근 가능한 같은 내부 네트워크에 연결합니다.

## 테스트

라즈베리파이에 `tools/wifi_test_server.c`를 복사한 뒤, 해당 폴더에서 컴파일하고 실행합니다.
이 서버는 Linux 소켓 API를 사용하므로 라즈베리파이 또는 WSL/Linux에서 빌드합니다.
STM32 펌웨어의 CMake 빌드에 포함하지 않습니다.

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -O2 wifi_test_server.c -o wifi_test_server
./wifi_test_server --port 5000
```

STM32를 빌드하고 다운로드한 후 디버그 COM 포트 로그를 확인합니다.

1. `TCP server connected`가 출력됩니다.
2. 서버가 `[LOCKER_101]HELLO`를 수신합니다.
3. 서버가 `[SERVER]PLUG@ON\n`을 보내면 LD2가 켜지고 STM32가 같은 메시지를 응답합니다.
4. 서버가 `[SERVER]PLUG@OFF\n`을 보내면 LD2가 꺼지고 STM32가 응답합니다.
5. 서버에 `PASS: HELLO and LED ON/OFF round trip`이 출력됩니다.
6. 서버를 종료했다가 다시 실행하면 STM32가 재연결 후 같은 테스트를 실행합니다.

기존 Aiot 서버는 `[수신자]PLUG@ON\n`, `[수신자]PLUG@OFF\n` 형식을 사용할 수 있습니다.
로그인 문자열과 HELLO 메시지를 기존 서버가 지원하는지는 별도로 확인해야 합니다.
로그인 데이터 전송 성공은 서버의 인증 승인과 같지 않습니다.

## 구현 범위

- USART6 인터럽트 수신, AT 응답과 `+IPD` TCP 데이터 분리.
- TCP 패킷에 나뉘어 도착한 줄을 조립하고, 여러 줄을 각각 처리.
- 서버 메시지는 줄바꿈으로 끝나야 하며 최대 255바이트입니다.
- 과도한 길이의 메시지는 버리고, 메시지 큐가 가득 차면 로그를 남깁니다.
- `AT+CIPSEND`의 `>` 프롬프트를 기다린 뒤 데이터를 보내고 `SEND OK` 확인.
- 10초 간격의 상태 확인/연결 재시도. TIM3 추가 없이 `HAL_GetTick()` 사용.
- 연결 시도는 최대 수십 초 동안 메인 루프를 기다리게 하는 초기 통신 시험용 구현입니다.
  문 감지·경보를 통합할 때는 연결 절차를 비동기 상태 머신으로 바꿔야 합니다.
- UART 오버런/데이터 손실 발생 시 연결을 다시 초기화합니다. 모듈이 AT 명령에
  응답하지 않거나 전송 대기 상태에 남은 경우 모듈 리셋/전원 재시작이 필요할 수 있습니다.
- 현재 서버 명령은 보드 LD2 테스트에만 사용됩니다. 문 잠금·대여 인증은 다음 단계입니다.

AT 명령 기준: [Espressif ESP8266 TCP/IP AT Commands](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/TCP-IP_AT_Commands.html).

## C 자동 검증 코드

실제 DB 처리는 별도의 sql_client가 담당합니다. [두 프로그램 실행 안내](db-auth.md)를 따르세요.

`tools/test_wifi_server.c`는 STM32 대신 테스트 메시지를 보내는 개발용 코드입니다.
Linux에서 자체 테스트 서버를 실행해 LED 메시지, 카드 UID, 재접속 등을 검증합니다.
실제 보드 통신 확인에는 `wifi_test_server.c`만 실행하면 됩니다.

`tools/` 폴더에서 다음 명령으로 검증할 수 있습니다.

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -O2 wifi_test_server.c -o wifi_test_server
gcc -std=c11 -Wall -Wextra -Wpedantic -O2 test_wifi_server.c -o test_wifi_server
./test_wifi_server ./wifi_test_server
```
