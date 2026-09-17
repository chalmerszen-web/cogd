"""Prepare and verify a context migration from a saved 4 MiB Flash image.

This tool never accesses USB. Stage files and the private event export belong in
an ignored artifacts directory. Flash bank A and the blank bank-B header first,
read them back and compare, then switch the partition table/application. Erase
the new clip region only after the new firmware has opened the migrated context.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

FLASH_SIZE = 0x400000
OLD_CTX, OLD_SIZE = 0x2A0000, 0x100000
NEW_CTX, NEW_SIZE = 0x190000, 0x200000
BANK_MAGIC, RECORD_MAGIC, COMMIT = 0x314B4241, 0x31525741, 0x54494D43


def sha(data):
    return hashlib.sha256(data).hexdigest()


def u32(data, offset=0):
    return struct.unpack_from("<I", data, offset)[0]


def partitions(table):
    result, end, md5 = {}, 0, False
    for offset in range(0, len(table) - 31, 32):
        entry = table[offset:offset + 32]
        magic = struct.unpack_from("<H", entry)[0]
        if magic == 0xEBEB:
            if entry[16:] != hashlib.md5(table[:offset]).digest():
                raise ValueError("Partition table MD5 mismatch")
            md5 = True
            break
        if magic != 0x50AA:
            raise ValueError("Invalid partition entry")
        _, kind, sub, at, size, name, flags = struct.unpack("<HBBII16sI", entry)
        name = name.rstrip(b"\0").decode("ascii")
        if not name or name in result or at < end or at + size > FLASH_SIZE:
            raise ValueError("Invalid partition geometry")
        result[name] = {"type": kind, "subtype": sub, "offset": at, "size": size, "flags": flags}
        end = at + size
    if not md5:
        raise ValueError("Partition table digest is required")
    return result


def application_size(data):
    if len(data) < 24 or data[0] != 0xE9 or not 1 <= data[1] <= 16:
        raise ValueError("Invalid ESP application header")
    offset, checksum = 24, 0xEF
    for _ in range(data[1]):
        if offset + 8 > len(data):
            raise ValueError("Truncated ESP segment header")
        count = u32(data, offset + 4)
        offset += 8
        if count > len(data) - offset:
            raise ValueError("Truncated ESP segment")
        for byte in data[offset:offset + count]:
            checksum ^= byte
        offset += count
    offset = (offset + 16) & ~15
    if offset > len(data) or data[offset - 1] != checksum:
        raise ValueError("ESP application checksum mismatch")
    if data[23] != 1 or offset + 32 > len(data) or hashlib.sha256(data[:offset]).digest() != data[offset:offset + 32]:
        raise ValueError("ESP application SHA-256 mismatch")
    return offset + 32


def bank_header(data):
    if len(data) < 64:
        return None
    if u32(data) != BANK_MAGIC or u32(data, 4) != 1 or u32(data, 60) != COMMIT:
        return None
    if u32(data, 24) != zlib.crc32(data[:24]):
        return None
    return struct.unpack_from("<QQ", data, 8)


def record(data, offset):
    if offset + 32 > len(data):
        return None
    h = data[offset:offset + 32]
    magic, size, seq, kind, crc, hcrc, commit = struct.unpack("<IIQIIII", h)
    if magic != RECORD_MAGIC or commit != COMMIT or hcrc != zlib.crc32(h[:24]):
        return None
    if not 0 < size <= 24576 or kind not in (1, 2) or offset + 32 + ((size + 3) & ~3) > len(data):
        return None
    body = data[offset + 32:offset + 32 + size]
    if zlib.crc32(body) != crc:
        return None
    return {"seq": seq, "kind": kind, "length": size, "offset": offset, "body": body,
            "end": offset + 32 + ((size + 3) & ~3)}


def scan_context(data):
    if len(data) % 8192:
        raise ValueError("Invalid context geometry")
    bank_size = len(data) // 2
    headers = [(i, bank_header(data[i * bank_size:(i + 1) * bank_size])) for i in range(2)]
    valid = [(i, h) for i, h in headers if h]
    if not valid:
        raise ValueError("No committed context bank")
    index, (generation, high) = max(valid, key=lambda item: (item[1][0], -item[0]))
    bank = data[index * bank_size:(index + 1) * bank_size]
    records, seen, offset = [], set(), 64
    while offset + 32 <= bank_size:
        item = record(bank, offset)
        if item is None:
            for later in range(offset + 4, bank_size - 31, 4):
                if u32(bank, later) == RECORD_MAGIC and record(bank, later):
                    raise ValueError("Middle corruption; migration refused")
            break
        # Compaction writes a new snapshot BEFORE retained older event frames.
        # Physical order need not be monotonic; identities must remain unique.
        if item["seq"] in seen or not item["seq"]:
            raise ValueError("Duplicate or zero WAL sequence")
        seen.add(item["seq"])
        json.loads(item["body"])
        records.append(item)
        offset = item["end"]
    return {"index": index, "generation": generation, "high": high, "used": offset, "records": records,
            "bank": bank, "tail_recovered": any(byte != 0xFF for byte in bank[offset:]),
            "next_seq": max([high] + [r["seq"] for r in records]) + 1}


def prepare(image):
    if len(image) != FLASH_SIZE:
        raise ValueError("Expected an exact 4 MiB backup")
    table = partitions(image[0x8000:0x9000])
    expected = {"nvs": (0x9000, 0x6000), "phy_init": (0xF000, 0x1000),
                "factory": (0x10000, 0x290000), "ctx": (OLD_CTX, OLD_SIZE)}
    if set(table) != set(expected) or any((table[k]["offset"], table[k]["size"]) != v for k, v in expected.items()):
        raise ValueError("Backup does not match the supported M0/M1 layout")
    app_size = application_size(image[0x10000:OLD_CTX])
    if app_size > NEW_CTX - 0x10000:
        raise ValueError("Staging would overwrite the running application")
    old = scan_context(image[OLD_CTX:OLD_CTX + OLD_SIZE])
    stage = bytearray(b"\xFF" * (NEW_SIZE // 2))
    stage[:old["used"]] = old["bank"][:old["used"]]
    struct.pack_into("<Q", stage, 8, old["generation"] + 1)
    struct.pack_into("<I", stage, 24, zlib.crc32(stage[:24]))
    new = scan_context(stage + b"\xFF" * (NEW_SIZE // 2))
    identity = lambda s: [(r["seq"], r["kind"], r["body"]) for r in s["records"]]
    if identity(new) != identity(old) or new["next_seq"] != old["next_seq"]:
        raise ValueError("Migration changed record content or sequence")
    events = [json.loads(r["body"]) for r in old["records"] if r["kind"] == 1]
    event_ids = [event["event_id"] for event in events]
    if len(set(event_ids)) != len(event_ids):
        raise ValueError("Duplicate event IDs in retained log")
    manifest = {"backup_sha256": sha(image), "old_layout": table, "old_app_size": app_size,
                "old_bank": old["index"], "old_generation": old["generation"], "new_generation": new["generation"],
                "used": old["used"], "records": len(old["records"]), "events": len(events),
                "next_wal_seq": old["next_seq"], "tail_recovered": old["tail_recovered"],
                "event_ids_sha256": sha("\n".join(event_ids).encode()),
                "records_sha256": sha(b"".join(r["body"] for r in old["records"])),
                "nvs_sha256": sha(image[0x9000:0xF000]), "stage_address": NEW_CTX,
                "stage_sha256": sha(stage), "blank_header_address": NEW_CTX + NEW_SIZE // 2,
                "clip_address": 0x390000, "clip_size": 0x70000}
    return bytes(stage), events, manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("backup", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit("Output directory exists; refusing to overwrite migration evidence")
    stage, events, manifest = prepare(args.backup.read_bytes())
    args.output.mkdir(parents=True)
    (args.output / "ctx-stage.bin").write_bytes(stage)
    (args.output / "bank-b-header.bin").write_bytes(b"\xFF" * 4096)
    (args.output / "clip-blank.bin").write_bytes(b"\xFF" * manifest["clip_size"])
    (args.output / "events-private.jsonl").write_text("".join(json.dumps(e, ensure_ascii=False) + "\n" for e in events), encoding="utf-8")
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
