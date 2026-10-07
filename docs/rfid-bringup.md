# RC522 카드 UID 테스트

| RC522 | STM32 |
| --- | --- |
| SDA / SS | PB12 (SPI CS) |
| SCK | PB13 |
| MISO | PB14 |
| MOSI | PB15 |
| RST | PC4 |
| 3.3V | 3.3V |
| GND | GND |
| IRQ | 미연결 |

RC522의 SDA 표기는 이 설정에서 SPI CS입니다. LCD I²C 핀에 연결하지 않습니다.
RC522 전원은 3.3V입니다. ESP-01 어댑터의 전원과 구분하세요.

## 실행

1. 새 STM32 펌웨어를 다운로드하고 실행합니다.
2. USART2 디버그 터미널을 115200 baud로 열어 시작 로그를 확인합니다.
   `[RFID] Version=0x92; ready - tag one card` 같은 로그가 출력됩니다.
   버전 값은 칩에 따라 다릅니다. `0x00` 또는 `0xFF`이면 초기화를 실패 처리합니다.
3. 지원 카드 한 장을 가까이 대면 `[RFID] UID=...`가 출력됩니다.
4. 서버가 연결되어 있으면 `[LOCKER_101]CARD@A1B2C3D4@0000123400000001\n` 같은 메시지를 전송합니다.
5. 라즈베리파이에 **수정된** `tools/wifi_test_server.c`를 다시 복사합니다.
   실행 중인 서버는 Ctrl+C로 종료하고 재컴파일합니다.

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -O2 wifi_test_server.c -o wifi_test_server
./wifi_test_server --port 5000
```

6. 서버는 UID를 출력하고 `[SERVER]CARD@A1B2C3D4@0000123400000001@RECEIVED\n`를 반환합니다.
   STM32 터미널의 `Server: ...@RECEIVED`로 왕복 확인이 가능합니다.
   이어서 `[RFID] Server receipt confirmed: UID=...`가 출력됩니다.

서버 시작 시 `wifi_test_server C v10 - camera sessions + recognition beep; Listening on port 5000`를 확인하세요.
STM32는 전송할 카드 메시지와 바이트 수를 출력하고, 서버는 TCP 수신 바이트 수와
줄바꿈 수를 출력합니다. 5초 내 카드 수신 확인이 없으면 STM32는
`Server receipt TIMEOUT`을 출력합니다. ESP의 `SEND OK`만으로 서버 수신을 확정하지 않습니다.

## 동작 범위

- ISO14443A 카드 한 장의 4/7/10바이트 UID를 읽습니다. 여러 카드 충돌은 오류 처리합니다.
- 200ms 간격으로 확인하고, 같은 카드를 계속 대고 있으면 중복 전송하지 않습니다.
  다시 전송하려면 카드를 약 1초 떼었다가 대세요.
- 카드 선택 후 또는 선택 오류 후 RF 필드를 잠깐 껐다 켜서 다음 판독 세션을 시작합니다.
- 서버 연결이 없거나 전송이 실패하면 로그만 남깁니다. 연결 복구 후 카드를 다시 대야 합니다.
- 현재 `RECEIVED`는 **수신 확인**이며 주민 인증 승인이 아닙니다. 문을 열지 않습니다.
  DB 인증 모드는 [DB 인증 안내](db-auth.md)를 따르세요.
  중계 서버와 sql_client 두 프로그램을 동시에 실행합니다.
- 카드 메모리 읽기/쓰기와 대여·반납은 아직 구현하지 않았습니다.
- Wi-Fi 재연결의 대기 구간에는 카드 확인도 지연됩니다.
- RC522는 모든 RFID 카드를 읽는 장치가 아닙니다. 먼저 동봉된 호환 카드/태그로 확인하세요.

레지스터 기준: [NXP MFRC522 데이터시트](https://www.nxp.com/docs/en/data-sheet/MFRC522.pdf).
