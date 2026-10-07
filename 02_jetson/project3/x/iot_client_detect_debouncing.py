import socket
import threading
import time
import re
import sys

import cv2
import numpy as np
import tflite_runtime.interpreter as tflite


# ============================================================
# Server Settings
# ============================================================

HOST = "10.10.16.84"
PORT = 5000
ADDR = (HOST, PORT)


# ============================================================
# Detection Settings
# ============================================================

MODEL_PATH = "model.tflite"
LABEL_PATH = "labels.txt"

CONFIDENCE_THRESHOLD = 0.9

# ------------------------------------------------------------
# 중복 인식 방지 설정
# ------------------------------------------------------------

# 몇 프레임 연속으로 NULL이어야
# 실제로 물체가 사라졌다고 판단할지
NULL_FRAME_THRESHOLD = 3

# 마지막 SEND 후 다시 SEND하기까지 최소 시간
# 단위: 초
SEND_COOLDOWN = 2.0


# 현재 감지 상태
current_state = "NULL"

# 이전 감지 상태
previous_state = "NULL"

# 연속 NULL 프레임 수
null_frame_count = 0

# 마지막 SEND 시간
last_send_time = 0


# ============================================================
# Socket
# ============================================================

recvFlag = False
rsplit = []

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

try:
    s.connect(ADDR)

    print("connect is success")

    # --------------------------------------------------------
    # Send Thread
    # --------------------------------------------------------

    def sendingMsg():
        try:
            # 서버 접속 후 비밀번호 전송
            s.send('[LJW_JET:PASSWD]'.encode())

            time.sleep(0.5)

            while True:
                data = input()

                data = bytes(data + '\n', "utf-8")

                s.send(data)

        except Exception as e:
            print("Sending error:", e)

    # --------------------------------------------------------
    # Receive Thread
    # --------------------------------------------------------

    def gettingMsg():
        global rsplit
        global recvFlag

        try:
            while True:
                data = s.recv(1024)

                if not data:
                    print("Server disconnected")
                    break

                rstr = data.decode("utf-8")

                rsplit = re.split(
                    r'[\]|\[@]|\n',
                    rstr
                )

                recvFlag = True

        except Exception as e:
            print("Receiving error:", e)


    # Thread Start
    threading._start_new_thread(sendingMsg, ())
    threading._start_new_thread(gettingMsg, ())


except Exception as e:
    print("%s:%s" % ADDR)
    print("Connection error:", e)
    sys.exit()


# ============================================================
# Load Labels
# ============================================================

labels = []

with open(LABEL_PATH, "r") as f:
    for line in f:
        line = line.strip()

        if not line:
            continue

        # "0 Class 1" -> "Class 1"
        parts = line.split(" ", 1)

        if len(parts) == 2:
            labels.append(parts[1])
        else:
            labels.append(parts[0])


print("Labels:", labels)


# ============================================================
# Load TFLite Model
# ============================================================

interpreter = tflite.Interpreter(
    model_path=MODEL_PATH
)

interpreter.allocate_tensors()

input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()

input_shape = input_details[0]["shape"]

print("Input shape:", input_shape)
print("Input dtype:", input_details[0]["dtype"])
print("Model loaded")


# ============================================================
# Open USB Camera
# ============================================================

cap = cv2.VideoCapture(0)

if not cap.isOpened():
    print("Camera open failed")
    sys.exit()

print("Camera started")
print("Press Ctrl+C to stop")


# ============================================================
# Detection -> Server
# ============================================================

def send_detection_message(label, confidence):
    """
    감지 상태가 NULL -> 특정 상태로 바뀌었을 때
    서버로 메시지를 전송하는 함수.
    """

    message = f"[LJW_SQL]SETDB@OBJ@{label}@LJW_JET\n"
    message_beep = "[LJW_ARD]BUZ\n"

    try:
        s.send(message.encode("utf-8"))
        s.send(message_beep.encode("utf-8"))

        print(
            f"\n[SEND] {label}: {confidence * 100:.1f}%"
        )

    except Exception as e:
        print("\nSend detection error:", e)


# ============================================================
# Main Detection Loop
# ============================================================

try:

    while True:

        ret, frame = cap.read()

        if not ret:
            print("Camera read failed")
            break

        # ----------------------------------------------------
        # BGR -> RGB
        # ----------------------------------------------------

        image = cv2.cvtColor(
            frame,
            cv2.COLOR_BGR2RGB
        )

        # ----------------------------------------------------
        # Resize 224 x 224
        # ----------------------------------------------------

        image = cv2.resize(
            image,
            (224, 224)
        )

        # ----------------------------------------------------
        # float32
        # ----------------------------------------------------

        image = image.astype(np.float32)

        # ----------------------------------------------------
        # Teachable Machine float model
        # 0~255 -> -1~1
        # ----------------------------------------------------

        image = (image / 127.5) - 1.0

        # ----------------------------------------------------
        # Add batch dimension
        # ----------------------------------------------------

        image = np.expand_dims(
            image,
            axis=0
        )

        # ----------------------------------------------------
        # Set input
        # ----------------------------------------------------

        interpreter.set_tensor(
            input_details[0]["index"],
            image
        )

        # ----------------------------------------------------
        # Inference
        # ----------------------------------------------------

        interpreter.invoke()

        # ----------------------------------------------------
        # Get output
        # ----------------------------------------------------

        output = interpreter.get_tensor(
            output_details[0]["index"]
        )

        probabilities = output[0]

        # ----------------------------------------------------
        # Highest probability
        # ----------------------------------------------------

        class_index = np.argmax(probabilities)

        confidence = probabilities[class_index]

        label = labels[class_index]


        # ====================================================
        # Detection State
        # ====================================================

        if confidence > CONFIDENCE_THRESHOLD:

            # 물체가 확실하게 감지됨
            current_state = label

            # NULL 카운터 초기화
            null_frame_count = 0

        else:

            # NULL 프레임 증가
            null_frame_count += 1

            # 일정 프레임 이상 NULL이어야
            # 실제 NULL 상태로 변경
            if null_frame_count >= NULL_FRAME_THRESHOLD:

                current_state = "NULL"


        # ====================================================
        # Display Detection
        # ====================================================

        if confidence > CONFIDENCE_THRESHOLD:

            print(
                f"\r{label}: {confidence * 100:.1f}%",
                end="",
                flush=True
            )

        else:

            print(
                f"\rNULL ({null_frame_count}/{NULL_FRAME_THRESHOLD})",
                end="",
                flush=True
            )


        # ====================================================
        # NULL -> Something
        # ====================================================

        if (
            previous_state == "NULL"
            and current_state != "NULL"
            and time.time() - last_send_time >= SEND_COOLDOWN
        ):

            send_detection_message(
                current_state,
                confidence
            )

            # 마지막 SEND 시간 저장
            last_send_time = time.time()


        # ----------------------------------------------------
        # Save current state
        # ----------------------------------------------------

        previous_state = current_state


except KeyboardInterrupt:

    print("\nStopped")


finally:

    cap.release()

    try:
        s.close()
    except:
        pass
