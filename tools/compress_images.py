#!/usr/bin/env python3
"""Compress image assets with xz for storage in git."""

from __future__ import annotations

import argparse
import lzma
from pathlib import Path

IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".webp"}
JPEG_SUFFIXES = {".jpg", ".jpeg"}


def is_image(path: Path) -> bool:
    return path.suffix.lower() in IMAGE_SUFFIXES


def convert_jpeg_to_png(path: Path) -> Path:
    try:
        from PIL import Image as PilImage
    except ImportError:
        import subprocess

        png_path = path.with_suffix(".png")
        subprocess.run(
            ["sips", "-s", "format", "png", str(path), "--out", str(png_path)],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        return png_path

    png_path = path.with_suffix(".png")
    with PilImage.open(path) as image:
        image.save(png_path, format="PNG")
    return png_path


def prepare_image(path: Path, convert_jpeg: bool) -> tuple[Path, list[Path]]:
    cleanup: list[Path] = []
    if convert_jpeg and path.suffix.lower() in JPEG_SUFFIXES:
        png_path = convert_jpeg_to_png(path)
        cleanup.append(path)
        path = png_path
    return path, cleanup


def compress_file(path: Path, preset: int, remove_source: bool) -> tuple[int, int]:
    output_path = Path(f"{path}.xz")
    original_size = path.stat().st_size

    with path.open("rb") as source:
        data = source.read()

    compressed = lzma.compress(data, preset=preset)
    output_path.write_bytes(compressed)

    if remove_source:
        path.unlink()

    return original_size, len(compressed)


def collect_images(paths: list[Path]) -> list[Path]:
    images: list[Path] = []
    for root in paths:
        if root.is_file():
            if is_image(root):
                images.append(root)
            continue

        for candidate in sorted(root.rglob("*")):
            if candidate.is_file() and is_image(candidate):
                images.append(candidate)

    return images


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "paths",
        nargs="*",
        default=["resources"],
        help="Files or directories to scan for images (default: resources)",
    )
    parser.add_argument(
        "--preset",
        type=int,
        default=9,
        help="xz compression preset (0-9, default: 9)",
    )
    parser.add_argument(
        "--remove-source",
        action="store_true",
        help="Delete uncompressed images after writing .xz files",
    )
    parser.add_argument(
        "--convert-jpeg",
        action="store_true",
        default=True,
        help="Convert JPEG inputs to PNG before compression (default: enabled)",
    )
    parser.add_argument(
        "--no-convert-jpeg",
        dest="convert_jpeg",
        action="store_false",
        help="Keep JPEG files as-is when compressing",
    )
    args = parser.parse_args()

    roots = [Path(path) for path in args.paths]
    images = collect_images(roots)
    if not images:
        print("No images found.")
        return 0

    total_original = 0
    total_compressed = 0

    for image_path in images:
        prepared_path, cleanup_paths = prepare_image(image_path, args.convert_jpeg)
        original_size, compressed_size = compress_file(
            prepared_path,
            preset=args.preset,
            remove_source=args.remove_source,
        )
        if args.remove_source:
            for cleanup_path in cleanup_paths:
                if cleanup_path.exists():
                    cleanup_path.unlink()
                compressed_original = Path(f"{cleanup_path}.xz")
                if compressed_original.exists():
                    compressed_original.unlink()

        total_original += original_size
        total_compressed += compressed_size
        ratio = (compressed_size / original_size) * 100 if original_size else 0.0
        print(
            f"{prepared_path} -> {prepared_path}.xz "
            f"({original_size} -> {compressed_size} bytes, {ratio:.1f}%)"
        )

    saved = total_original - total_compressed
    print(
        f"Compressed {len(images)} image(s): "
        f"{total_original} -> {total_compressed} bytes "
        f"({saved} bytes saved)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())