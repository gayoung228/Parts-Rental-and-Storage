# Parts-Rental-and-Storage

STM32F411RE 기반 동별 부품 대여·반납 보관함 프로젝트입니다.

Wi-Fi 모듈 설정과 라즈베리파이 통신 테스트: [Wi-Fi 연결 안내](docs/wifi-bringup.md).

카드 UID 읽기와 서버 전송 테스트: [RC522 연결 안내](docs/rfid-bringup.md).

중계 서버 → C sql_client → MariaDB 자동 저장·카드 인증: [실행 안내](docs/db-auth.md).

승인 응답에 SG90 회전 연결: [서보 시험 안내](docs/servo-bringup.md).

문 상태 판정 전 조도 측정: [CDS 연결 안내](docs/cds-bringup.md).

비정상 문 열림 → 관리실 UNO 부저·LED: [관리실 경보 안내](docs/management-alarm.md).

카드 태그 안내·호수·카메라 안내 스크롤: [1602 LCD 안내](docs/lcd-bringup.md).

Jetson Python 인식 → C 중계/sql_client → 세대별 인식 기록: [카메라 연결 안내](docs/camera-bringup.md).

공구 인식 저장 후 STM32 PB1 확인음: [로컬 부저 안내](docs/tool-buzzer.md).

세대별 미반납·연체·요금 및 대여 목록: [대여 DB 안내](docs/rental-db.md).

세대·공구 상태와 대여 이력을 보는 웹 화면: [관리자 화면 안내](docs/admin-dashboard.md).
