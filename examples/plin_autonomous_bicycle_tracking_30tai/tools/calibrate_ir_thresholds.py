import argparse
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageOps, ImageStat

from preview_plin_network_frames import find_ir_candidates


def clamp_byte(value: float) -> int:
    return max(0, min(255, int(round(value))))


def masked_mean(image: Image.Image, mask: Image.Image) -> float:
    return float(ImageStat.Stat(image, mask=mask).mean[0])


def analyze_ir_pair(
    off_image: Image.Image,
    on_image: Image.Image,
) -> tuple[dict, Image.Image]:
    off_rgb = off_image.convert("RGB")
    on_rgb = on_image.convert("RGB")
    if off_rgb.size != on_rgb.size:
        raise ValueError("off/on frames must have the same dimensions")

    off_red, off_green, off_blue = off_rgb.split()
    on_red, on_green, on_blue = on_rgb.split()
    delta_red = ImageChops.subtract(on_red, off_red)
    delta_green = ImageChops.subtract(on_green, off_green)
    delta_blue = ImageChops.subtract(on_blue, off_blue)
    delta_competing = ImageChops.lighter(delta_green, delta_blue)
    red_delta_advantage = ImageChops.subtract(delta_red, delta_competing)

    on_competing = ImageChops.lighter(on_green, on_blue)
    on_dominance = ImageChops.subtract(on_red, on_competing)
    delta_background = delta_red.filter(ImageFilter.GaussianBlur(radius=5))
    delta_local = ImageChops.subtract(delta_red, delta_background)

    delta_mask = delta_red.point(lambda value: 255 if value >= 6 else 0)
    local_mask = delta_local.point(lambda value: 255 if value >= 3 else 0)
    color_mask = red_delta_advantage.point(lambda value: 255 if value >= 2 else 0)
    candidate_mask = ImageChops.multiply(
        ImageChops.multiply(delta_mask, local_mask),
        color_mask,
    )

    magenta_overlay = ImageChops.multiply(
        ImageChops.multiply(
            on_red.point(lambda value: 255 if value >= 140 else 0),
            on_blue.point(lambda value: 255 if value >= 90 else 0),
        ),
        on_green.point(lambda value: 255 if value <= 140 else 0),
    ).filter(ImageFilter.MaxFilter(11))
    yellow_overlay = ImageChops.multiply(
        ImageChops.multiply(
            on_red.point(lambda value: 255 if value >= 170 else 0),
            on_green.point(lambda value: 255 if value >= 130 else 0),
        ),
        on_blue.point(lambda value: 255 if value <= 100 else 0),
    ).filter(ImageFilter.MaxFilter(11))
    overlay_mask = ImageChops.lighter(magenta_overlay, yellow_overlay)
    candidate_mask = ImageChops.multiply(
        candidate_mask,
        ImageOps.invert(overlay_mask),
    )
    ImageDraw.Draw(candidate_mask).rectangle((0, 0, on_rgb.width, 105), fill=0)

    candidates = find_ir_candidates(
        candidate_mask,
        min_area=2,
        max_area=160,
        max_span=24,
    )
    if not candidates:
        raise RuntimeError(
            "no compact red-light change was found; keep the scene still and "
            "capture the off/on frames again"
        )

    def candidate_score(box: tuple[int, int, int, int, int]) -> float:
        left, top, right, bottom, area = box
        crop = (left, top, right, bottom)
        return (
            delta_red.crop(crop).getextrema()[1] * 4.0
            + red_delta_advantage.crop(crop).getextrema()[1] * 2.0
            + on_dominance.crop(crop).getextrema()[1]
            + min(area, 30) * 0.5
        )

    selected = max(candidates, key=candidate_score)
    left, top, right, bottom, area = selected
    crop_box = (left, top, right, bottom)
    component_mask = candidate_mask.crop(crop_box)

    red_mean = masked_mean(on_red.crop(crop_box), component_mask)
    green_mean = masked_mean(on_green.crop(crop_box), component_mask)
    blue_mean = masked_mean(on_blue.crop(crop_box), component_mask)
    competing_mean = masked_mean(on_competing.crop(crop_box), component_mask)
    dominance_mean = masked_mean(on_dominance.crop(crop_box), component_mask)
    local_mean = masked_mean(delta_local.crop(crop_box), component_mask)
    delta_red_mean = masked_mean(delta_red.crop(crop_box), component_mask)

    recommendation = {
        "ir_red_min": clamp_byte(red_mean - max(8.0, delta_red_mean * 0.35)),
        "ir_red_dominance": clamp_byte(max(5.0, dominance_mean - 8.0)),
        "ir_reflection_max": clamp_byte(competing_mean + 12.0),
        "ir_local_contrast": clamp_byte(max(3.0, local_mean * 0.45)),
    }
    report = {
        "point": {
            "x": round((left + right - 1) / 2.0, 1),
            "y": round((top + bottom - 1) / 2.0, 1),
            "bounds": [left, top, right, bottom],
            "area": area,
        },
        "on_rgb_mean": {
            "r": round(red_mean, 1),
            "g": round(green_mean, 1),
            "b": round(blue_mean, 1),
        },
        "red_dominance_mean": round(dominance_mean, 1),
        "competing_channel_mean": round(competing_mean, 1),
        "red_delta_mean": round(delta_red_mean, 1),
        "local_delta_mean": round(local_mean, 1),
        "recommendation": recommendation,
    }

    preview = ImageOps.grayscale(on_rgb).convert("RGB")
    preview_pixels = preview.load()
    on_pixels = on_rgb.load()
    mask_pixels = candidate_mask.load()
    for y in range(top, bottom):
        for x in range(left, right):
            if mask_pixels[x, y]:
                red_value = on_pixels[x, y][0]
                preview_pixels[x, y] = (red_value, 0, 0)
    draw = ImageDraw.Draw(preview)
    padding = 7
    draw.ellipse(
        (left - padding, top - padding, right + padding, bottom + padding),
        outline=(255, 0, 0),
        width=2,
    )
    return report, preview


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--off", required=True, type=Path)
    parser.add_argument("--on", required=True, type=Path)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--preview-out", type=Path)
    args = parser.parse_args()

    report, preview = analyze_ir_pair(Image.open(args.off), Image.open(args.on))
    output = json.dumps(report, ensure_ascii=False, indent=2)
    print(output)
    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(output + "\n", encoding="utf-8")
    if args.preview_out is not None:
        args.preview_out.parent.mkdir(parents=True, exist_ok=True)
        preview.save(args.preview_out, quality=92)


if __name__ == "__main__":
    main()
