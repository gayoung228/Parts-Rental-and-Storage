# SG90 승인 연동 시험

| SG90 | 연결 |
| --- | --- |
| 주황색 신호 | PA8 / TIM1 CH1 |
| 빨간색 전원 | 외부 안정화 5V 전원 + |
| 갈색 GND | 외부 전원 - 및 STM32 GND 공통 |

문 잠금 장치와 분리해 회전부터 확인하세요. 현재는 카드 재태그로 서보 잠금 위치를 제어합니다.
CDS 문 상태는 표시하며 자동 잠금 조건으로 사용하지 않습니다.

## 동작

- 시작: 잠금 시험 위치(1000µs)로 PWM 출력.
- 잠금 상태에서 현재 카드 요청 UID·요청 ID에 일치하는 APPROVED: 해제 위치(2000µs)로 이동.
- 해제 상태에서 같은 카드의 새 태그 요청이 승인되면 잠금 위치(1000µs)로 이동.
- 자동 복귀 시간은 없습니다. 카드를 1초 이상 떼었다가 다시 대세요.
- 문을 연 카드 UID를 STM32에 기억하며, 다른 카드는 해당 세션을 전환할 수 없습니다.
- DENIED, ERROR, 응답 시간 초과, 잘못된 요청 ID, 중복 승인: 현재 상태 유지.
- 카드 태그 시 현재 제어 상태를 `State=LOCK` 또는 `State=UNLOCK`으로 표시합니다.
- 해제 중에도 같은 카드의 인증 요청과 Wi-Fi 재연결이 가능합니다.
- 열린 상태와 세션 소유 UID는 RAM에 저장합니다. MCU 재시작 시 잠금 위치로 초기화됩니다.
  DB에 잠금/카드 세션 상태 필드를 추가하지 않았습니다.

`Core/Inc/servo_lock.h`에서 펄스 폭과 인증 응답 제한 시간을 조정할 수 있습니다.
1000µs/2000µs는 약 90도 이동을 목표로 한 시험값이며 실제 회전 폭은 서보 개체에 따라 다릅니다.
실제 잠금 위치는 장착 방향에 맞춰 보정해야 합니다.
서보가 명령 위치에 도달했는지는 현재 별도 센서로 확인하지 않습니다.

## 서버도 업데이트 필요

최신 중계 서버와 sql_client를 [DB 자동 저장 안내](db-auth.md)에 따라 설치합니다.
DB 계정과 DB_PASSWORD는 sql_client에서 사용합니다.

```sh
cd ~/parts_storage
gcc -std=c11 -Wall -Wextra -O2 wifi_test_server.c -o wifi_test_server
./wifi_test_server --port 5000
```

시작 로그: `wifi_test_server C v10 - camera sessions + recognition beep; Listening on port 5000`.
두 번째 터미널에서 sql_client도 실행해야 카드 승인 처리가 됩니다.

새 STM32 펌웨어를 다운로드하고 활성 카드 한 장을 태그합니다.

```text
[SERVO] State=LOCK (card tagged)
[AUTH] APPROVED UID=E6044006
[SERVO] State=UNLOCK (approved first tag)
[RFID] Session=ACTIVE owner=E6044006; retag the same card to lock
# 카드를 떼었다가 같은 카드 다시 태그
[SERVO] State=UNLOCK (card tagged)
[AUTH] APPROVED UID=E6044006
[SERVO] State=LOCK (approved second tag)
[RFID] Session=IDLE
```

비활성 카드에서는 DENIED가 출력되고 서보는 기존 위치를 유지합니다.
표시 상태는 서보에 내린 제어 명령 기준입니다. 실제 걸쇠 위치를 확인하는 피드백 센서는 없습니다.

## 메시지

```text
[LOCKER_101]CARD@E6044006@0000123400000001
[SERVER]CARD@E6044006@0000123400000001@RECEIVED
[SERVER]AUTH@E6044006@0000123400000001@APPROVED
```

각 메시지는 줄바꿈으로 끝납니다. 16자리 요청 ID는 현재 부팅 내 요청을 구분하며,
응답은 10초 안에 한 번만 처리합니다. 이전 ID 없는 AUTH 응답은 서보를 움직이지 않습니다.
요청 ID는 암호학적 장치 인증을 대신하지 않습니다.
