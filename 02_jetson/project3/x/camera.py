import cv2
import time

cap = cv2.VideoCapture(0)

if not cap.isOpened():
    print("Camera open failed")
    exit()

print("Camera started")

count = 0
start = time.time()

try:
    while True:
        ret, frame = cap.read()

        if not ret:
            print("Camera read failed")
            break

        count += 1

        elapsed = time.time() - start

        if elapsed >= 1.0:
            print(f"Camera FPS: {count / elapsed:.1f}")
            count = 0
            start = time.time()

except KeyboardInterrupt:
    print("\nStopped")

finally:
    cap.release()

