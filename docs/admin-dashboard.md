# 관리자 조회 화면

5분 대여 설정을 유지하며 기존 MariaDB를 조회하는 화면입니다. HTTP 서버는 C이고 브라우저 화면은 HTML/CSS/JavaScript입니다. 별도 프레임워크나 Python 서버는 필요 없습니다.

## 표시 정보

- 세대별 미반납 개수, 현재 미반납 공구의 연체 시간 합계, 청구 예정 금액
- 공구별 대여 가능/대여 중/연체 중 상태, 이용 세대, 반납 예정 시각
- 대여·반납 이력: 세대/공구/상태 필터, 최신순 20건 페이지
- DB에 설정된 대여 시간·대여료·연체료

5초마다 자동 갱신합니다. 자동 갱신을 해제하거나 직접 새로고침할 수 있습니다. DB 장애 때 마지막 데이터가 남아 있다면 오래된 데이터임을 표시합니다.

반납한 대여의 요금도 청구 예정 금액에 남습니다. 미반납 개수가 0이어도 청구 예정 금액이 있을 수 있습니다. 월별 청구로 이전한 금액은 제외되며 이 화면은 청구 이후의 납부 여부를 표시하지 않습니다.

## Pi 파일 복사

업데이트 압축파일 `build/admin-dashboard-update.zip`에는 C 서버와 화면 파일 세 개가 함께 들어 있습니다. 프로젝트 폴더의 PC PowerShell에서 다음처럼 옮길 수 있습니다. IP는 실제 Pi 주소에 맞춥니다.

```powershell
scp .\build\admin-dashboard-update.zip pi@10.10.16.80:~/parts_storage/
```

Pi의 관리자 서버를 Ctrl+C로 종료하고 압축을 풉니다. 대여 서버와 sql_client는 계속 실행해 둡니다.

```sh
cd ~/parts_storage
unzip -o admin-dashboard-update.zip
```

아래 컴파일 명령으로 서버를 다시 빌드해 실행하고 브라우저에서 Ctrl+Shift+R로 새로고침합니다. CSS는 `/dashboard.css?v=2`로 불러옵니다. 서버 시작 시 세 화면 파일이 모두 존재하는지도 확인합니다.

`tools/admin_server.c`와 `admin` 폴더 전체를 Pi에 복사합니다. 다음 구조로 놓으세요.

```text
~/parts_storage/
  admin_server.c
  admin/
    index.html
    dashboard.css
    dashboard.js
```

기존 대여 프로그램과 DB를 실행한 채 관리자 화면용 터미널을 하나 더 엽니다. 기존 `rental_runtime_schema.sql`까지 적용되어 있어야 합니다. 추가 SQL 적용이나 STM32/UNO 펌웨어 변경은 없습니다.

## 컴파일

```sh
cd ~/parts_storage
gcc -std=c11 -Wall -Wextra -O2 admin_server.c -o admin_server $(pkg-config --cflags --libs libmariadb)
```

`pkg-config` 또는 MySQL 헤더가 없다면 기존 DB 클라이언트와 동일하게 개발 패키지를 설치합니다.

```sh
sudo apt install pkg-config libmariadb-dev
```

## 실행

이 새 터미널에도 DB 비밀번호 환경 변수가 필요합니다. 아래 명령 한 줄을 실행한 다음 실제 비밀번호를 입력하고 Enter를 누릅니다. 입력 내용은 화면에 표시되지 않습니다.

```sh
read -r -s -p 'DB password: ' DB_PASSWORD
```

비밀번호 입력이 끝난 뒤 다음을 실행합니다.

```sh
echo
export DB_PASSWORD
./admin_server --port 8080 --web-root admin
```

Pi의 현재 IP를 확인합니다.

```sh
hostname -I
```

같은 네트워크의 PC 브라우저에서 `http://Pi의IP:8080/`을 엽니다. 예: `http://10.10.16.80:8080/`.
Pi 자체 브라우저에서는 `http://127.0.0.1:8080/`을 사용합니다.

서버 기본 주소는 `0.0.0.0`, 포트는 8080입니다. 같은 LAN에서 보는 시연용 조회 화면이며 로그인 기능은 포함하지 않습니다. Pi에서만 보려면 `--bind 127.0.0.1`을 추가합니다.

DB 연결 설정은 기존 `DB_HOST`, `DB_USER`, `DB_NAME`, `DB_PASSWORD`, 선택적 `DB_SOCKET` 환경 변수를 사용합니다. 기본값은 localhost / parts_app / parts_rental입니다. DB 연결 정보는 브라우저로 보내지 않습니다.

## 동작 확인

카드로 공구를 대여하면 세대 미반납 개수, 공구 상태, 이력과 금액이 다음 갱신에서 변경됩니다. 5분 초과 시 연체로 표시되고 반납하면 미반납 개수만 줄면서 발생한 요금은 유지됩니다. 세대의 `이력 보기` 버튼을 누르면 해당 세대 필터가 적용됩니다.

HTTP 서버는 읽기 전용 DB 트랜잭션을 사용하며 GET 요청만 처리합니다. 대여 정책과 대여 기록은 변경하지 않습니다. 페이지/API 필터와 DB 장애·JSON 한글 처리·5분 정책 보존은 격리된 실제 MariaDB와 C HTTP 테스트에서 확인했습니다. JavaScript의 갱신·필터·오류 복구 동작도 검증했습니다. 시험 데이터를 사용한 실제 Chrome의 PC·모바일 화면 표시를 확인했습니다. Pi의 실제 DB 연결과 네트워크 접속은 장비에서 확인해야 합니다.

## 스타일이나 DB 오류가 표시될 때

`Missing dashboard file`이 서버 터미널에 뜨면 `admin` 폴더 전체를 옮기고 `--web-root`를 확인합니다. 브라우저에서 `/dashboard.css?v=2` 주소를 열면 CSS 내용이 나와야 합니다.

DB 오류는 화면에 오류 번호가 나오고 서버 터미널에 상세 원인이 출력됩니다. 1045는 DB 계정/비밀번호, 1142는 조회 권한, 1146은 필요한 테이블 누락입니다. 1356은 뷰 정의 또는 조회 권한을 확인합니다. 기본 계정은 `parts_app`, DB는 `parts_rental`입니다. 정책·뷰가 필요하면 [대여 DB 안내](rental-db.md)의 SQL 적용 순서를 확인하세요.
