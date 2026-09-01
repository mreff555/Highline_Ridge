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
SOUND_TYPES = {
    "generate_examine_sound",
    "generate_use_sound",
    "generate_ambient_sound",
    "generate_music",
}
SCENE_TTS_TEXT_TYPES = {
    "generate_scene_description_tts_text",
    "generate_scene_examine_tts_text",
}


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


def _rename_replace(src: Path, dst: Path) -> None:
    if not src.exists():
        return
    if dst.exists():
        dst.unlink()
    src.rename(dst)


def rotate_live_asset_backup(path: Path) -> None:
    """Before overwriting a live asset while editing:
    name_1.ext (prior backup) → name_2.ext
    name.ext (current) → name_1.ext
    Same for sibling .xz companions.
    New content is then written to name.ext.
    """
    path = Path(path)
    p1 = path.with_name(f"{path.stem}_1{path.suffix}")
    p2 = path.with_name(f"{path.stem}_2{path.suffix}")
    xz = Path(str(path) + ".xz")
    xz1 = Path(str(p1) + ".xz")
    xz2 = Path(str(p2) + ".xz")

    _rename_replace(p1, p2)
    _rename_replace(xz1, xz2)
    _rename_replace(path, p1)
    _rename_replace(xz, xz1)


# --- Ambient backend (swappable; LocalLayerSynth v1; ElevenLabs later) ---

AMBIENT_LAYER_CATALOG = [
    "wind_soft",
    "wind_gust",
    "birds_distant",
    "stream_faint",
    "forest_bed",
    "town_murmur",
    "fire_soft",
    "rain_light",
    "insects_night",
]


def plan_ambient_layers_via_chat(api_key: str, prompt: str) -> dict:
    """Ask Grok for a JSON layer plan using only AMBIENT_LAYER_CATALOG ids."""
    catalog = ", ".join(AMBIENT_LAYER_CATALOG)
    system = (
        "You design seamless loopable ambient SOUND BEDS for a period adventure game. "
        "Return ONLY valid JSON (no markdown) with keys durationSec (6-12) and layers "
        "(array of {id, gain 0-1, optional density 0-1}). "
        f"id MUST be one of: {catalog}. Pick 2-4 layers that match the scene. "
        "No speech, no melodic music — environmental beds only."
    )
    raw = generate_chat_text_with_system(api_key, system, prompt)
    raw = strip_llm_fences(raw)
    try:
        plan = json.loads(raw)
    except json.JSONDecodeError:
        # try extract first {...}
        a, b = raw.find("{"), raw.rfind("}")
        if a >= 0 and b > a:
            plan = json.loads(raw[a : b + 1])
        else:
            raise
    if not isinstance(plan, dict):
        raise ValueError("ambient plan is not an object")
    layers = plan.get("layers") or []
    cleaned = []
    for layer in layers:
        if not isinstance(layer, dict):
            continue
        lid = str(layer.get("id", "")).strip()
        if lid not in AMBIENT_LAYER_CATALOG:
            continue
        gain = float(layer.get("gain", 0.3))
        gain = max(0.05, min(1.0, gain))
        density = float(layer.get("density", 0.4))
        density = max(0.0, min(1.0, density))
        cleaned.append({"id": lid, "gain": gain, "density": density})
    if not cleaned:
        cleaned = [
            {"id": "wind_soft", "gain": 0.4, "density": 0.5},
            {"id": "birds_distant", "gain": 0.25, "density": 0.35},
        ]
    dur = float(plan.get("durationSec", 8.0))
    dur = max(6.0, min(12.0, dur))
    return {"durationSec": dur, "layers": cleaned, "backend": "local_layers_v1"}


def generate_chat_text_with_system(api_key: str, system: str, user: str) -> str:
    payload = {
        "model": "grok-4.20-non-reasoning",
        "messages": [
            {"role": "system", "content": system},
            {"role": "user", "content": user},
        ],
        "temperature": 0.4,
    }
    req = urllib.request.Request(
        "https://api.x.ai/v1/chat/completions",
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
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"chat HTTP {exc.code}: {detail}") from exc
    choices = body.get("choices") or []
    if not choices:
        raise RuntimeError(f"chat response missing choices: {body}")
    message = choices[0].get("message") or {}
    content = message.get("content") or ""
    if isinstance(content, list):
        parts = []
        for part in content:
            if isinstance(part, dict) and part.get("type") == "text":
                parts.append(part.get("text", ""))
            elif isinstance(part, str):
                parts.append(part)
        content = "".join(parts)
    return strip_llm_fences(str(content))


def _layer_sample(layer_id: str, t: float, i: int, density: float) -> float:
    """One sample of a named ambient layer at time t (seconds)."""
    noise = ((i * 1103515245 + 12345) & 0x7FFF) / 0x7FFF - 0.5
    n2 = ((i * 1664525 + 1013904223) & 0x7FFFFFFF) / 0x7FFFFFFF - 0.5
    if layer_id == "wind_soft":
        env = 0.55 + 0.45 * math.sin(2 * math.pi * 0.11 * t)
        return (0.45 * noise + 0.12 * math.sin(2 * math.pi * 70 * t)) * env * 0.5
    if layer_id == "wind_gust":
        gust = max(0.0, math.sin(2 * math.pi * 0.08 * t + noise)) ** 2
        return (0.55 * noise + 0.2 * n2) * gust * 0.65
    if layer_id == "birds_distant":
        # Sparse chirps: density controls how often
        phase = (i // 2205)  # ~0.1s buckets at 22.05k
        seed = (phase * 2654435761) & 0xFFFFFFFF
        if (seed % 1000) / 1000.0 > density * 0.35:
            return 0.0
        local = (i % 2205) / 22050.0
        env = math.exp(-local * 28.0) if local < 0.12 else 0.0
        f = 1800.0 + (seed % 900)
        return 0.22 * math.sin(2 * math.pi * f * t) * env
    if layer_id == "stream_faint":
        # Soft broadband water + slow swirl
        swirl = 0.5 + 0.5 * math.sin(2 * math.pi * 0.2 * t)
        return (0.5 * noise + 0.25 * n2) * swirl * 0.28
    if layer_id == "forest_bed":
        return (
            0.2 * noise
            + 0.15 * math.sin(2 * math.pi * 55 * t)
            + 0.08 * math.sin(2 * math.pi * 110 * t)
        ) * 0.35
    if layer_id == "town_murmur":
        murmur = 0.15 * math.sin(2 * math.pi * 120 * t + noise * 3)
        return (0.25 * noise + murmur) * 0.25
    if layer_id == "fire_soft":
        crack = noise if (i % 17 == 0) else noise * 0.3
        return crack * (0.2 + 0.15 * abs(n2)) * 0.4
    if layer_id == "rain_light":
        return abs(noise) * 0.22 + 0.05 * n2
    if layer_id == "insects_night":
        buzz = math.sin(2 * math.pi * 4200 * t) * (0.5 + 0.5 * math.sin(2 * math.pi * 3.1 * t))
        return buzz * 0.08 * density
    return 0.15 * noise


def synthesize_layered_ambient(plan: dict, out_wav: Path) -> None:
    sample_rate = 22050
    duration = float(plan.get("durationSec", 8.0))
    n = int(sample_rate * duration)
    layers = plan.get("layers") or []
    frames = bytearray()
    for i in range(n):
        t = i / sample_rate
        # Seamless-ish loop: fade edges
        edge = min(t, duration - t, 0.35) / 0.35
        edge = max(0.0, min(1.0, edge))
        sample = 0.0
        for layer in layers:
            lid = layer.get("id", "wind_soft")
            gain = float(layer.get("gain", 0.3))
            density = float(layer.get("density", 0.4))
            sample += gain * _layer_sample(lid, t, i, density)
        sample = max(-1.0, min(1.0, sample * edge * 0.85))
        frames += struct.pack("<h", int(sample * 30000))
    out_wav.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(out_wav), "w") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(bytes(frames))


def render_ambient_backend(api_key: str, prompt: str, out_path: Path) -> str:
    """
    AmbientAudioBackend entry point.
    v1: chat layer plan + LocalLayerSynth.
    Future: swap body for ElevenLabs (or other) without changing callers.
    """
    backend = "local_layers_v1"
    if api_key:
        try:
            plan = plan_ambient_layers_via_chat(api_key, prompt)
            print(f"  [ambient-plan] {plan}")
        except Exception as exc:  # noqa: BLE001
            print(f"  [ambient-plan fallback] {exc}")
            plan = {
                "durationSec": 8.0,
                "layers": [
                    {"id": "wind_soft", "gain": 0.35, "density": 0.5},
                    {"id": "birds_distant", "gain": 0.28, "density": 0.4},
                    {"id": "stream_faint", "gain": 0.3, "density": 0.5},
                ],
                "backend": backend,
            }
    else:
        plan = {
            "durationSec": 8.0,
            "layers": [
                {"id": "wind_soft", "gain": 0.4, "density": 0.5},
                {"id": "forest_bed", "gain": 0.25, "density": 0.4},
            ],
            "backend": backend,
        }
    with tempfile.TemporaryDirectory() as tmp:
        wav = Path(tmp) / "ambient.wav"
        synthesize_layered_ambient(plan, wav)
        generate_target = Path(tmp) / "ambient.mp3"
        wav_to_mp3(wav, generate_target)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_bytes(generate_target.read_bytes())
    return backend




def strip_llm_fences(text: str) -> str:
    text = (text or "").strip()
    if text.startswith("```"):
        lines = text.splitlines()
        if lines:
            lines = lines[1:]
        if lines and lines[-1].strip().startswith("```"):
            lines = lines[:-1]
        text = "\n".join(lines).strip()
    return text


def generate_chat_text(api_key: str, prompt: str) -> str:
    """xAI chat completion for TTS markup rewrite (no image/TTS audio charges beyond chat)."""
    payload = {
        "model": "grok-4.20-non-reasoning",
        "messages": [
            {
                "role": "system",
                "content": (
                    "You write Timberline / Highline Ridge spoken TTS markup. "
                    "Reply with ONLY the spoken text using [pause]/[long-pause]/ "
                    "style tags like <whisper></whisper>, and {{voice:id}}...{{/voice}} "
                    "where needed. No markdown fences, no commentary."
                ),
            },
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.5,
    }
    req = urllib.request.Request(
        "https://api.x.ai/v1/chat/completions",
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
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"chat HTTP {exc.code}: {detail}") from exc
    choices = body.get("choices") or []
    if not choices:
        raise RuntimeError(f"chat response missing choices: {body}")
    message = choices[0].get("message") or {}
    content = message.get("content") or ""
    if isinstance(content, list):
        parts = []
        for part in content:
            if isinstance(part, dict) and part.get("type") == "text":
                parts.append(part.get("text", ""))
            elif isinstance(part, str):
                parts.append(part)
        content = "".join(parts)
    text = strip_llm_fences(str(content))
    if not text:
        raise RuntimeError("chat returned empty TTS text")
    return text


def generate_image(
    api_key: str,
    prompt: str,
    out_path: Path,
    aspect_ratio: str,
    *,
    resolution: str = "2k",
    model: str = "grok-imagine-image-2.0",
) -> None:
    """Generate a scene/item plate via xAI Imagine.

    Scene masters default to 16:9 at resolution=2k (API max; ~2K class, not 4K).
    Icons stay 1:1; callers may pass resolution=\"1k\" to save cost.
    """
    out_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "model": model,
        "prompt": prompt,
        "n": 1,
        "response_format": "b64_json",
        "aspect_ratio": aspect_ratio,
        "resolution": resolution,
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
    """Procedural audio when no dedicated audio gen API is available."""
    sample_rate = 22050
    if action == "ambient":
        duration = 6.0
    elif action == "music":
        duration = 10.0
    elif action in ("enter", "exit", "use"):
        duration = 0.45
    else:
        duration = 0.28
    n = int(sample_rate * duration)
    frames = bytearray()
    for i in range(n):
        t = i / sample_rate
        noise = ((i * 1103515245 + 12345) & 0x7FFF) / 0x7FFF - 0.5
        if action == "ambient":
            # Soft wind / room tone loop (no sharp envelope).
            env = 0.55 + 0.45 * math.sin(2 * math.pi * 0.15 * t)
            sig = (
                0.35 * noise
                + 0.2 * math.sin(2 * math.pi * 90 * t)
                + 0.12 * math.sin(2 * math.pi * 180 * t + noise)
                + 0.08 * math.sin(2 * math.pi * 40 * t)
            )
            sample = max(-1.0, min(1.0, sig * env * 0.55))
        elif action == "music":
            # Sparse minor-ish pad: low fifths + slow motion.
            env = 0.5 + 0.5 * math.sin(2 * math.pi * 0.07 * t)
            f1 = 110.0
            f2 = 164.81  # E3
            f3 = 196.0
            sig = (
                0.28 * math.sin(2 * math.pi * f1 * t)
                + 0.22 * math.sin(2 * math.pi * f2 * t)
                + 0.16 * math.sin(2 * math.pi * f3 * t)
                + 0.05 * noise
            )
            # Gentle tremolo
            trem = 0.85 + 0.15 * math.sin(2 * math.pi * 3.1 * t)
            sample = max(-1.0, min(1.0, sig * env * trem * 0.7))
        elif action == "examine":
            env = math.exp(-t * 14.0)
            sig = (
                0.55 * math.sin(2 * math.pi * 1800 * t)
                + 0.25 * math.sin(2 * math.pi * 3200 * t)
                + 0.15 * math.sin(2 * math.pi * 420 * t)
            )
            sample = max(-1.0, min(1.0, (sig + 0.08 * noise) * env))
        else:
            # use / enter / exit — door-ish thump + click
            env = math.exp(-t * 8.0)
            sig = (
                0.5 * math.sin(2 * math.pi * 220 * t)
                + 0.35 * math.sin(2 * math.pi * 110 * t)
                + 0.2 * math.sin(2 * math.pi * 900 * t * (1.0 + t))
            )
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


def process_job(
    api_key: str,
    asset_root: Path,
    job: dict,
    *,
    backup_rotate: bool = False,
) -> str:
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

    # Preview jobs under .authoring/ should not rotate live scene assets.
    job_rotate = bool(job.get("backupRotate", backup_rotate))
    if job_rotate and ".authoring/" in str(out_rel).replace("\\", "/"):
        job_rotate = False

    if jtype in IMAGE_TYPES:
        if not api_key:
            raise RuntimeError(
                "Missing XAI_API_KEY for image generation. "
                "Export XAI_API_KEY or place key in resources/xai_api_key."
            )
        aspect = str(job.get("aspectRatio") or ("1:1" if jtype == "generate_icon" else "16:9"))
        # Icons: 1k is enough. Scene plates: 2k (highest Imagine still supports).
        resolution = str(
            job.get("resolution")
            or ("1k" if jtype == "generate_icon" else "2k")
        ).lower()
        model = str(job.get("model") or "grok-imagine-image-2.0")
        if not out_path.suffix:
            out_path = out_path.with_suffix(".png")
        if out_path.suffix.lower() != ".png":
            out_path = out_path.with_suffix(".png")
        if job_rotate:
            print(f"  [backup] rotate {out_path.name} → _1 / prior _1 → _2")
            rotate_live_asset_backup(out_path)
        print(
            f"  [image] {out_path.relative_to(asset_root)} "
            f"({aspect}, {resolution}, {model}) …"
        )
        generate_image(
            api_key,
            prompt,
            out_path,
            aspect,
            resolution=resolution,
            model=model,
        )
        xz = xz_compress(out_path, remove_source=False)
        print(f"  [ok] wrote {out_path.name} + {xz.name}")
        return str(out_path.relative_to(asset_root))

    if jtype in SOUND_TYPES:
        # Prefer .mp3 for item SFX / scene beds
        if out_path.suffix.lower() in {".opus", ".wav", ""}:
            out_path = out_path.with_suffix(".mp3")
        if jtype == "generate_ambient_sound":
            action = "ambient"
        elif jtype == "generate_music":
            action = "music"
        elif action not in ("examine", "use", "enter", "exit", "ambient", "music"):
            action = "examine"
        if job_rotate:
            print(f"  [backup] rotate {out_path.name} → _1 / prior _1 → _2")
            rotate_live_asset_backup(out_path)
        print(f"  [sound] {out_path.relative_to(asset_root)} ({action}) …")
        if jtype == "generate_ambient_sound":
            backend = render_ambient_backend(api_key, prompt, out_path)
            print(f"  [ambient-backend] {backend}")
        else:
            generate_sound(out_path, action)
        xz = xz_compress(out_path, remove_source=False)
        print(f"  [ok] wrote {out_path.name} + {xz.name}")
        return str(out_path.relative_to(asset_root))

    if jtype in SCENE_TTS_TEXT_TYPES:
        if not api_key:
            raise RuntimeError(
                "Missing XAI_API_KEY for TTS dialog generation. "
                "Export XAI_API_KEY or place key in resources/xai_api_key."
            )
        print(f"  [tts-text] {jtype} …")
        text = generate_chat_text(api_key, prompt)
        job["resultText"] = text
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(text + "\n", encoding="utf-8")
        print(f"  [ok] wrote TTS markup ({len(text)} chars) → {out_path.name}")
        return str(out_path.relative_to(asset_root))

    # Legacy item text/TTS construction jobs are applied in the editor payload.
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
    backup_rotate = bool(data.get("backupRotate", False))
    print(f"Running {len(jobs)} authoring job(s) for {item_id}"
          + (" (backupRotate)" if backup_rotate else ""))

    errors: list[str] = []
    produced: list[str] = []
    for job in jobs:
        try:
            rel = process_job(
                api_key, asset_root, job, backup_rotate=backup_rotate)
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
