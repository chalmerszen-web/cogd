"""Compile current component methods against the bounded host policy fixture.

No implementation is copied into the fixture. A missing/renamed method fails
the test setup rather than silently testing a stale copy.
"""
from pathlib import Path
import re
import sys


def extract(source: Path, mode: str = "raw") -> str:
    text = source.read_text(encoding="utf-8")
    names = (
        "OpenRawStream", "CanSendRawPacket", "WriteRawStream", "FinishRawStream",
        "AcknowledgeRawStreamData", "ReceiveRawStreamFrame",
        "ProcessCertificate", "ProcessCertificateVerify", "ProcessServerFinished",
    )
    if mode == "ack":
        names = ("CanSendDatagram", "SendDatagram", "GetMaxDatagramSize", "IsDatagramAvailable", "OnFrameAck", "ProcessAckFrame")
        methods = []
    elif mode == "datagram":
        names = ("CanSendDatagram", "SendDatagram", "GetMaxDatagramSize", "IsDatagramAvailable")
        methods = []
    elif mode == "cid":
        names = ("StartHandshake", "GenerateLocalConnectionId", "ProcessReceivedData")
        methods = []
    else:
        begin = text.index("static bool ConstantTimeEqual(")
        end = text.index("\n}", begin) + 2
        methods = [text[begin:end]]
    for name in names:
        matches = list(re.finditer(
            r"^(?:bool|void|int64_t|ssize_t|size_t) QuicConnection::Impl::" + name + r"\(",
            text, re.MULTILINE,
        ))
        if len(matches) != 1:
            raise ValueError(f"Expected exactly one current component method: {name}")
        begin = matches[0].start()
        end = text.index("\n}", begin) + 2
        methods.append(text[begin:end].replace("QuicConnection::Impl::", "TestConnection::"))
    return "\n\n".join(methods) + "\n"


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4) or (len(sys.argv) == 4 and sys.argv[3] not in ("cid", "datagram", "ack")):
        raise SystemExit("usage: extract_methods.py COMPONENT_SOURCE OUTPUT_INC [cid|datagram|ack]")
    Path(sys.argv[2]).write_text(extract(Path(sys.argv[1]), sys.argv[3] if len(sys.argv) == 4 else "raw"), encoding="utf-8")
