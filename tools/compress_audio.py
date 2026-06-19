#!/usr/bin/env python3
"""Compress MP3 audio assets with xz for storage in git."""

from __future__ import annotations

import argparse
import lzma
from pathlib import Path

AUDIO_SUFFIXES = {".mp3"}


def is_audio(path: Path) -> bool:
    return path.suffix.lower() in AUDIO_SUFFIXES


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


def collect_audio(paths: list[Path]) -> list[Path]:
    audio_files: list[Path] = []
    for root in paths:
        if root.is_file():
            if is_audio(root):
                audio_files.append(root)
            continue

        for candidate in sorted(root.rglob("*")):
            if candidate.is_file() and is_audio(candidate):
                audio_files.append(candidate)

    return audio_files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "paths",
        nargs="*",
        default=["resources/audio"],
        help="Files or directories to scan for MP3 audio (default: resources/audio)",
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
        help="Delete uncompressed MP3 files after writing .xz files",
    )
    args = parser.parse_args()

    roots = [Path(path) for path in args.paths]
    audio_files = collect_audio(roots)
    if not audio_files:
        print("No MP3 audio files found.")
        return 0

    total_original = 0
    total_compressed = 0

    for audio_path in audio_files:
        original_size, compressed_size = compress_file(
            audio_path,
            preset=args.preset,
            remove_source=args.remove_source,
        )
        total_original += original_size
        total_compressed += compressed_size
        ratio = (compressed_size / original_size) * 100 if original_size else 0.0
        print(
            f"{audio_path} -> {audio_path}.xz "
            f"({original_size} -> {compressed_size} bytes, {ratio:.1f}%)"
        )

    saved = total_original - total_compressed
    print(
        f"Compressed {len(audio_files)} audio file(s): "
        f"{total_original} -> {total_compressed} bytes "
        f"({saved} bytes saved)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())