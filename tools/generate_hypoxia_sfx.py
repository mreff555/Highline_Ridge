#!/usr/bin/env python3
"""Generate hypoxia fade-in/out sfx for overlay transitions."""

from __future__ import annotations

import math
import struct
import subprocess
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "resources" / "audio" / "sfx"
SAMPLE_RATE = 44100


def write_wav(path: Path, samples: list[float]) -> None:
    clipped = [max(-1.0, min(1.0, sample)) for sample in samples]
    pcm = b"".join(
        struct.pack("<h", int(sample * 32767.0))
        for sample in clipped
    )
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(SAMPLE_RATE)
        handle.writeframes(pcm)


def convert_to_mp3(wav_path: Path, mp3_path: Path) -> None:
    commands = [
        [
            "ffmpeg",
            "-y",
            "-loglevel",
            "error",
            "-i",
            str(wav_path),
            "-codec:a",
            "libmp3lame",
            "-qscale:a",
            "4",
            str(mp3_path),
        ],
        [
            "afconvert",
            "-f",
            "mp4f",
            "-d",
            "aac",
            str(wav_path),
            str(mp3_path),
        ],
    ]
    for command in commands:
        try:
            subprocess.run(command, check=True)
            return
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue
    raise RuntimeError("Need ffmpeg or afconvert to create MP3 output")


def generate_fade_in(duration: float = 4.5) -> list[float]:
    total_samples = int(duration * SAMPLE_RATE)
    samples: list[float] = []
    for index in range(total_samples):
        t = index / SAMPLE_RATE
        progress = index / max(1, total_samples - 1)

        heartbeat = math.sin(2.0 * math.pi * 1.35 * t) * math.exp(-1.8 * t)
        pulse = max(0.0, math.sin(2.0 * math.pi * 2.7 * t)) ** 6.0
        rumble = math.sin(2.0 * math.pi * 42.0 * t) * 0.18
        hiss = (((index * 1103515245 + 12345) & 0x7FFFFFFF) / 0x7FFFFFFF) - 0.5
        hiss *= 0.08 * progress

        sample = (heartbeat * 0.42 + pulse * 0.28 + rumble + hiss) * (0.25 + 0.75 * progress)
        samples.append(sample)
    return samples


def generate_fade_out(duration: float = 3.5) -> list[float]:
    total_samples = int(duration * SAMPLE_RATE)
    samples: list[float] = []
    for index in range(total_samples):
        t = index / SAMPLE_RATE
        progress = index / max(1, total_samples - 1)
        envelope = 1.0 - progress

        breath = math.sin(2.0 * math.pi * 0.55 * t) * math.exp(-1.2 * progress)
        air = math.sin(2.0 * math.pi * (220.0 + 90.0 * progress) * t) * 0.12 * envelope
        hiss = (((index * 1664525 + 1013904223) & 0x7FFFFFFF) / 0x7FFFFFFF) - 0.5
        hiss *= 0.05 * envelope

        sample = (breath * 0.55 + air + hiss) * envelope
        samples.append(sample)
    return samples


def build_clip(name: str, samples: list[float]) -> Path:
    wav_path = OUT_DIR / f"{name}.wav"
    mp3_path = OUT_DIR / f"{name}.mp3"
    write_wav(wav_path, samples)
    convert_to_mp3(wav_path, mp3_path)
    wav_path.unlink(missing_ok=True)
    return mp3_path


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    fade_in = build_clip("hypoxia_fade_in", generate_fade_in())
    fade_out = build_clip("hypoxia_fade_out", generate_fade_out())
    compress = ROOT / "tools" / "compress_audio.py"
    subprocess.run(
        [sys.executable, str(compress), str(fade_in), str(fade_out), "--remove-source"],
        check=True,
        cwd=ROOT,
    )
    print(f"Wrote {fade_in.with_suffix('.mp3.xz')} and {fade_out.with_suffix('.mp3.xz')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())