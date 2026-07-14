import argparse
import json
import os
import shlex
import struct
import subprocess
import time
from pathlib import Path
from typing import Optional

from PIL import Image


MAGIC = b"PLINRGB5"
HEADER = struct.Struct("<8sIQHHI")


def read_exact(stream, size: int) -> bytes:
    chunks = []
    remaining = size
    while remaining:
        chunk = stream.read(remaining)
        if not chunk:
            raise EOFError("stream ended before a full frame arrived")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def slow_rgb565_to_image(data: bytes, width: int, height: int) -> Image.Image:
    out = bytearray(width * height * 3)
    j = 0
    for i in range(0, width * height * 2, 2):
        value = data[i] | (data[i + 1] << 8)
        red = ((value >> 11) & 0x1F) * 255 // 31
        green = ((value >> 5) & 0x3F) * 255 // 63
        blue = (value & 0x1F) * 255 // 31
        out[j:j + 3] = bytes((red, green, blue))
        j += 3
    return Image.frombytes("RGB", (width, height), bytes(out))


def rgb565_to_image(data: bytes, width: int, height: int) -> Image.Image:
    try:
        return Image.frombytes("RGB", (width, height), data, "raw", "BGR;16", 0, 1)
    except ValueError:
        return slow_rgb565_to_image(data, width, height)


def write_preview_page(out_dir: Path) -> Path:
    page = out_dir / "live_preview.html"
    page.write_text(
        """<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>PLin Live Preview</title>
  <style>
    * { box-sizing: border-box; }
    body { margin: 0; height: 100vh; overflow: hidden; background: #111418; color: #e8eef6; font-family: Segoe UI, Arial, sans-serif; }
    header { height: 42px; padding: 0 14px; display: flex; align-items: center; background: #1b222b; font-size: 14px; white-space: nowrap; overflow: hidden; }
    main { height: calc(100vh - 42px); background: #050607; }
    img { display: block; width: 100%; height: 100%; object-fit: contain; }
  </style>
</head>
<body>
  <header id="status">waiting for frames...</header>
  <main><img id="frame" alt="live detection and tracking frame"></main>
  <script>
    const img = document.getElementById("frame");
    const statusBox = document.getElementById("status");
    let lastFrame = "";

    function refresh() {
      fetch("live_manifest.json?t=" + Date.now(), {cache: "no-store"})
        .then(response => response.json())
        .then(state => {
          if (state.frame && state.frame !== lastFrame) {
            const next = new Image();
            next.onload = () => {
              img.src = next.src;
              lastFrame = state.frame;
              statusBox.textContent =
                "LIVE  frame: " + state.count +
                "  received: " + state.received_at +
                "  displayed: " + new Date().toLocaleTimeString();
            };
            next.src = state.frame + "?t=" + Date.now();
          }
        })
        .catch(() => {
          statusBox.textContent = "waiting for the next frame...";
        })
        .finally(() => setTimeout(refresh, 250));
    }

    refresh();
  </script>
</body>
</html>
""",
        encoding="utf-8",
    )
    return page


def publish_frame(
    image: Image.Image,
    out_dir: Path,
    frame_count: int,
    quality: int,
) -> Path:
    frame = out_dir / f"live_frame_{frame_count:08d}.jpg"
    image.save(frame, format="JPEG", quality=quality)

    manifest = out_dir / "live_manifest.json"
    manifest_tmp = out_dir / f"live_manifest_{os.getpid()}.tmp"
    payload = {
        "frame": frame.name,
        "count": frame_count,
        "received_at": time.strftime("%H:%M:%S"),
    }
    manifest_tmp.write_text(json.dumps(payload), encoding="utf-8")
    for attempt in range(10):
        try:
            os.replace(manifest_tmp, manifest)
            break
        except PermissionError:
            if attempt == 9:
                raise
            time.sleep(0.02)
    return frame


def make_window(enabled: bool):
    if not enabled:
        return None, None
    try:
        import tkinter as tk
        from PIL import ImageTk
    except Exception:
        return None, None

    root = tk.Tk()
    root.title("PLin network preview")
    label = tk.Label(root, bg="#050607")
    label.pack(fill="both", expand=True)
    root.geometry("960x570")
    return root, (label, ImageTk)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ssh", default="ssh")
    parser.add_argument("--target", required=True)
    parser.add_argument("--remote-dir", required=True)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("--frames", type=int, default=0)
    parser.add_argument("--interval", type=float, default=0.6)
    parser.add_argument("--preview-width", type=int, default=960)
    parser.add_argument("--quality", type=int, default=80)
    parser.add_argument("--window", action="store_true")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    page = write_preview_page(args.out_dir)
    status = args.out_dir / "live_status.txt"
    for pattern in ("live_frame_*.jpg", "live_enhanced_*.jpg"):
        for stale in args.out_dir.glob(pattern):
            try:
                stale.unlink()
            except OSError:
                pass

    def make_ssh_args(stream_seconds: float, stream_frames: int):
        remote_cmd = (
            f"cd {shlex.quote(args.remote_dir)} && "
            "python3 tools/stream_plin_hdmi_udma.py "
            "--log logs/plin_live.log "
            f"--frames {stream_frames} "
            f"--seconds {stream_seconds:.3f} "
            f"--interval {float(args.interval):.3f}"
        )
        return [
            args.ssh,
            "-o",
            "ConnectTimeout=10",
            "-o",
            "StrictHostKeyChecking=no",
            "-o",
            "UserKnownHostsFile=/dev/null",
            "-o",
            "PreferredAuthentications=password",
            "-o",
            "PubkeyAuthentication=no",
            args.target,
            remote_cmd,
        ]

    root, window = make_window(args.window)
    last_photo: Optional[object] = None
    start = time.time()
    deadline = start + max(0.1, args.seconds)
    frame_count = 0
    stderr_chunks = []
    last_stream_error = ""
    try:
        while time.time() < deadline and (args.frames <= 0 or frame_count < args.frames):
            remaining_seconds = max(0.1, deadline - time.time())
            remaining_frames = max(0, args.frames - frame_count) if args.frames > 0 else 0
            proc = subprocess.Popen(
                make_ssh_args(remaining_seconds, remaining_frames),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            assert proc.stdout is not None

            try:
                while time.time() < deadline and (args.frames <= 0 or frame_count < args.frames):
                    header = read_exact(proc.stdout, HEADER.size)
                    magic, frame_id, _ts_ns, width, height, data_len = HEADER.unpack(header)
                    if magic != MAGIC:
                        raise RuntimeError(f"bad stream header: {magic!r}")
                    data = read_exact(proc.stdout, data_len)

                    display = rgb565_to_image(data, width, height)
                    display.thumbnail(
                        (args.preview_width, max(1, int(args.preview_width * height / width))),
                        Image.Resampling.BILINEAR,
                    )
                    frame_count += 1
                    latest = publish_frame(display, args.out_dir, frame_count, args.quality)
                    if frame_count > 90:
                        old_frame = args.out_dir / f"live_frame_{frame_count - 90:08d}.jpg"
                        try:
                            old_frame.unlink()
                        except OSError:
                            pass

                    elapsed = max(time.time() - start, 0.001)
                    status.write_text(
                        f"frames: {frame_count}   fps: {frame_count / elapsed:.2f}   "
                        f"latest: {time.strftime('%H:%M:%S')}",
                        encoding="utf-8",
                    )
                    print(f"frame {frame_id} -> {latest.name}", flush=True)

                    if root is not None and window is not None:
                        label, image_tk = window
                        last_photo = image_tk.PhotoImage(display)
                        label.configure(image=last_photo)
                        root.update_idletasks()
                        root.update()
            except (EOFError, OSError, RuntimeError) as exc:
                last_stream_error = str(exc)
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    try:
                        proc.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                stderr = proc.stderr.read().decode("utf-8", errors="ignore") if proc.stderr else ""
                if stderr.strip():
                    stderr_chunks.append(stderr.strip())

            if time.time() < deadline and (args.frames <= 0 or frame_count < args.frames):
                status.write_text(
                    f"frames: {frame_count}   reconnecting: {time.strftime('%H:%M:%S')}",
                    encoding="utf-8",
                )
                time.sleep(min(1.0, max(0.0, deadline - time.time())))
    finally:
        diagnostic = "\n\n".join(stderr_chunks)
        if last_stream_error:
            diagnostic = f"{diagnostic}\n\n{last_stream_error}".strip()
        if diagnostic:
            (args.out_dir / "live_preview_stderr.txt").write_text(diagnostic, encoding="utf-8")
        status.write_text(
            f"frames: {frame_count}   finished: {time.strftime('%H:%M:%S')}   page: {page}",
            encoding="utf-8",
        )


if __name__ == "__main__":
    main()
