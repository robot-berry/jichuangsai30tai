import argparse
import re
import struct
import sys
import time
from pathlib import Path
from typing import Tuple


WIDTH = 1920
HEIGHT = 1080
FRAME_SIZE = WIDTH * HEIGHT * 2
MAGIC = b"PLINRGB5"
HEADER = struct.Struct("<8sIQHHI")
UDMA_DIR = Path("/sys/devices/platform/udmabuf@0/u-dma-buf/udmabuf0")


def read_base_phys() -> int:
    return int((UDMA_DIR / "phys_addr").read_text().strip(), 16)


def read_hdmi_addr(log_path: Path, wait_seconds: float) -> Tuple[int, int]:
    deadline = time.monotonic() + wait_seconds
    pattern = re.compile(r"RGB565HDMIDisplay buffer udma addr:\s*(\d+), size:\s*(\d+)")

    while time.monotonic() < deadline:
        text = log_path.read_text(errors="ignore") if log_path.exists() else ""
        matches = pattern.findall(text)
        if matches:
            addr, size = matches[-1]
            return int(addr), int(size)
        time.sleep(0.1)

    raise RuntimeError(f"HDMI buffer address was not found in {log_path}")


def sync_for_cpu(offset: int) -> None:
    try:
        (UDMA_DIR / "sync_offset").write_text(hex(offset))
        (UDMA_DIR / "sync_size").write_text(str(FRAME_SIZE))
        (UDMA_DIR / "sync_for_cpu").write_text("1")
    except OSError:
        pass


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--frames", type=int, default=0, help="0 means stream until stopped")
    parser.add_argument("--seconds", type=float, default=0.0, help="0 means no time limit")
    parser.add_argument("--wait-seconds", type=float, default=20.0)
    parser.add_argument("--interval", type=float, default=0.5)
    args = parser.parse_args()

    base = read_base_phys()
    addr, size = read_hdmi_addr(args.log, args.wait_seconds)
    if size != FRAME_SIZE:
        print(f"warning: expected {FRAME_SIZE} bytes, log reports {size}", file=sys.stderr)

    offset = addr - base
    if offset < 0:
        raise RuntimeError(f"invalid HDMI offset: addr=0x{addr:x}, base=0x{base:x}")

    deadline = time.monotonic() + args.seconds if args.seconds > 0 else None
    frame_id = 0

    with open("/dev/udmabuf0", "rb", buffering=0) as dev:
        while args.frames <= 0 or frame_id < args.frames:
            if deadline is not None and time.monotonic() >= deadline:
                break

            sync_for_cpu(offset)
            time.sleep(max(args.interval, 0.0))
            dev.seek(offset)
            data = dev.read(FRAME_SIZE)
            if len(data) != FRAME_SIZE:
                print(f"short frame: {len(data)} bytes", file=sys.stderr)
                break

            frame_id += 1
            sys.stdout.buffer.write(
                HEADER.pack(MAGIC, frame_id, time.monotonic_ns(), WIDTH, HEIGHT, len(data))
            )
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()


if __name__ == "__main__":
    main()
