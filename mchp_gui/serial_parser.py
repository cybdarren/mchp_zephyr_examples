import threading
import queue
import serial
import serial.tools.list_ports


def list_serial_ports():
    return [p.device for p in serial.tools.list_ports.comports()]


def validate_checksum(line):
    """Validate $MCHP,...*XX checksum. Returns (tag, [floats]) or None."""
    if not line.startswith("$") or "*" not in line:
        return None

    body, _, ckstr = line.partition("*")
    ckstr = ckstr.strip()
    if len(ckstr) != 2:
        return None

    try:
        expected = int(ckstr, 16)
    except ValueError:
        return None

    computed = 0
    for ch in body[1:]:
        computed ^= ord(ch)

    if computed != expected:
        return None

    parts = body[1:].split(",")
    if len(parts) < 2 or parts[0] != "MCHP":
        return None

    tag = parts[1]
    try:
        values = [float(v) for v in parts[2:]]
    except ValueError:
        return None

    return (tag, values)


class SerialReader:
    def __init__(self, port, baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.queue = queue.Queue(maxsize=500)
        self._serial = None
        self._thread = None
        self._running = False

    def start(self):
        self._serial = serial.Serial(self.port, self.baudrate, timeout=0.1)
        self._running = True
        self._thread = threading.Thread(target=self._read_loop, daemon=True)
        self._thread.start()

    def stop(self):
        self._running = False
        if self._thread:
            self._thread.join(timeout=2)
        if self._serial and self._serial.is_open:
            self._serial.close()
        self._serial = None

    def _read_loop(self):
        while self._running:
            try:
                raw = self._serial.readline()
                if not raw:
                    continue
                line = raw.decode("ascii", errors="ignore").strip()
                result = validate_checksum(line)
                if result:
                    try:
                        self.queue.put_nowait(result)
                    except queue.Full:
                        try:
                            self.queue.get_nowait()
                        except queue.Empty:
                            pass
                        self.queue.put_nowait(result)
            except (serial.SerialException, OSError):
                self._running = False
                break
