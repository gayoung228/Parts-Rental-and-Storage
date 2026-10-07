import cv2
import numpy as np
import tflite_runtime.interpreter as tflite


MODEL_PATH = "model.tflite"
LABEL_PATH = "labels.txt"


# -----------------------------
# Load labels
# -----------------------------
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


# -----------------------------
# Load TFLite model
# -----------------------------
interpreter = tflite.Interpreter(model_path=MODEL_PATH)
interpreter.allocate_tensors()

input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()

input_shape = input_details[0]["shape"]

print("Input shape:", input_shape)
print("Input dtype:", input_details[0]["dtype"])
print("Model loaded")


# -----------------------------
# Open USB camera
# -----------------------------
cap = cv2.VideoCapture(0)

if not cap.isOpened():
    print("Camera open failed")
    exit()

print("Camera started")
print("Press Ctrl+C to stop")


# -----------------------------
# Main loop
# -----------------------------
try:
    while True:

        ret, frame = cap.read()

        if not ret:
            print("Camera read failed")
            break

        # BGR -> RGB
        image = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

        # 224 x 224
        image = cv2.resize(image, (224, 224))

        # float32
        image = image.astype(np.float32)

        # Teachable Machine float model
        # 0~255 -> -1~1
        image = (image / 127.5) - 1.0

        # Add batch dimension
        image = np.expand_dims(image, axis=0)

        # Set input tensor
        interpreter.set_tensor(
            input_details[0]["index"],
            image
        )

        # Inference
        interpreter.invoke()

        # Get output
        output = interpreter.get_tensor(
            output_details[0]["index"]
        )

        probabilities = output[0]

        # Highest probability
        class_index = np.argmax(probabilities)
        confidence = probabilities[class_index]

        label = labels[class_index]
        if(confidence > 0.9):
            print(
                f"\r{label}: {confidence * 100:.1f}%",
                end="",
                flush=True
            )


except KeyboardInterrupt:
    print("\nStopped")


finally:
    cap.release()

