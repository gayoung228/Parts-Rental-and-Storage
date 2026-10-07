#include <SoftwareSerial.h>

#define BT_RX 10
#define BT_TX 11
#define BUZZER_PIN 9

#define CMD_SIZE 60
#define ARR_CNT 5

SoftwareSerial BTSerial(BT_RX, BT_TX);

void buzzer(int frequency, int volume, int duration)
{
  // frequency: Hz (예: 1000, 2000, 4000)
  // volume: 0~100 (%)
  // duration: ms

  if (volume <= 0) {
    digitalWrite(BUZZER_PIN, LOW);
    delay(duration);
    return;
  }

  if (volume > 100) volume = 100;

  // Timer1 설정
  pinMode(BUZZER_PIN, OUTPUT);

  TCCR1A = 0;
  TCCR1B = 0;

  // Fast PWM, TOP = ICR1
  TCCR1A |= (1 << COM1A1);
  TCCR1A |= (1 << WGM11);
  TCCR1B |= (1 << WGM13) | (1 << WGM12);
  TCCR1B |= (1 << CS10);  // 분주 1

  // 주파수 설정
  ICR1 = (16000000UL / frequency) - 1;

  // 듀티비 = 볼륨
  OCR1A = ((ICR1 + 1) * volume) / 100;

  delay(duration);

  // PWM OFF
  TCCR1A = 0;
  TCCR1B = 0;
  digitalWrite(BUZZER_PIN, LOW);
}


void setup()
{
  Serial.begin(115200);
  BTSerial.begin(9600);

  pinMode(BUZZER_PIN, OUTPUT);
}

void loop()
{
  if (BTSerial.available())
    bluetoothEvent();
}

void bluetoothEvent()
{
  int i = 0;
  char *pToken;
  char *pArray[ARR_CNT] = {0};
  char recvBuf[CMD_SIZE] = {0};

  int len = BTSerial.readBytesUntil('\n', recvBuf, sizeof(recvBuf) - 1);
  recvBuf[len] = '\0';

  // 받은 데이터 확인
  Serial.print("Recv : ");
  Serial.println(recvBuf);

  // [LJW_LIN]BUZ
  pToken = strtok(recvBuf, "[@]");

  while (pToken != NULL)
  {
    pArray[i] = pToken;

    if (++i >= ARR_CNT)
      break;

    pToken = strtok(NULL, "[@]");
  }

  // pArray[0] = LJW_LIN
  // pArray[1] = BUZ

  if (pArray[1] != NULL && !strcmp(pArray[1], "BUZ"))
  {
    // tone(BUZZER_PIN, 2000);  // 2kHz
    // delay(100);              // 100ms
    // noTone(BUZZER_PIN);
    buzzer(3000, 70, 70);
    //      ↑     ↑   ↑
    //     톤    볼륨 시간
    //    2kHz   50% 100ms
    // buzzer(1000, 30, 100);   // 낮은 음 + 작은 소리
    // buzzer(2000, 50, 100);   // 2kHz + 중간
    // buzzer(4000, 100, 100);  // 높은 음 + 최대
    Serial.println("buz!");


  }
}
