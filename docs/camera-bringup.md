# Jetson 공구 인식 연결

카메라만 Python을 사용합니다. 중계 서버, sql_client, STM32, Arduino는 C입니다.
원본 iot_client_detect_debouncing.pyy 대신 camera/iot_camera_client.py를 실행합니다.
원본 파일은 수정하지 않았습니다.

## 현재 구현 범위

카드 승인과 실제 STM32 해제 제어 이후 이용 세션을 중계 서버에 보고합니다.
카메라가 인식한 공구 코드·신뢰도는 그 세션의 카드 UID와 세대에 연결해 저장합니다.
LCD에는 Room 101 및 Tool 1 detected 같은 인식 결과를 표시합니다.
이 데이터는 **공구 인식 이력**입니다. 대여 스키마와 최신 C 클라이언트를 적용하면
사용자가 정한 방식으로 인식을 대여/반납에 연결합니다. [대여 DB 안내](rental-db.md)를 따르세요.

라벨 순서는 제공한 labels.txt를 그대로 사용합니다.

| 모델 출력 인덱스 | 라벨 |
| --- | --- |
| 0 | 1 |
| 1 | 2 |
| 2 | 3 |
| 3 | 4 |
| 4 | 5 |
| 5 | NULL (공구 없음) |

공구 이름은 `1=스트리퍼`, `2=드라이버`, `3=니퍼`, `4=롱노즈`, `5=멀티미터기`입니다.
인식 이력에는 숫자 라벨, 대여 목록에는 공구 번호와 이름을 표시합니다.
2026-10-07에 제공한 `model (1).tflite`와 `labels (1).txt`를 프로젝트의
`camera/model.tflite`와 `camera/labels.txt`로 반영했습니다.
모델은 float32 [1,224,224,3] 입력 및 float32 [1,6] 출력을 사용합니다.

## 중복 인식과 이용 세션

- 신뢰도 90% 이상, 같은 클래스 3프레임 연속 확인 후 전송.
- 공구가 계속 보이는 동안 다시 전송하지 않습니다.
- NULL/낮은 신뢰도 3프레임으로 공구가 사라진 상태를 확인하면 다시 인식할 수 있습니다.
- 전송 간 최소 시간은 2초이며, 이 시간 안에 인식됐더라도 안정된 결과를 기다렸다 전송합니다.
- 카메라는 현재 OPEN 세션을 받아야 결과를 보냅니다. 카드 태그 없이 인식하면 전송하지 않습니다.
- 이벤트 ID는 랜덤 16자리 hex입니다. 응답 유실 시 같은 ID로 최대 3회 전송합니다.
- 같은 이벤트 ID를 중복 저장하지 않으며, 같은 ID에 다른 내용이 오면 거절합니다.
- 다른 카드 이용 세션으로 바뀌면 이전 세션의 미완료 이벤트를 버립니다.
- STM32가 연결을 끊거나 15초 동안 세션을 보고하지 않으면 카메라 전송을 중단합니다.
- 서버를 재시작한 경우 이미 열린 보관함의 승인 캐시가 사라집니다.
  같은 카드로 닫았다 다시 승인받아 열면 새 세션이 만들어집니다.
- 세션 상태는 MCU/서버 RAM에만 유지하고, DB에는 각 인식 이벤트의 세션 ID를 기록합니다.
- 정상 공구 인식은 관리실 부저를 울리지 않습니다. 관리실 부저는 비정상 문 열림 경보 전용입니다.
- 공구 인식 결과 저장 후 STM32 PB1 부저가 150ms 확인음을 냅니다. [로컬 부저 안내](tool-buzzer.md)를 따르세요.

## Pi 업데이트

최신 tools/wifi_test_server.c, sql_client.c, rental_db.h, rental_protocol.h,
camera_schema.sql, rental_schema.sql, rental_runtime_schema.sql을
Pi ~/parts_storage에 복사합니다. 기존 세대·카드와 이력은 삭제하지 않습니다.
추가 테이블은 한 번 생성합니다.

```sh
cd ~/parts_storage
sudo mariadb < camera_schema.sql
sudo mariadb < rental_schema.sql
sudo mariadb < rental_runtime_schema.sql
gcc -std=c11 -Wall -Wextra -O2 wifi_test_server.c -o wifi_test_server
gcc -std=c11 -Wall -Wextra -O2 sql_client.c -o sql_client $(pkg-config --cflags --libs libmariadb)
```

서버와 sql_client를 각각 Ctrl+C로 종료한 뒤 서로 다른 터미널에서 실행합니다.

```sh
./wifi_test_server --port 5000
```

```sh
./sql_client 127.0.0.1 5000 SQL_CLIENT
```

sql_client 터미널에는 기존 DB_PASSWORD 설정이 필요합니다.
비밀번호 설정은 [DB 실행 안내](db-auth.md)를 따릅니다.
서버 시작 로그는 C v11 - automatic rentals + returns 입니다.
기존 Bluetooth 중계와 관리실 UNO 펌웨어는 그대로 사용합니다.

## STM32 업데이트

새 펌웨어를 다운로드합니다. 첫 카드 승인 시 다음 세션 보고가 나옵니다.

```text
[LOCKER_101]SESSION@E6044006@0000123400000001@OPEN
```

다시 같은 카드로 잠그면 CLOSED를 보고합니다.
코드의 SESSION은 서보 제어 상태에 따른 이용 세션이며 실제 문 위치 센서 확인과는 별개입니다.

## Jetson 준비

다음 세 파일을 Jetson의 같은 폴더에 복사합니다.

- camera/iot_camera_client.py
- camera/model.tflite
- camera/labels.txt

기존 예제에서 사용한 OpenCV, NumPy, TFLite 런타임 환경을 유지합니다.
ai_edge_litert, tflite_runtime, TensorFlow 중 설치된 Interpreter를 순서대로 사용합니다.
새 환경이면 실제 Jetson의 Python/JetPack 버전에 맞는 패키지를 준비해야 합니다.
모델 로딩부터 확인합니다.

```sh
python3 iot_camera_client.py --inspect-model
```

입력/출력 모양, 라벨과 시험 추론 결과가 출력됩니다. 이 명령은 카메라나 서버 연결이 필요 없습니다.

## Jetson 실행

Pi에서 hostname -I로 현재 내부 IP를 확인합니다.
아래 10.10.16.80은 예시이며 현재 Pi IP로 바꾸세요.

```sh
python3 iot_camera_client.py --host 10.10.16.80 --port 5000 --locker LOCKER_101 --camera 0
```

기본은 USB 카메라 0이며 영상 창을 띄우지 않습니다. GUI 환경에서는 --preview 옵션을 추가할 수 있습니다.
다른 카메라라면 --camera 1 또는 영상 경로를 지정합니다.
카메라 하나는 해당 --locker 보관함에 연결됩니다. 한 번에 공구 한 개를 보여주세요.

카드로 보관함을 연 뒤 공구를 보여주면 다음처럼 나옵니다.

```text
[SESSION] Session(uid='E6044006', identifier='0000123400000001')
[DETECT] queued label=1 confidence=0.950 uid=E6044006
[DETECT] event=... result=RENTED
```

모델/USB 카메라 없이 메시지 경로만 시험할 때는 --simulate-label 1을 사용합니다.
이는 가짜 인식 결과를 전송하는 개발 시험 옵션입니다.

## DB 확인

```sql
USE parts_rental;
SELECT d.id, d.locker_id, d.uid, h.building_no, h.unit_no,
       d.tool_label, d.confidence_milli, d.received_at
FROM tool_detections d
JOIN households h ON h.id=d.household_id
ORDER BY d.id DESC LIMIT 10;
```

confidence_milli=950은 신뢰도 95%입니다.
실제 Jetson USB 카메라의 인식 정확도와 공구별 라벨 매핑은 장비에서 확인해야 합니다.
PC에서는 제공 모델 로딩·시험 추론, 카메라 상태 로직, 실제 임시 DB와 C 서버 통합 경로를 검증했습니다.

Interpreter API 기준: [Google TensorFlow Lite Python API](https://www.tensorflow.org/api_docs/python/tf/lite/Interpreter).
