#!/usr/bin/env python3
"""
Execute item authoring AI jobs written by the scene-editor.

Reads resources/.authoring/<itemId>_ai_jobs.json and generates:
  - generate_image / generate_icon  → PNG via xAI Grok Imagine API, then .xz
  - generate_examine_sound / generate_use_sound → short MP3 SFX (procedural), then .xz

API key resolution (first match):
  1. --key=
  2. $XAI_API_KEY
  3. <asset-root>/resources/xai_api_key
  4. <asset-root>/xai_api_key

Usage:
  python3 tools/run_item_authoring_ai.py --asset-root . --jobs-file resources/.authoring/foo_ai_jobs.json
  python3 tools/run_item_authoring_ai.py --asset-root . --item-id foo
"""

from __future__ import annotations

import argparse
import json
import lzma
import math
import os
import struct
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import wave
from pathlib import Path


IMAGE_TYPES = {"generate_image", "generate_icon"}
SOUND_TYPES = {"generate_examine_sound", "generate_use_sound"}


def resolve_api_key(asset_root: Path, cli_key: str | None) -> str:
    if cli_key and cli_key.strip():
        return cli_key.strip()
    for env_name in ("XAI_API_KEY", "xAI_API_KEY", "GROK_API_KEY"):
        env = os.environ.get(env_name, "").strip()
        if env:
            return env
    # Common project locations (gitignored).
    candidates = [
        asset_root / "resources" / "xai_api_key",
        asset_root / "xai_api_key",
        asset_root / ".env",
        Path.home() / ".config" / "xai" / "api_key",
    ]
    for candidate in candidates:
        if not candidate.is_file():
            continue
        text = candidate.read_text(encoding="utf-8").strip()
        if not text:
            continue
        # .env style: XAI_API_KEY=...
        if candidate.name == ".env" or "=" in text.splitlines()[0]:
            for line in text.splitlines():
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                if line.startswith("XAI_API_KEY="):
                    val = line.split("=", 1)[1].strip().strip('"').strip("'")
                    if val:
                        return val
            continue
        return text
    return ""


def load_jobs(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or "jobs" not in data:
        raise ValueError(f"Invalid jobs file: {path}")
    return data


def find_jobs_file(asset_root: Path, item_id: str) -> Path:
    return asset_root / "resources" / ".authoring" / f"{item_id}_ai_jobs.json"


def xz_compress(path: Path, remove_source: bool = False) -> Path:
    out = Path(str(path) + ".xz")
    data = path.read_bytes()
    out.write_bytes(lzma.compress(data, preset=6))
    if remove_source:
        path.unlink(missing_ok=True)
    return out


def generate_image(api_key: str, prompt: str, out_path: Path, aspect_ratio: str) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "model": "grok-imagine-image-quality",
        "prompt": prompt,
        "n": 1,
        "response_format": "b64_json",
        "aspect_ratio": aspect_ratio,
    }
    req = urllib.request.Request(
        "https://api.x.ai/v1/images/generations",
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=180) as resp:
            body = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        err = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"Image API HTTP {exc.code}: {err}") from exc

    data = body.get("data") or []
    if not data:
        raise RuntimeError(f"Image API returned no data: {body}")

    entry = data[0]
    if entry.get("b64_json"):
        import base64

        raw = base64.b64decode(entry["b64_json"])
        # API may return JPEG bytes; normalize to PNG when possible.
        raw = ensure_png_bytes(raw, out_path)
        out_path.write_bytes(raw)
        return

    url = entry.get("url")
    if not url:
        raise RuntimeError(f"Image API entry missing url/b64: {entry}")
    with urllib.request.urlopen(url, timeout=180) as img_resp:
        raw = img_resp.read()
    raw = ensure_png_bytes(raw, out_path)
    out_path.write_bytes(raw)


def ensure_png_bytes(raw: bytes, out_path: Path) -> bytes:
    """If output is .png but payload is JPEG, convert via Pillow or sips."""
    if out_path.suffix.lower() != ".png":
        return raw
    if raw[:8] == b"\x89PNG\r\n\x1a\n":
        return raw
    # Likely JPEG or other — convert.
    try:
        from PIL import Image
        import io

        with Image.open(io.BytesIO(raw)) as im:
            buf = io.BytesIO()
            im.convert("RGBA").save(buf, format="PNG")
            return buf.getvalue()
    except Exception:
        pass
    # Fallback: write temp and sips on macOS
    with tempfile.NamedTemporaryFile(suffix=".img", delete=False) as tmp:
        tmp.write(raw)
        tmp_path = Path(tmp.name)
    png_tmp = tmp_path.with_suffix(".png")
    try:
        subprocess.run(
            ["sips", "-s", "format", "png", str(tmp_path), "--out", str(png_tmp)],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        data = png_tmp.read_bytes()
        return data
    except Exception:
        # Last resort: write bytes as-is (may be wrong format)
        return raw
    finally:
        tmp_path.unlink(missing_ok=True)
        png_tmp.unlink(missing_ok=True)


def write_procedural_sfx_wav(out_wav: Path, action: str) -> None:
    """Short one-shot SFX when no dedicated audio gen API is available."""
    sample_rate = 22050
    duration = 0.28 if action == "examine" else 0.45
    n = int(sample_rate * duration)
    frames = bytearray()
    for i in range(n):
        t = i / sample_rate
        # Envelope
        env = math.exp(-t * (14.0 if action == "examine" else 8.0))
        if action == "examine":
            # Soft metallic click / handling noise
            sig = (
                0.55 * math.sin(2 * math.pi * 1800 * t)
                + 0.25 * math.sin(2 * math.pi * 3200 * t)
                + 0.15 * math.sin(2 * math.pi * 420 * t)
            )
        else:
            # Heavier use / mechanism
            sig = (
                0.5 * math.sin(2 * math.pi * 220 * t)
                + 0.35 * math.sin(2 * math.pi * 110 * t)
                + 0.2 * math.sin(2 * math.pi * 900 * t * (1.0 + t))
            )
        # Light noise
        noise = ((i * 1103515245 + 12345) & 0x7FFF) / 0x7FFF - 0.5
        sample = max(-1.0, min(1.0, (sig + 0.08 * noise) * env))
        frames += struct.pack("<h", int(sample * 30000))

    out_wav.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(out_wav), "w") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(bytes(frames))


def wav_to_mp3(wav_path: Path, mp3_path: Path) -> None:
    mp3_path.parent.mkdir(parents=True, exist_ok=True)
    # Prefer lame (reliable on macOS even when ffmpeg dylibs are broken).
    if shutil_which("lame"):
        subprocess.run(
            ["lame", "-V4", str(wav_path), str(mp3_path)],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return
    if shutil_which("ffmpeg"):
        subprocess.run(
            [
                "ffmpeg",
                "-y",
                "-i",
                str(wav_path),
                "-codec:a",
                "libmp3lame",
                "-qscale:a",
                "4",
                str(mp3_path),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return
    raise RuntimeError(
        "Need `lame` or `ffmpeg` to encode SFX as MP3 "
        "(e.g. brew install lame)."
    )


def shutil_which(name: str) -> str | None:
    from shutil import which

    return which(name)


def generate_sound(out_path: Path, action: str) -> None:
    """Generate examine/use SFX as MP3 under out_path."""
    out_path = out_path.with_suffix(".mp3") if out_path.suffix.lower() == ".opus" else out_path
    if out_path.suffix.lower() != ".mp3":
        out_path = out_path.with_suffix(".mp3")

    with tempfile.TemporaryDirectory() as tmp:
        wav = Path(tmp) / "sfx.wav"
        write_procedural_sfx_wav(wav, action)
        generate_target = Path(tmp) / "sfx.mp3"
        wav_to_mp3(wav, generate_target)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_bytes(generate_target.read_bytes())


def process_job(api_key: str, asset_root: Path, job: dict) -> str:
    jtype = job.get("type", "")
    out_rel = job.get("outPath", "")
    prompt = job.get("prompt", "")
    action = job.get("action", "examine")
    if not out_rel:
        raise ValueError(f"Job missing outPath: {job}")

    # Refuse garbage outPaths (e.g. a pasted API key mistaken for a path).
    if not str(out_rel).replace("\\", "/").startswith("resources/"):
        raise ValueError(
            f"Refusing outPath outside resources/: {out_rel!r} "
            "(check the path field — do not paste the API key there)"
        )

    out_path = asset_root / out_rel
    # Normalize accidental absolute
    if out_rel.startswith("/"):
        out_path = Path(out_rel)

    if jtype in IMAGE_TYPES:
        if not api_key:
            raise RuntimeError(
                "Missing XAI_API_KEY for image generation. "
                "Export XAI_API_KEY or place key in resources/xai_api_key."
            )
        aspect = "1:1" if jtype == "generate_icon" else "16:9"
        if not out_path.suffix:
            out_path = out_path.with_suffix(".png")
        if out_path.suffix.lower() != ".png":
            out_path = out_path.with_suffix(".png")
        print(f"  [image] {out_path.relative_to(asset_root)} …")
        generate_image(api_key, prompt, out_path, aspect)
        xz = xz_compress(out_path, remove_source=False)
        print(f"  [ok] wrote {out_path.name} + {xz.name}")
        return str(out_path.relative_to(asset_root))

    if jtype in SOUND_TYPES:
        # Prefer .mp3 for item SFX
        if out_path.suffix.lower() in {".opus", ".wav", ""}:
            out_path = out_path.with_suffix(".mp3")
        print(f"  [sound] {out_path.relative_to(asset_root)} ({action}) …")
        generate_sound(out_path, action if action in ("examine", "use") else "examine")
        xz = xz_compress(out_path, remove_source=False)
        print(f"  [ok] wrote {out_path.name} + {xz.name}")
        return str(out_path.relative_to(asset_root))

    # Text/TTS construction jobs are applied in the editor payload; nothing to render.
    print(f"  [skip] {jtype} (text job — handled by editor)")
    return ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--asset-root",
        type=Path,
        default=Path("."),
        help="Game root containing resources/",
    )
    parser.add_argument("--jobs-file", type=Path, default=None)
    parser.add_argument("--item-id", type=str, default=None)
    parser.add_argument("--key", type=str, default=None)
    args = parser.parse_args()

    asset_root = args.asset_root.resolve()
    if args.jobs_file:
        jobs_path = args.jobs_file if args.jobs_file.is_absolute() else asset_root / args.jobs_file
    elif args.item_id:
        jobs_path = find_jobs_file(asset_root, args.item_id)
    else:
        print("Provide --jobs-file or --item-id", file=sys.stderr)
        return 2

    if not jobs_path.is_file():
        print(f"Jobs file not found: {jobs_path}", file=sys.stderr)
        return 2

    api_key = resolve_api_key(asset_root, args.key)
    data = load_jobs(jobs_path)
    item_id = data.get("itemId", args.item_id or "?")
    jobs = data.get("jobs") or []
    print(f"Running {len(jobs)} authoring job(s) for {item_id}")

    errors: list[str] = []
    produced: list[str] = []
    for job in jobs:
        try:
            rel = process_job(api_key, asset_root, job)
            if rel:
                produced.append(rel)
        except Exception as exc:  # noqa: BLE001 — report per-job
            msg = f"{job.get('type', '?')}: {exc}"
            print(f"  [fail] {msg}", file=sys.stderr)
            errors.append(msg)

    # Update jobs file status
    data["lastRun"] = {
        "produced": produced,
        "errors": errors,
    }
    jobs_path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")

    if errors:
        print(f"Done with {len(errors)} error(s).", file=sys.stderr)
        return 1
    print("Done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
