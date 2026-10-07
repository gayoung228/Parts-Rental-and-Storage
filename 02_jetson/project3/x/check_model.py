import tflite_runtime.interpreter as tflite

interpreter = tflite.Interpreter(model_path="model.tflite")
interpreter.allocate_tensors()

input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()

print("=== INPUT ===")
for x in input_details:
    print(x)

print("\n=== OUTPUT ===")
for x in output_details:
    print(x)

