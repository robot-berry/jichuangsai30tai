import argparse
import json
import html
import os
import shlex
import struct
import subprocess
import sys
import time
from collections import deque
from pathlib import Path
from typing import Optional

from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFilter, ImageOps


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
        r = ((value >> 11) & 0x1F) * 255 // 31
        g = ((value >> 5) & 0x3F) * 255 // 63
        b = (value & 0x1F) * 255 // 31
        out[j:j + 3] = bytes((r, g, b))
        j += 3
    return Image.frombytes("RGB", (width, height), bytes(out))


def rgb565_to_image(data: bytes, width: int, height: int) -> Image.Image:
    try:
        return Image.frombytes("RGB", (width, height), data, "raw", "BGR;16", 0, 1)
    except ValueError:
        return slow_rgb565_to_image(data, width, height)


def find_ir_candidates(
    mask: Image.Image,
    min_area: int = 2,
    max_area: int = 220,
    max_span: int = 36,
) -> list[tuple[int, int, int, int, int]]:
    width, height = mask.size
    active = bytearray(mask.tobytes())
    candidates = []
    for start, value in enumerate(active):
        if not value:
            continue
        active[start] = 0
        pending = deque([start])
        area = 0
        min_x = max_x = start % width
        min_y = max_y = start // width
        while pending:
            index = pending.popleft()
            x = index % width
            y = index // width
            area += 1
            min_x = min(min_x, x)
            max_x = max(max_x, x)
            min_y = min(min_y, y)
            max_y = max(max_y, y)
            for ny in range(max(0, y - 1), min(height, y + 2)):
                row = ny * width
                for nx in range(max(0, x - 1), min(width, x + 2)):
                    neighbor = row + nx
                    if active[neighbor]:
                        active[neighbor] = 0
                        pending.append(neighbor)

        box_width = max_x - min_x + 1
        box_height = max_y - min_y + 1
        if area < min_area or area > max_area:
            continue
        if box_width > max_span or box_height > max_span:
            continue
        aspect = max(box_width, box_height) / max(1, min(box_width, box_height))
        fill = area / max(1, box_width * box_height)
        if aspect > 3.5 or fill < 0.12:
            continue
        candidates.append((min_x, min_y, max_x + 1, max_y + 1, area))
    return candidates


def make_ir_candidate_view(
    image: Image.Image,
    gain: float,
    held_candidate: Optional[tuple[int, int, int, int, int]] = None,
    red_min: int = 180,
    red_dominance_min: int = 50,
    reflection_max: int = 200,
    local_contrast_min: int = 8,
) -> tuple[Image.Image, int, int, Optional[tuple[int, int, int, int, int]]]:
    rgb = image.convert("RGB")
    red, green, blue = rgb.split()
    competing_color = ImageChops.lighter(green, blue)
    dominance = ImageChops.subtract(red, competing_color)
    peak = dominance.getextrema()[1]
    local_background = dominance.filter(ImageFilter.GaussianBlur(radius=5))
    local_peak = ImageChops.subtract(dominance, local_background)
    red_mask = red.point(lambda value: 255 if value >= red_min else 0)
    dominance_mask = dominance.point(
        lambda value: 255 if value >= red_dominance_min else 0
    )
    reflection_mask = competing_color.point(
        lambda value: 255 if value <= reflection_max else 0
    )
    contrast_mask = local_peak.point(
        lambda value: 255 if value >= local_contrast_min else 0
    )
    candidate_mask = ImageChops.multiply(
        ImageChops.multiply(
            ImageChops.multiply(red_mask, dominance_mask),
            reflection_mask,
        ),
        contrast_mask,
    )
    magenta_overlay = ImageChops.multiply(
        ImageChops.multiply(
            red.point(lambda value: 255 if value >= 140 else 0),
            blue.point(lambda value: 255 if value >= 90 else 0),
        ),
        green.point(lambda value: 255 if value <= 140 else 0),
    ).filter(ImageFilter.MaxFilter(11))
    candidate_mask = ImageChops.multiply(
        candidate_mask,
        ImageOps.invert(magenta_overlay),
    )
    ImageDraw.Draw(candidate_mask).rectangle((0, 0, rgb.width, 105), fill=0)
    candidates = find_ir_candidates(candidate_mask, max_area=80, max_span=18)
    detected_count = len(candidates)
    if candidates:
        candidates = [
            max(
                candidates,
                key=lambda box: (
                    local_peak.crop((box[0], box[1], box[2], box[3])).getextrema()[1] * 3
                    + dominance.crop((box[0], box[1], box[2], box[3])).getextrema()[1]
                ),
            )
        ]
    elif held_candidate is not None:
        candidates = [held_candidate]

    filtered_mask = Image.new("L", rgb.size, 0)
    filtered_pixels = filtered_mask.load()
    source_pixels = candidate_mask.load()
    for left, top, right, bottom, _area in candidates:
        if detected_count == 0:
            ImageDraw.Draw(filtered_mask).rectangle((left, top, right, bottom), fill=255)
        else:
            for y in range(top, bottom):
                for x in range(left, right):
                    if source_pixels[x, y]:
                        filtered_pixels[x, y] = 255

    scale = max(1.0, float(gain))
    boosted = local_peak.point(lambda value: min(255, int(value * scale * 6.0)))
    boosted = ImageChops.multiply(boosted, filtered_mask)
    heat = ImageOps.colorize(
        boosted,
        black=(0, 0, 0),
        white=(255, 0, 0),
        mid=(160, 0, 0),
    )
    context = ImageEnhance.Brightness(ImageOps.grayscale(rgb).convert("RGB")).enhance(0.68)
    output = ImageChops.screen(context, heat)
    draw = ImageDraw.Draw(output)
    for left, top, right, bottom, _area in candidates:
        padding = 5
        draw.ellipse(
            (left - padding, top - padding, right + padding, bottom + padding),
            outline=(255, 0, 0),
            width=2,
        )
    selected = candidates[0] if candidates else None
    return output, peak, detected_count, selected


def make_ir_difference_view(
    reference: Image.Image,
    image: Image.Image,
    gain: float,
    held_candidate: Optional[tuple[int, int, int, int, int]] = None,
) -> tuple[Image.Image, int, int, Optional[tuple[int, int, int, int, int]]]:
    rgb = image.convert("RGB")
    reference_rgb = reference.convert("RGB")
    if reference_rgb.size != rgb.size:
        reference_rgb = reference_rgb.resize(rgb.size, Image.Resampling.BILINEAR)

    reference_red, reference_green, reference_blue = reference_rgb.split()
    red, green, blue = rgb.split()
    delta_red = ImageChops.subtract(red, reference_red)
    delta_green = ImageChops.subtract(green, reference_green)
    delta_blue = ImageChops.subtract(blue, reference_blue)
    red_delta_advantage = ImageChops.subtract(
        delta_red,
        ImageChops.lighter(delta_green, delta_blue),
    )
    delta_background = delta_red.filter(ImageFilter.GaussianBlur(radius=5))
    local_delta = ImageChops.subtract(delta_red, delta_background)
    peak = delta_red.getextrema()[1]

    candidate_mask = ImageChops.multiply(
        ImageChops.multiply(
            delta_red.point(lambda value: 255 if value >= 6 else 0),
            local_delta.point(lambda value: 255 if value >= 3 else 0),
        ),
        red_delta_advantage.point(lambda value: 255 if value >= 6 else 0),
    )
    reference_brightness = ImageChops.lighter(
        reference_red,
        ImageChops.lighter(reference_green, reference_blue),
    )
    candidate_mask = ImageChops.multiply(
        candidate_mask,
        reference_brightness.point(lambda value: 255 if value >= 180 else 0),
    )
    current_red_dominance = ImageChops.subtract(red, green)
    candidate_mask = ImageChops.multiply(
        candidate_mask,
        current_red_dominance.point(lambda value: 255 if value >= 5 else 0),
    )
    magenta_raw = ImageChops.multiply(
        ImageChops.multiply(
            red.point(lambda value: 255 if value >= 150 else 0),
            blue.point(lambda value: 255 if value >= 140 else 0),
        ),
        green.point(lambda value: 255 if value <= 90 else 0),
    )
    magenta_overlay = magenta_raw.filter(ImageFilter.MaxFilter(11))
    magenta_edges = magenta_raw.filter(ImageFilter.MaxFilter(5))
    label_overlay = Image.new("L", rgb.size, 0)
    label_draw = ImageDraw.Draw(label_overlay)
    edge_pixels = magenta_edges.load()
    for y in range(105, rgb.height):
        xs = [x for x in range(rgb.width) if edge_pixels[x, y]]
        if len(xs) < 12:
            continue
        group_start = 0
        for index in range(1, len(xs) + 1):
            if index < len(xs) and xs[index] - xs[index - 1] <= 12:
                continue
            group = xs[group_start:index]
            if len(group) >= 12 and group[-1] - group[0] >= 40:
                label_draw.rectangle(
                    (group[0] - 80, y - 70, group[-1] + 80, y + 8),
                    fill=255,
                )
            group_start = index
    yellow_overlay = ImageChops.multiply(
        ImageChops.multiply(
            red.point(lambda value: 255 if value >= 170 else 0),
            green.point(lambda value: 255 if value >= 130 else 0),
        ),
        blue.point(lambda value: 255 if value <= 100 else 0),
    ).filter(ImageFilter.MaxFilter(11))
    candidate_mask = ImageChops.multiply(
        candidate_mask,
        ImageOps.invert(
            ImageChops.lighter(
                ImageChops.lighter(magenta_overlay, yellow_overlay),
                label_overlay,
            )
        ),
    )
    ImageDraw.Draw(candidate_mask).rectangle((0, 0, rgb.width, 105), fill=0)

    candidates = find_ir_candidates(
        candidate_mask,
        min_area=30,
        max_area=160,
        max_span=24,
    )
    detected_count = len(candidates)
    if candidates:
        candidates = [
            max(
                candidates,
                key=lambda box: (
                    delta_red.crop((box[0], box[1], box[2], box[3])).getextrema()[1] * 4
                    + red_delta_advantage.crop(
                        (box[0], box[1], box[2], box[3])
                    ).getextrema()[1] * 2
                    + min(box[4], 30) * 0.5
                ),
            )
        ]
    elif held_candidate is not None:
        candidates = [held_candidate]

    filtered_mask = Image.new("L", rgb.size, 0)
    filtered_pixels = filtered_mask.load()
    source_pixels = candidate_mask.load()
    for left, top, right, bottom, _area in candidates:
        if detected_count == 0:
            ImageDraw.Draw(filtered_mask).rectangle((left, top, right, bottom), fill=255)
        else:
            for y in range(top, bottom):
                for x in range(left, right):
                    if source_pixels[x, y]:
                        filtered_pixels[x, y] = 255

    scale = max(1.0, float(gain))
    boosted = delta_red.point(lambda value: min(255, int(value * scale * 3.0)))
    boosted = ImageChops.multiply(boosted, filtered_mask)
    heat = ImageOps.colorize(
        boosted,
        black=(0, 0, 0),
        white=(255, 0, 0),
        mid=(160, 0, 0),
    )
    context = ImageEnhance.Brightness(ImageOps.grayscale(rgb).convert("RGB")).enhance(0.68)
    output = ImageChops.screen(context, heat)
    draw = ImageDraw.Draw(output)
    for left, top, right, bottom, _area in candidates:
        padding = 5
        draw.ellipse(
            (left - padding, top - padding, right + padding, bottom + padding),
            outline=(255, 0, 0),
            width=2,
        )
    selected = candidates[0] if candidates else None
    return output, peak, detected_count, selected


def write_preview_page(out_dir: Path, ir_gain: float, difference_mode: bool) -> Path:
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
    .views { display: grid; grid-template-columns: 1fr 1fr; gap: 1px; height: calc(100vh - 42px); background: #38414c; }
    .view { position: relative; min-width: 0; min-height: 0; background: #050607; }
    .view img { display: block; width: 100%; height: 100%; object-fit: contain; }
    .label { position: absolute; z-index: 1; top: 10px; left: 10px; padding: 5px 8px; background: rgba(5, 6, 7, 0.72); color: #fff; font-size: 13px; }
    @media (max-width: 900px) {
      .views { grid-template-columns: 1fr; grid-template-rows: 1fr 1fr; }
    }
  </style>
</head>
<body>
  <header id="status">waiting for frames...</header>
  <main class="views">
    <section class="view">
      <span class="label">检测画面</span>
      <img id="frame" alt="live detection frame">
    </section>
    <section class="view">
      <span class="label">IR_VIEW_LABEL（增益 IR_GAIN）</span>
      <img id="enhanced" alt="infrared point candidate frame">
    </section>
  </main>
  <script>
    const img = document.getElementById("frame");
    const enhancedImg = document.getElementById("enhanced");
    const statusBox = document.getElementById("status");
    let refreshCount = 0;
    let lastFrame = "";

    function refresh() {
      fetch("live_manifest.json?t=" + Date.now(), {cache: "no-store"})
        .then(response => response.json())
        .then(state => {
          if (state.frame && state.enhanced && state.frame !== lastFrame) {
            const stamp = Date.now();
            const next = new Image();
            const nextEnhanced = new Image();
            let loaded = 0;
            const showPair = () => {
              loaded += 1;
              if (loaded !== 2) return;
              img.src = next.src;
              enhancedImg.src = nextEnhanced.src;
              lastFrame = state.frame;
              refreshCount += 1;
              statusBox.textContent =
                "LIVE  frame: " + state.count +
                "  red peak: " + state.red_peak +
                "  IR: " + state.ir_state +
                "  received: " + state.received_at +
                "  displayed: " + new Date().toLocaleTimeString();
            };
            next.onload = showPair;
            nextEnhanced.onload = showPair;
            next.src = state.frame + "?t=" + stamp;
            nextEnhanced.src = state.enhanced + "?t=" + stamp;
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
""".replace("IR_GAIN", f"{ir_gain:g}x").replace(
            "IR_VIEW_LABEL",
            "红外关闭/打开差分" if difference_mode else "弱红外点候选",
        ),
        encoding="utf-8",
    )
    return page


def publish_frame(
    image: Image.Image,
    enhanced: Image.Image,
    red_peak: int,
    ir_candidates: int,
    ir_state: str,
    out_dir: Path,
    frame_count: int,
    quality: int,
) -> Path:
    frame = out_dir / f"live_frame_{frame_count:08d}.jpg"
    enhanced_frame = out_dir / f"live_enhanced_{frame_count:08d}.jpg"
    image.save(frame, format="JPEG", quality=quality)
    enhanced.save(enhanced_frame, format="JPEG", quality=quality)

    manifest = out_dir / "live_manifest.json"
    manifest_tmp = out_dir / f"live_manifest_{os.getpid()}.tmp"
    payload = {
        "frame": frame.name,
        "enhanced": enhanced_frame.name,
        "red_peak": red_peak,
        "ir_candidates": ir_candidates,
        "ir_state": ir_state,
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
    parser.add_argument("--ir-gain", type=float, default=4.0)
    parser.add_argument("--ir-red-min", type=int, default=180)
    parser.add_argument("--ir-red-dominance", type=int, default=50)
    parser.add_argument("--ir-reflection-max", type=int, default=200)
    parser.add_argument("--ir-local-contrast", type=int, default=8)
    parser.add_argument("--ir-reference", type=Path)
    parser.add_argument("--window", action="store_true")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    ir_reference = None
    if args.ir_reference is not None:
        ir_reference = Image.open(args.ir_reference).convert("RGB")
    page = write_preview_page(args.out_dir, args.ir_gain, ir_reference is not None)
    status = args.out_dir / "live_status.txt"
    for stale in args.out_dir.glob("live_frame_*.jpg"):
        try:
            stale.unlink()
        except OSError:
            pass
    for stale in args.out_dir.glob("live_enhanced_*.jpg"):
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
    held_ir_candidate = None
    held_ir_frames = 0
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
                    magic, frame_id, ts_ns, width, height, data_len = HEADER.unpack(header)
                    if magic != MAGIC:
                        raise RuntimeError(f"bad stream header: {magic!r}")
                    data = read_exact(proc.stdout, data_len)

                    image = rgb565_to_image(data, width, height)
                    display = image.copy()
                    display.thumbnail(
                        (args.preview_width, max(1, int(args.preview_width * height / width))),
                        Image.Resampling.BILINEAR,
                    )
                    if ir_reference is not None:
                        enhanced, red_peak, ir_candidates, selected_ir_candidate = (
                            make_ir_difference_view(
                                ir_reference,
                                display,
                                args.ir_gain,
                                held_ir_candidate if held_ir_frames > 0 else None,
                            )
                        )
                    else:
                        enhanced, red_peak, ir_candidates, selected_ir_candidate = (
                            make_ir_candidate_view(
                                display,
                                args.ir_gain,
                                held_ir_candidate if held_ir_frames > 0 else None,
                                red_min=max(0, min(255, args.ir_red_min)),
                                red_dominance_min=max(0, min(255, args.ir_red_dominance)),
                                reflection_max=max(0, min(255, args.ir_reflection_max)),
                                local_contrast_min=max(0, min(255, args.ir_local_contrast)),
                            )
                        )
                    if ir_candidates > 0:
                        held_ir_candidate = selected_ir_candidate
                        held_ir_frames = 6
                        ir_state = "DETECTED"
                    elif selected_ir_candidate is not None and held_ir_frames > 0:
                        held_ir_frames -= 1
                        ir_state = "HELD"
                    else:
                        held_ir_candidate = None
                        held_ir_frames = 0
                        ir_state = "NONE"
                    frame_count += 1
                    latest = publish_frame(
                        display,
                        enhanced,
                        red_peak,
                        ir_candidates,
                        ir_state,
                        args.out_dir,
                        frame_count,
                        args.quality,
                    )
                    old_frame = args.out_dir / f"live_frame_{frame_count - 90:08d}.jpg"
                    old_enhanced = args.out_dir / f"live_enhanced_{frame_count - 90:08d}.jpg"
                    if frame_count > 90:
                        for old_image in (old_frame, old_enhanced):
                            try:
                                old_image.unlink()
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
            f"frames: {frame_count}   finished: {time.strftime('%H:%M:%S')}   page: {html.escape(str(page))}",
            encoding="utf-8",
        )


if __name__ == "__main__":
    main()
