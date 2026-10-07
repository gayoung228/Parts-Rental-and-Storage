"""Jetson camera classifier. Server/DB/MCU code remains C; detections are not rentals."""
import argparse
import math
from dataclasses import dataclass
from pathlib import Path
import secrets
import socket
import threading
import time


@dataclass(frozen=True)
class Session:
    uid: str
    identifier: str


class Debouncer:
    def __init__(self, threshold=0.9, stable_frames=3, null_frames=3, cooldown=2.0):
        self.threshold = threshold
        self.stable_frames = stable_frames
        self.null_frames = null_frames
        self.cooldown = cooldown
        self.reset()

    def reset(self):
        self.candidate = None
        self.count = self.empty_count = 0
        self.armed = True
        self.last_sent = float('-inf')

    def update(self, label, confidence, now):
        if label == 'NULL' or not math.isfinite(confidence) or confidence < self.threshold:
            self.empty_count += 1
            self.candidate = None
            self.count = 0
            if self.empty_count >= self.null_frames:
                self.armed = True
            return None
        self.empty_count = 0
        if label == self.candidate:
            self.count += 1
        else:
            self.candidate, self.count = label, 1
        if self.armed and self.count >= self.stable_frames and now - self.last_sent >= self.cooldown:
            return label
        return None

    def sent(self, now):
        self.armed = False
        self.last_sent = now


class CameraConnection:
    def __init__(self, host, port, locker):
        self.host, self.port, self.locker = host, port, locker
        self.camera_id = 'CAMERA_' + locker
        if len(self.camera_id) > 31 or not all(c.isascii() and (c.isalnum() or c == '_') for c in locker):
            raise ValueError('Locker ID must fit CAMERA_<locker> within 31 ASCII characters')
        self.lock = threading.Lock()
        self.session = None
        self.pending = None
        self.stopping = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)

    def start(self):
        self.thread.start()

    def close(self):
        self.stopping.set()
        self.thread.join(timeout=5)

    def current_session(self):
        with self.lock:
            return self.session

    def submit(self, session, label, confidence):
        if not math.isfinite(confidence) or confidence < 0.9 or confidence > 1.00001 or \
                label == 'NULL' or not label or len(label) > 31 or not all(
                c.isascii() and (c.isalnum() or c == '_') for c in label):
            return False
        with self.lock:
            if self.session != session or self.pending is not None:
                return False
            event = secrets.token_hex(8).upper()
            score = min(1000, max(900, round(confidence * 1000)))
            line = f'[{self.camera_id}]DETECT@{session.identifier}@{label}@{score}@{event}\n'
            self.pending = dict(session=session, event=event, line=line, attempts=0, sent_at=0.0)
        print(f'[DETECT] queued label={label} confidence={confidence:.3f} uid={session.uid}', flush=True)
        return True

    def receive_line(self, line):
        if not line.startswith('[SERVER]'):
            return
        fields = line[8:].split('@')
        if len(fields) == 5 and fields[0] == 'SESSION' and fields[1] == self.locker:
            value = Session(fields[2], fields[3]) if fields[4] == 'OPEN' else None
            if value and (len(value.uid) not in (8, 14, 20) or len(value.identifier) != 16 or
                          any(c not in '0123456789ABCDEF' for c in value.uid + value.identifier)):
                return
            with self.lock:
                changed = self.session != value
                self.session = value
                if self.pending and self.pending['session'] != value:
                    print('[DETECT] old session ended; pending event discarded', flush=True)
                    self.pending = None
            if changed:
                print(f'[SESSION] {value if value else "CLOSED - waiting for approved card"}', flush=True)
        elif len(fields) == 3 and fields[0] == 'DETECT':
            with self.lock:
                if self.pending and self.pending['event'] == fields[1] and fields[2] in (
                        'SAVED', 'RENTED', 'RETURNED', 'BUSY', 'UNCHANGED', 'REJECTED', 'ERROR'):
                    print(f'[DETECT] event={fields[1]} result={fields[2]}', flush=True)
                    self.pending = None

    def run(self):
        while not self.stopping.is_set():
            try:
                with socket.create_connection((self.host, self.port), timeout=3) as connection:
                    connection.settimeout(0.2)
                    connection.sendall(f'[{self.camera_id}]HELLO\n'.encode('ascii'))
                    print('[NETWORK] connected to relay', flush=True)
                    buffer = b''
                    while not self.stopping.is_set():
                        with self.lock:
                            pending = self.pending
                            current = self.session
                        if pending and current == pending['session']:
                            now = time.monotonic()
                            if now - pending['sent_at'] >= 3:
                                if pending['attempts'] >= 3:
                                    print('[DETECT] no receipt after retries; retake tool after removing it', flush=True)
                                    with self.lock:
                                        if self.pending is pending:
                                            self.pending = None
                                else:
                                    connection.sendall(pending['line'].encode('ascii'))
                                    pending['attempts'] += 1
                                    pending['sent_at'] = now
                        try:
                            data = connection.recv(1024)
                        except socket.timeout:
                            continue
                        if not data:
                            raise ConnectionError('Relay closed connection')
                        buffer += data
                        if len(buffer) > 4096:
                            raise ValueError('Oversized relay input')
                        while b'\n' in buffer:
                            raw, buffer = buffer.split(b'\n', 1)
                            if len(raw) > 255:
                                raise ValueError('Oversized relay line')
                            self.receive_line(raw.rstrip(b'\r').decode('ascii'))
            except (OSError, ValueError) as error:
                print(f'[NETWORK] {error}; retry in 1 second', flush=True)
            with self.lock:
                self.session = None
            self.stopping.wait(1)


def load_labels(path):
    labels = []
    for line in Path(path).read_text(encoding='utf-8-sig').splitlines():
        parts = line.strip().split(maxsplit=1)
        if not parts:
            continue
        labels.append(parts[1] if len(parts) == 2 and parts[0].isdigit() else line.strip())
    if not labels or 'NULL' not in labels:
        raise ValueError('labels.txt must include a NULL background class')
    for label in labels:
        if len(label) > 31 or not all(c.isascii() and (c.isalnum() or c == '_') for c in label):
            raise ValueError('Use ASCII tool codes without spaces in labels.txt')
    return labels


class Classifier:
    def __init__(self, model, labels):
        import numpy as np
        try:
            from ai_edge_litert.interpreter import Interpreter
        except ImportError:
            try:
                from tflite_runtime.interpreter import Interpreter
            except ImportError:
                from tensorflow.lite.python.interpreter import Interpreter
        self.np = np
        self.labels = labels
        self.interpreter = Interpreter(model_content=Path(model).read_bytes(), num_threads=2)
        self.interpreter.allocate_tensors()
        inputs, outputs = self.interpreter.get_input_details(), self.interpreter.get_output_details()
        if len(inputs) != 1 or len(outputs) != 1:
            raise ValueError('Expected one image input and one classification output')
        self.input, self.output = inputs[0], outputs[0]
        shape = list(self.input['shape'])
        if len(shape) != 4 or shape[0] != 1 or shape[3] != 3 or self.input['dtype'] != np.float32:
            raise ValueError('This Teachable Machine client expects float32 [1,height,width,3] input')
        if list(self.output['shape']) != [1, len(labels)] or self.output['dtype'] != np.float32:
            raise ValueError('Output class count or dtype does not match labels.txt')
        self.height, self.width = int(shape[1]), int(shape[2])
        print(f'[MODEL] input={shape} output={list(self.output["shape"])} labels={labels}', flush=True)

    def predict(self, frame):
        import cv2
        np = self.np
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        rgb = cv2.resize(rgb, (self.width, self.height))
        image = np.expand_dims(rgb.astype(np.float32) / 127.5 - 1.0, 0)
        self.interpreter.set_tensor(self.input['index'], image)
        self.interpreter.invoke()
        probabilities = self.interpreter.get_tensor(self.output['index'])[0]
        if not np.all(np.isfinite(probabilities)):
            raise ValueError('Model produced non-finite scores')
        index = int(np.argmax(probabilities))
        return self.labels[index], float(probabilities[index])


def main():
    root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', help='Current Raspberry Pi relay IP')
    parser.add_argument('--port', type=int, default=5000)
    parser.add_argument('--locker', default='LOCKER_101')
    parser.add_argument('--model', default=str(root / 'model.tflite'))
    parser.add_argument('--labels', default=str(root / 'labels.txt'))
    parser.add_argument('--camera', default='0')
    parser.add_argument('--preview', action='store_true')
    parser.add_argument('--inspect-model', action='store_true')
    parser.add_argument('--simulate-label', help='Protocol test only, bypasses camera/model')
    args = parser.parse_args()
    labels = load_labels(args.labels)
    if args.inspect_model:
        model = Classifier(args.model, labels)
        blank = model.np.zeros((model.height, model.width, 3), dtype=model.np.uint8)
        print('[MODEL] smoke inference:', model.predict(blank))
        return
    if not args.host:
        parser.error('--host is required')
    model = None if args.simulate_label else Classifier(args.model, labels)
    if args.simulate_label and (args.simulate_label not in labels or args.simulate_label == 'NULL'):
        parser.error('--simulate-label must be a known non-NULL code')
    connection = CameraConnection(args.host, args.port, args.locker)
    debouncer = Debouncer()
    cap = None
    if model:
        import cv2
        device = int(args.camera) if args.camera.isdecimal() else args.camera
        cap = cv2.VideoCapture(device)
        if not cap.isOpened():
            cap.release()
            raise RuntimeError('Cannot open USB camera; check --camera and permissions')
    connection.start()
    previous_session, log_tick = None, 0.0
    try:
        while True:
            if cap:
                ok, frame = cap.read()
                if not ok:
                    raise RuntimeError('Camera frame read failed')
                label, confidence = model.predict(frame)
                if args.preview:
                    cv2.imshow('Tool camera', frame)
                    if cv2.waitKey(1) & 0xFF == ord('q'):
                        break
            else:
                label, confidence = args.simulate_label, 0.95
                time.sleep(0.1)
            now = time.monotonic()
            session = connection.current_session()
            if session != previous_session:
                debouncer.reset()
                previous_session = session
            candidate = debouncer.update(label, confidence, now)
            if session and candidate and connection.submit(session, candidate, confidence):
                debouncer.sent(now)
            if now - log_tick >= 1:
                print(f'[CAMERA] {label} confidence={confidence:.3f} session={"OPEN" if session else "CLOSED"}', flush=True)
                log_tick = now
    except KeyboardInterrupt:
        print('\nStopped')
    finally:
        connection.close()
        if cap:
            cap.release()
            if args.preview:
                cv2.destroyAllWindows()


if __name__ == '__main__':
    main()
