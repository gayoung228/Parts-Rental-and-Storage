# 세대별 대여 데이터

라즈베리파이 `~/parts_storage`에 다음 파일을 복사합니다.

- `tools/rental_schema.sql`
- `tools/rental_runtime_schema.sql`
- `tools/sql_client.c`
- `tools/rental_db.h`
- `tools/rental_protocol.h`
- `tools/wifi_test_server.c`

기존 서버와 sql_client를 각각 Ctrl+C로 종료한 뒤 적용합니다.

```sh
cd ~/parts_storage
sudo mariadb < rental_schema.sql
sudo mariadb < rental_runtime_schema.sql
gcc -std=c11 -Wall -Wextra -O2 wifi_test_server.c -o wifi_test_server
gcc -std=c11 -Wall -Wextra -O2 sql_client.c -o sql_client $(pkg-config --cflags --libs libmariadb)
```

서로 다른 터미널에서 실행합니다. sql_client 터미널에는 기존 `DB_PASSWORD` 환경 변수가 필요합니다.

```sh
./wifi_test_server --port 5000
```

```sh
./sql_client 127.0.0.1 5000 SQL_CLIENT
```

최신 STM32 펌웨어 `build/Debug/Parts-Rental-and-Storage.elf`를 다운로드해야 완료 문구를 표시합니다.
Jetson에도 최신 `camera/iot_camera_client.py`를 복사하고 다시 실행합니다.
관리실 Arduino의 경보 펌웨어는 바뀌지 않았습니다.

## 시험 방법

1. 카드로 보관함을 열고 공구를 보여줍니다. LCD에 `Room 101` / `Rental complete`가 나오고 부저가 울립니다.
2. 아래 조회에서 공구 `rental_state=1`, `unreturned_count=1`, 금액 500원을 확인합니다.
3. 같은 이용 중 공구를 치웠다가 다시 보여줘도 대여/반납 상태는 바뀌지 않습니다.
4. 같은 카드로 닫았다 다시 카드로 열고 같은 공구를 보여주면 `Return complete`, 상태 0, 미반납 개수 0입니다.
5. 반납해도 발생한 대여료는 남습니다. 다른 세대가 빌린 공구는 `Tool in use`가 표시되며 상태를 바꾸지 않습니다.
6. 대여 5분 초과 즉시 연체료 500원, 연체 5분 초과마다 추가 500원입니다. 반납 후 연체료는 증가하지 않습니다.

`rental_state`는 미반납 대여에서 계산하는 상태입니다. 별도 숫자 필드를 중복 저장하지 않아 목록과 상태가 어긋나지 않습니다.

```sh
sudo mariadb parts_rental
```

```sql
SELECT building_no AS 동, unit_no AS 호수,
       unreturned_count AS 미반납개수, overdue_minutes_total AS 연체분합계,
       amount_due_won AS 청구예정금액
FROM household_rental_summary;

SELECT building_no AS 동, unit_no AS 호수, tool_no AS 공구번호,
       tool_name AS 공구이름, rental_date AS 대여일자,
       due_date AS 반납예정일, is_returned AS 반납여부,
       return_date AS 반납일자, overdue_minutes AS 연체분,
       amount_due_won AS 청구예정금액
FROM loan_list ORDER BY loan_id DESC;

SELECT tool_no AS 공구번호, tool_name AS 공구이름, rental_state AS 대여상태,
       household_id AS 세대ID, due_date AS 반납예정
FROM rental_tool_status ORDER BY id;
```

`households`가 부모이고 `loans`가 자식입니다. 같은 101호가 여러 동에 있을 수 있으므로 기존 `households.id`를 기본키로 유지하고 `(building_no, unit_no)`를 고유 주소로 사용합니다. `loans.household_id`는 세대, `loans.tool_id`는 `rental_tools.id`를 참조합니다.

공구는 `1=스트리퍼`, `2=드라이버`, `3=니퍼`, `4=롱노즈`, `5=멀티미터기`로 등록합니다. 현재는 번호 하나당 실물 공구 하나를 전제로 합니다. 같은 종류가 여러 개라면 각각 번호가 필요하며, 카메라의 종류 라벨과 실물 번호를 연결하는 처리는 별도로 필요합니다.

`loans`에 한국 시각의 대여 시각, 반납 예정 시각, 반납 시각과 대여 당시 요금을 저장합니다. 마이그레이션 전 DATE 기록은 같은 날 00:00:00으로 보존합니다. 대여 때 `rental_policy`를 읽어 300초, 500원, 연체 300초당 500원을 복사합니다. 기존 필드명 `daily_late_fee_won`은 이제 `late_fee_period_seconds`마다 붙는 요금입니다. 동일 실물 공구의 미반납 대여는 하나만 허용합니다.

`loan_list`와 `household_rental_summary`는 실제 기록에서 조회 시 집계하는 뷰입니다. 연체일은 만 24시간 단위이고 테스트에서는 연체분/연체초 열을 확인합니다. 미반납 개수에서 반납된 공구는 제외하지만 아직 청구하지 않은 대여료와 연체료는 반납 후에도 금액에 포함됩니다.

`loan_billing_items`는 월별 청구로 이전한 금액을 기록합니다. `billing_month`는 해당 월의 1일입니다. 이전된 금액은 청구 예정 금액에서 제외합니다. 이것은 납부 완료 기록이 아니며 이미 청구한 돈의 미납 관리는 별도 기능입니다. 청구 기록은 실제 발생한 요금 이내로 작성해야 합니다.

`rental_actions`가 보관함·카드 이용 세션·공구별 처리 결과를 기억합니다. 같은 이벤트 재전송은 원래 결과를 돌려주며, 같은 이용 중 새 인식 이벤트는 `UNCHANGED`로 처리합니다. 공구 행 잠금과 한 트랜잭션으로 인식 기록·대여 목록·처리 기록을 저장하며 오류 시 모두 되돌립니다. DB 적용 전의 인식 이력은 대여로 소급 변환하지 않습니다.

24시간 운영으로 전환하려면 관리자 계정으로 아래를 실행합니다. 변경 후 **새 대여**부터 24시간이며 기존 대여의 기한/요금은 유지됩니다.

```sql
UPDATE rental_policy SET duration_seconds=86400, late_fee_period_seconds=86400 WHERE id=1;
```

위 설정은 대여 24시간, 연체 24시간마다 500원입니다. 다시 5분 시험으로 바꾸려면 두 값을 300으로 설정합니다. 마이그레이션을 다시 적용해도 관리자 정책 변경은 보존됩니다.
