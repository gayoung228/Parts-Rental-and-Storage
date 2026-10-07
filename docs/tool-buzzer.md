# STM32 공구 인식 확인음

관리실 UNO의 개방 경보와 별도로 STM32에 짧은 확인음을 추가합니다. 수동 부저 기준입니다.

| 연결 | 위치 |
| --- | --- |
| 부저 구동 회로 입력 | PB1 / TIM3 CH4 |
| 구동 회로 GND | STM32 공통 GND |
| 부저 전원 | 실제 부저의 정격 전원 |

2핀 부저는 소비 전류를 확인하고 필요하면 트랜지스터 구동 회로를 사용합니다.
예: PB1 → 베이스 저항 → NPN B, NPN E → GND, NPN C → 부저 −, 부저 + → 정격 전원 +.
트랜지스터 B/C/E 위치는 부품마다 다릅니다. PB1은 제어 신호이며 5V 전원을 연결하지 않습니다.

## 동작

- TIM3 클록 84MHz, Prescaler 83, Period 499 → 2kHz PWM.
- 인식 결과가 DB에 저장되고 현재 세션에 전달되면 150ms 확인음.
- LCD 인식 결과와 동시에 동작합니다. NULL, 낮은 신뢰도, 저장 실패에는 울리지 않습니다.
- 최근 8개 이벤트 ID를 세션별로 기억해서 재전송에 중복 확인음을 내지 않습니다.
- 자동 대여 기능에서는 같은 세션의 같은 공구는 한 번만 처리하고 소리도 반복하지 않습니다.
- UART 대기 중에도 종료 시간을 검사합니다. Wi-Fi 재연결 전에는 부저를 끕니다.
- 길이는 Core/Inc/tool_buzzer.h의 TOOL_BUZZER_DURATION_MS에서 조정합니다.
- .ioc도 PB1 TIM3_CH4 PWM으로 변경했습니다. 서보는 TIM1 CH1을 사용합니다.

## 적용

새 STM32 펌웨어를 다운로드합니다. 최신 tools/wifi_test_server.c를 Pi에 복사하고 서버를 Ctrl+C로 종료한 뒤:

```sh
cd ~/parts_storage
gcc -std=c11 -Wall -Wextra -O2 wifi_test_server.c -o wifi_test_server
./wifi_test_server --port 5000
```

시작 로그에서 C v11 - automatic rentals + returns를 확인합니다.
자동 대여/반납 기능을 사용할 때는 [대여 DB 안내](rental-db.md)에 따라 sql_client와
Jetson 카메라 코드도 업데이트합니다. UNO의 관리실 경보 펌웨어는 유지합니다.
이미 열린 보관함은 같은 카드로 닫았다가 다시 열어 새 세션을 만듭니다.

```text
[BUZZER] PB1 PWM ready (2kHz)
Server: [SERVER]TOOL@0000123400000001@1@0000123400000002@RENTED
[BUZZER] Beep 150ms - tool=1 event=0000123400000002
```

실제 소리와 전원·배선은 장비에서 확인해야 합니다.
