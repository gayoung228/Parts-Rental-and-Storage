# Parts-Rental-and-Storage

물품 대여/반납 시스템의 IoT 구성 요소 및 소스 코드 저장소입니다.

## Project Structure

### `01_raspberry-pi`

Raspberry Pi에서 실행되는 IoT 서버 및 클라이언트 코드입니다.

- 소켓 서버
- Bluetooth 클라이언트
- SQL 클라이언트
- 물체 인식 결과에 따른 Arduino 제어

### `02_jetson`

Jetson에서 실행되는 물체 인식 코드입니다.

- `iot_client_detect_debouncing.py`
- 카메라를 이용한 물체 인식
- Teaching Machine 사이트에서 학습한 모델을 TensorFlow Lite (`.tflite`) 모델로 활용
- 물체 인식 결과를 Raspberry Pi 서버로 전송
- Debouncing을 적용하여 동일 물체의 중복 인식 방지

### `03_arduino`

Arduino에서 실행되는 Bluetooth 및 부저 제어 코드입니다.

- `bt_buzzer.ino`
- Raspberry Pi 서버와 Bluetooth 연결
- 물체 인식 신호 수신
- 인식 신호 수신 시 부저 작동
- 부저 음 높낮이,볼륨,지속시간 조절가능 함수 구현

---

## System Flow

```text
[Jetson]
   │
   │ Object Detection
   │
   ▼
[Raspberry Pi]
   │
   ├──► SQL Server
   │
   └──► Arduino (Bluetooth)
              │
              ▼
            Buzzer
