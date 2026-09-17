"""Song USB transfer and immutable readback, shared by acoustic and soak tests."""
import json
import re
import zlib


def upload(device, song):
    data = json.dumps(song, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    if len(data) > 4096:
        raise ValueError("Song exceeds 4096 bytes")
    device.run(f"agent audio song begin {len(data)} {zlib.crc32(data):08x}")
    try:
        for offset in range(0, len(data), 256):
            device.run(f"agent audio song chunk {offset} {data[offset:offset+256].hex()}")
        return device.run("agent audio song commit")
    except BaseException:
        try:
            device.run("agent audio song abort")
        except (AssertionError, TimeoutError):
            pass
        raise


def read_song(device):
    before = device.get("audio status")["last_song_job"]
    reply = device.run("agent audio song get")
    text = next(line for line in reply.splitlines() if line.startswith('{"v":'))
    match = re.search(r"@song (\d+) ([0-9a-f]{8})", reply)
    data = text.encode("utf-8")
    assert match and int(match[1]) == len(data) and int(match[2], 16) == zlib.crc32(data), reply
    assert device.get("audio status")["last_song_job"] == before
    return json.loads(text)


def main():
    import argparse
    from pathlib import Path
    from hardware_m2 import Device
    from serial_link import connect
    p=argparse.ArgumentParser(description='Validate/upload a saved song to the device; no network call or recording.')
    p.add_argument('song',type=Path)
    p.add_argument('--port',default='COM5')
    args=p.parse_args()
    score=json.loads(args.song.read_text(encoding='utf-8'))
    with connect(args.port) as link:
        d=Device(link,Path('artifacts/logs/music-upload.json'))
        upload(d,score)
        assert read_song(d)==score
        print('Song accepted. Use agent audio stop or agent cancel to stop.')


if __name__=='__main__':
    main()
