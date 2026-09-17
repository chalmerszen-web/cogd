"""Small USB transport shared by provisioning and hardware test scripts."""
import time
import serial
from detect_port import detect_port


class USBReplyTimeout(TimeoutError):
    """Retain diagnostic bytes without putting potentially secret data in errors."""
    def __init__(self, received):
        self.received = bytes(received)
        super().__init__(f"Device reply timed out ({len(self.received)} bytes received)")


def connect(port=None):
    link = serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=3)
    link.dtr = False
    link.rts = False
    link.port = detect_port(port)
    link.open()
    return link


def receive_until(link, marker, timeout=10):
    deadline = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < deadline:
        data.extend(link.read(4096))
        markers = marker if isinstance(marker, tuple) else (marker,)
        if any(item in data for item in markers):
            return bytes(data)
        if len(data) > 128 * 1024:
            raise RuntimeError("USB reply exceeded limit")
    raise USBReplyTimeout(data)
