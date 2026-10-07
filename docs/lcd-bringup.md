# 1602 LCD 사용자 안내

현재 사용자는 영문 표시를 선택했습니다. 일반 HD44780 호환 1602의 한글 글꼴 제한과
16칸 길이 때문에 둘째 줄 안내는 영문 스크롤로 표시합니다.

## 배선

PCF8574/PCF8574A I²C 변환 보드가 달린 LCD를 사용합니다.

| LCD | STM32 |
| --- | --- |
| SCL | PB8 / I2C1 SCL |
| SDA | PB9 / I2C1 SDA |
| GND | 공통 GND |
| VCC | LCD 모듈의 정격 전원. 일반 5V용 모듈이면 5V |

현재 PB8/PB9는 대체 기능 오픈드레인 설정이며,
[STM32F411 데이터시트](https://www.st.com/resource/en/datasheet/stm32f411re.pdf)의 FT 핀입니다.
다른 핀이나 ADC 핀에 LCD의 5V 신호를 연결하지 않습니다.
전원을 끄고 배선하며, 5V LCD 전원과 STM32 전원을 함께 공급하고 GND를 공통으로 연결하세요.

드라이버는 0x27과 0x3F를 먼저 확인하고 PCF8574 계열 주소 범위를 검사합니다.
Core/Inc/lcd1602.h의 LCD1602_ADDRESS를 지정하면 고정 주소를 사용합니다.
변환 보드 핀 매핑은 P0=RS, P1=RW, P2=EN, P3=백라이트, P4~P7=데이터를 가정합니다.
글자가 안 보이면 모듈 뒤의 대비 조절 가변저항을 천천히 조정하세요.

## 화면

대기:

```text
Tap RFID card

```

등록 카드 첫 태그 승인 후:

```text
Room 101
Show tool to cam
```

둘째 줄 전체 메시지 Show tool to camera를 약 350ms 간격으로 스크롤합니다.
첫째 줄 호수는 고정입니다. D3693D06이 102호로 등록돼 있으면 Room 102가 나옵니다.
같은 카드 재태그로 잠그면 대기 화면으로 돌아갑니다.
승인 대기, 거절, 다른 카드 이용 시도, 서버 오류에는 짧은 안내를 표시합니다.
세대 번호는 카드 이벤트의 household_id가 아니라 households.unit_no를 사용합니다.

## 업데이트

1. 새 STM32 펌웨어를 다운로드하고 LCD를 연결한 상태에서 리셋합니다.
2. 최신 tools/wifi_test_server.c, sql_client.c, rental_protocol.h를 Pi에 다시 복사합니다.
3. 중계 서버와 sql_client를 Ctrl+C로 종료하고 다시 빌드합니다.

```sh
cd ~/parts_storage
gcc -std=c11 -Wall -Wextra -O2 wifi_test_server.c -o wifi_test_server
gcc -std=c11 -Wall -Wextra -O2 sql_client.c -o sql_client $(pkg-config --cflags --libs libmariadb)
```

서버 터미널:

```sh
./wifi_test_server --port 5000
```

sql_client 터미널(DB_PASSWORD 설정 유지):

```sh
./sql_client 127.0.0.1 5000 SQL_CLIENT
```

시작 로그는 wifi_test_server C v10 - camera sessions + recognition beep 입니다.
DB 테이블 구조 변경이나 UNO 펌웨어 재업로드는 필요 없습니다.
기존 Bluetooth 중계와 RFCOMM 연결은 유지합니다.

## 확인할 로그

```text
[LCD] Ready at I2C address=0x27
Server: [SERVER]USER@E6044006@0000123400000001@101
Server: [SERVER]AUTH@E6044006@0000123400000001@APPROVED
```

USER 메시지는 현재 요청 UID/ID와 일치할 때만 기억하고, 승인 후 LCD에 표시합니다.
Room ?가 나오면 서버와 sql_client가 최신인지, USER 메시지가 도착했는지 확인하세요.
LCD init FAILED면 I²C 전원·GND·배선·주소를 확인합니다.

기준 자료: [HD44780U](https://www.waveshare.com/datasheet/LCD_en_PDF/HD44780.pdf),
[PCF8574/PCF8574A](https://www.nxp.com/docs/en/data-sheet/PCF8574_PCF8574A.pdf).
