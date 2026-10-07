# sql_client를 통한 자동 DB 저장

현재 구성은 **STM32 → Wi-Fi → 중계 서버 → sql_client → MariaDB**입니다.
서버는 MySQL 라이브러리를 사용하지 않습니다. DB 조회·INSERT·UPDATE는 sql_client만 실행합니다.

| 프로그램 | 역할 |
| --- | --- |
| wifi_test_server.c | 여러 STM32와 SQL_CLIENT 동시 접속, 메시지 중계 |
| sql_client.c | 카드 인증, 카드 요청 이력·CDS ADC 값 자동 저장, 결과 응답 |
| rental_protocol.h | 두 C 프로그램이 공유하는 메시지 검사 |
| sql_client_schema.sql | 추가 테이블 및 기존 parts_app 계정 권한 설정 |

원본 예제의 sql_client.c 대신 **이 프로젝트의 tools/sql_client.c**를 사용하세요.
원본 파일은 수정하지 않았습니다. 원본 SENSOR/GETDB/SETDB 메시지는 새 프로토콜과 다릅니다.
검증용 test_wifi_server.c는 Pi에 복사하거나 실행할 필요 없습니다.

## 1. 파일 복사와 DB 준비

위 네 파일을 모두 라즈베리파이 ~/parts_storage에 복사합니다.
기존 households, rfid_cards와 parts_app 계정은 그대로 사용합니다.
추가 테이블을 만들기 위해 다음 명령을 **Pi 셸에서 한 번** 실행합니다.

~~~sh
cd ~/parts_storage
sudo mariadb < sql_client_schema.sql
~~~

기존 카드와 세대는 삭제하지 않습니다.

- card_events: 보관함 ID, 카드 UID, 요청 ID, 세대 ID, 승인/거절 결과, 수신 시각.
- cds_samples: 보관함 ID, 샘플 ID, CDS ADC 값, 수신 시각.
- 같은 카드 요청을 다시 받으면 새 행을 만들지 않고 수신 횟수·결과·마지막 시각을 UPDATE합니다.
- 잠금/해제 및 카드 이용 세션 상태는 현재 STM32 RAM에서 관리하며 DB에 저장하지 않습니다.
- 아직 대여·반납이나 요금 기록은 생성하지 않습니다.

## 2. 두 프로그램 빌드

~~~sh
cd ~/parts_storage
gcc -std=c11 -Wall -Wextra -O2 wifi_test_server.c -o wifi_test_server
gcc -std=c11 -Wall -Wextra -O2 sql_client.c -o sql_client $(pkg-config --cflags --libs libmariadb)
~~~

중계 서버에는 -DENABLE_MARIADB나 MariaDB 링크 옵션이 필요 없습니다.

## 3. 첫 번째 터미널: 중계 서버

기존 서버는 Ctrl+C로 종료하고 새 서버를 실행합니다.

~~~sh
./wifi_test_server --port 5000
~~~

시작 로그: wifi_test_server C v10 - camera sessions + recognition beep; Listening on port 5000.
이제 --db 옵션은 사용하지 않습니다.

## 4. 두 번째 터미널: sql_client

이 터미널은 계속 실행한 채 둡니다. DB 비밀번호는 기존 parts_app 계정의 비밀번호입니다.
아래 명령은 **한 줄씩 실행**하세요.

~~~sh
cd ~/parts_storage
export DB_HOST=localhost
export DB_USER=parts_app
export DB_NAME=parts_rental
read -r -s -p 'DB password: ' DB_PASSWORD
~~~

DB password:가 나오면 비밀번호를 입력하고 Enter를 누릅니다. 입력 문자는 화면에 보이지 않습니다.
그다음 다음 명령을 한 줄씩 실행합니다.

~~~sh
echo
export DB_PASSWORD
./sql_client 127.0.0.1 5000 SQL_CLIENT
~~~

이 경우 127.0.0.1은 올바릅니다. sql_client는 같은 Pi의 중계 서버에 연결하는 역할입니다.
서버에서 SQL_CLIENT registered, sql_client에서 SQL_CLIENT ready 로그를 확인하세요.
이 역할은 루프백 접속만 허용하고, SQL_CLIENT 한 개만 등록합니다.

## 5. STM32 실행

CDS 전송 기능이 추가된 새 펌웨어를 다운로드하고 보드를 실행합니다.
ESP_SERVER_HOST는 여전히 **Pi의 Wi-Fi/LAN IP**입니다. 127.0.0.1로 바꾸지 마세요.

- 카드 태그: sql_client가 DB 조회 후 card_events 저장을 커밋하고 APPROVED/DENIED 반환.
- 승인 시 households.unit_no도 USER 메시지로 전달하여 LCD에 호수를 표시합니다.
- CDS: 약 5초 간격으로 ADC 값을 보내며 sql_client가 cds_samples에 저장하고 SAVED 반환.
- 카드 승인 대기 중에는 CDS 전송을 미룹니다.
- CDS 샘플은 오프라인에서 누적하지 않습니다. 연결 복구 후 다음 측정부터 보냅니다.
- sql_client가 없거나 DB 조회·저장에 실패하면 ERROR. 저장 실패 시 승인하지 않습니다.
- sql_client는 중계 연결이 끊어지면 1초 간격으로 재접속합니다.

메시지 예:

~~~text
STM32 → relay → sql_client:
[LOCKER_101]CARD@E6044006@0000123400000001

sql_client → relay:
[LOCKER_101]AUTH@E6044006@0000123400000001@APPROVED

relay → STM32:
[SERVER]AUTH@E6044006@0000123400000001@APPROVED

STM32 → relay → sql_client:
[LOCKER_101]SENSOR@3800@0000123400000002

sql_client → relay → STM32:
[SERVER]SENSOR@0000123400000002@SAVED
~~~

각 메시지는 줄바꿈으로 끝납니다. 중계 서버는 요청 ID·UID·대상 보관함이 일치하는 응답만 전달합니다.
RECEIVED는 서버 수신 확인이고 SAVED/APPROVED는 DB 처리 이후 응답입니다.

## 6. 자동 저장 확인

검증할 때만 세 번째 터미널에서 sudo mariadb로 접속합니다.
수동으로 INSERT/UPDATE하지 않아도 sql_client가 기록을 저장합니다.

~~~sql
USE parts_rental;
SELECT id, locker_id, uid, household_id, auth_result, received_count, received_at
FROM card_events ORDER BY id DESC LIMIT 10;

SELECT id, locker_id, adc_value, received_at
FROM cds_samples ORDER BY id DESC LIMIT 10;
~~~

카드 비활성화 검증은 기존과 같습니다.

~~~sql
UPDATE rfid_cards SET enabled=0 WHERE uid='D3693D06';
-- 카드를 태그하면 DENIED와 해당 card_events 행이 기록됩니다.
UPDATE rfid_cards SET enabled=1 WHERE uid='D3693D06';
~~~

쿼리의 값은 [MariaDB prepared statement](https://mariadb.com/docs/connectors/mariadb-connector-c/api-prepared-statement-functions/mysql_stmt_bind_param)로 바인딩합니다.
기존 요청 ID는 부팅 내 요청 구분용이며 암호학적 장치 인증을 대신하지 않습니다.

