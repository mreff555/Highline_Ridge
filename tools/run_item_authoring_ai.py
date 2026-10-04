#!/usr/bin/env python3
"""
Execute item authoring AI jobs written by the scene-editor.

Reads resources/.authoring/<itemId>_ai_jobs.json and generates:
  - generate_image / generate_icon  → PNG via xAI Grok Imagine API, then .xz
  - generate_examine_sound / generate_use_sound / enter / exit → ElevenLabs SFX MP3, then .xz

API key resolution (first match):
  1. --key=
  2. $XAI_API_KEY
  3. ~/.config/highline-ridge/xai_api_key
  4. <asset-root>/resources/xai_api_key (legacy; prefer user config)
  5. <asset-root>/xai_api_key (legacy)

Usage:
  python3 tools/run_item_authoring_ai.py --asset-root . --jobs-file resources/.authoring/foo_ai_jobs.json
  python3 tools/run_item_authoring_ai.py --asset-root . --item-id foo
"""

from __future__ import annotations

import argparse
import array
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
# Item dialog text jobs (chat → resultText + .txt under resources/.authoring/).
ITEM_TEXT_TYPES = {
    "generate_tts_description",
    "generate_tts_construction_description",
    "generate_construction_description",
}
# Legacy logical outPath tokens (still accepted; rewritten under .authoring/).
LOGICAL_OUT_PATHS = {
    "examineTts": "examine_tts.txt",
    "assembleTts": "assemble_tts.txt",
    "assembleNarrative": "assemble_narrative.txt",
}


def _read_env_file_key(path: Path, names: tuple[str, ...]) -> str:
    if not path.is_file():
        return ""
    text = path.read_text(encoding="utf-8").strip()
    if not text:
        return ""
    first = text.splitlines()[0]
    if path.name == ".env" or "=" in first:
        for line in text.splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            for name in names:
                prefix = name + "="
                if line.startswith(prefix):
                    val = line.split("=", 1)[1].strip().strip('"').strip("'")
                    if val:
                        return val
        return ""
    return text


def resolve_api_key(asset_root: Path, cli_key: str | None) -> str:
    if cli_key and cli_key.strip():
        return cli_key.strip()
    for env_name in ("XAI_API_KEY", "xAI_API_KEY", "GROK_API_KEY"):
        env = os.environ.get(env_name, "").strip()
        if env:
            return env
    # Prefer user config over resources/ (secrets must not live in the asset tree).
    candidates = [
        Path.home() / ".config" / "highline-ridge" / "xai_api_key",
        Path.home() / ".config" / "xai" / "api_key",
        asset_root / "resources" / "xai_api_key",  # legacy
        asset_root / "xai_api_key",  # legacy
        asset_root / ".env",
    ]
    for candidate in candidates:
        val = _read_env_file_key(
            candidate, ("XAI_API_KEY", "xAI_API_KEY", "GROK_API_KEY")
        )
        if val:
            return val
        # Plain single-line key files (not .env).
        if candidate.name != ".env" and candidate.is_file():
            text = candidate.read_text(encoding="utf-8").strip()
            if text and "=" not in text.splitlines()[0]:
                return text
    return ""


def resolve_elevenlabs_api_key(
    asset_root: Path, cli_key: str | None = None
) -> str:
    """ElevenLabs Music key — never fall back to the xAI key."""
    if cli_key and cli_key.strip():
        return cli_key.strip()
    for env_name in ("ELEVENLABS_API_KEY", "ELEVEN_API_KEY", "XI_API_KEY"):
        env = os.environ.get(env_name, "").strip()
        if env:
            return env
    candidates = [
        Path.home() / ".config" / "highline-ridge" / "elevenlabs_api_key",
        Path.home() / ".config" / "elevenlabs" / "api_key",
        asset_root / ".env",
    ]
    for candidate in candidates:
        val = _read_env_file_key(
            candidate, ("ELEVENLABS_API_KEY", "ELEVEN_API_KEY", "XI_API_KEY")
        )
        if val:
            return val
        if candidate.name != ".env" and candidate.is_file():
            text = candidate.read_text(encoding="utf-8").strip()
            if text and "=" not in text.splitlines()[0]:
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


# --- Ambient backend (swappable; LocalLayerSynth v1; music via ElevenLabs) ---

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
    # Interior / workplace layers (implemented in _layer_sample).
    "kitchen_clatter",
    "steam_hiss",
    "crowd_muffled",
]

# Positive / negative styles for ElevenLabs Music composition plans (v2 / v2.5).
# Keep these musical — never pass image/world casting constraints into music.
MUSIC_STYLE_PRESETS: dict[str, dict] = {
    "saloon_piano": {
        "label": "saloon parlor upright piano",
        "cue": "{upright piano playing soft early ragtime waltz fragments}",
        "bpm": "96 BPM",
        "positive": [
            "1890s American frontier saloon underscore",
            "solo upright piano",
            "early ragtime and waltz fragments",
            "warm wooden parlor acoustics",
            "modest tempo",
            "instrumental only",
            "melodic but restrained game bed",
        ],
        "negative": [
            "vocals",
            "choir",
            "synth",
            "electronic",
            "EDM",
            "drums",
            "electric guitar",
            "noir jazz saxophone",
            "trailer braams",
            "sound effects",
            "high-pitched sine tones",
            "glitch noise",
            "distortion",
        ],
    },
    "trail_folk": {
        "label": "mountain trail folk fiddle",
        "cue": "{sparse fiddle melody with soft acoustic guitar accompaniment}",
        "bpm": "84 BPM",
        "positive": [
            "1890s American frontier folk underscore",
            "Colorado high country trail mood",
            "sparse acoustic fiddle lead",
            "soft fingerpicked acoustic guitar",
            "warm woody natural tone",
            "gentle walking tempo",
            "intimate and restrained",
            "instrumental only",
            "seamless loopable game bed",
        ],
        "negative": [
            "vocals",
            "choir",
            "humming",
            "synth",
            "electronic pads",
            "EDM",
            "modern drums",
            "electric guitar",
            "trailer music",
            "noir jazz",
            "sound effects",
            "wind noise bed",
            "bird calls as lead",
            "harsh screeching",
            "high-pitched electronic tones",
            "glitch",
            "distortion",
            "white noise",
        ],
    },
    "cabin_hearth": {
        "label": "cabin hearth sparse piano",
        "cue": "{soft pedaled piano near a quiet cabin hearth}",
        "bpm": "72 BPM",
        "positive": [
            "1890s frontier cabin underscore",
            "sparse intimate piano",
            "soft pedaled notes",
            "quiet and warm",
            "instrumental only",
            "loopable game bed",
        ],
        "negative": [
            "vocals",
            "synth",
            "electronic",
            "drums",
            "trailer braams",
            "sound effects",
            "high-pitched sine tones",
            "glitch noise",
        ],
    },
    "mining_camp": {
        "label": "mining camp harmonica",
        "cue": "{spare harmonica with quiet acoustic guitar}",
        "bpm": "88 BPM",
        "positive": [
            "1890s mining camp underscore",
            "spare harmonica",
            "quiet acoustic guitar",
            "dusty and restrained",
            "instrumental only",
            "loopable game bed",
        ],
        "negative": [
            "vocals",
            "synth",
            "electronic",
            "EDM",
            "trailer braams",
            "sound effects",
            "high-pitched electronic tones",
            "glitch noise",
        ],
    },
    "tension": {
        "label": "period tension underscore",
        "cue": "{low piano and muted strings, held tension}",
        "bpm": "66 BPM",
        "positive": [
            "1890s period tension underscore",
            "low piano",
            "muted acoustic strings",
            "still and restrained",
            "instrumental only",
            "loopable game bed",
        ],
        "negative": [
            "vocals",
            "synth",
            "electronic",
            "trailer braams",
            "horror stingers",
            "modern percussion",
            "sound effects",
            "high-pitched sine tones",
            "glitch noise",
        ],
    },
    "title_hymn": {
        "label": "title hymn acoustic",
        "cue": "{hopeful hymn-like acoustic guitar and soft piano}",
        "bpm": "76 BPM",
        "positive": [
            "1890s frontier title theme",
            "hymn-like acoustic guitar",
            "soft piano",
            "hopeful but worn",
            "instrumental only",
            "loopable menu bed",
        ],
        "negative": [
            "vocals",
            "choir lyrics",
            "synth",
            "electronic",
            "EDM",
            "trailer braams",
            "sound effects",
            "high-pitched electronic tones",
        ],
    },
    "scarlet_whispers": {
        "label": "Scarlet Whispers tragic violin orchestra",
        "cue": (
            "{solo tragic violin over soft chamber orchestra at dawn, "
            "melancholy sustained strings, restrained dynamics}"
        ),
        "bpm": "68 BPM",
        "positive": [
            "Scarlet Whispers at Dawn mood",
            "tragic romantic violin lead",
            "small chamber orchestra underscore",
            "warm acoustic strings and soft woodwinds",
            "melancholy dawn atmosphere",
            "1890s period cinematic but restrained",
            "intimate and mournful",
            "instrumental only",
            "seamless loopable game bed",
            "moderate dynamics no sudden hits",
        ],
        "negative": [
            "vocals",
            "choir lyrics",
            "humming",
            "synth",
            "electronic pads",
            "EDM",
            "modern drums",
            "electric guitar",
            "trailer braams",
            "horror stingers",
            "jump scares",
            "harsh screeching violin",
            "atonal noise",
            "sound effects",
            "wind noise bed",
            "high-pitched electronic tones",
            "glitch",
            "distortion",
            "white noise",
        ],
    },
}


def plan_ambient_layers_via_chat(api_key: str, prompt: str) -> dict:
    """Ask Grok for a JSON layer plan using only AMBIENT_LAYER_CATALOG ids."""
    catalog = ", ".join(AMBIENT_LAYER_CATALOG)
    system = (
        "You design seamless loopable ambient SOUND BEDS for a period adventure game. "
        "Allowed layer ids: wind_soft, wind_gust, birds_distant, stream_faint, forest_bed, "
        "town_murmur, fire_soft, rain_light, insects_night, kitchen_clatter, steam_hiss, "
        "crowd_muffled. For kitchens / back-of-house prefer kitchen_clatter, steam_hiss, "
        "fire_soft, crowd_muffled (rowdy bar through a doorway), town_murmur. "
        "Return ONLY valid JSON (no markdown) with keys durationSec (8-14) and layers "
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
    if layer_id == "kitchen_clatter":
        # Sparse metallic taps / plate knocks.
        phase = i // 1800
        seed = (phase * 2654435761) & 0xFFFFFFFF
        if (seed % 1000) / 1000.0 > density * 0.45:
            return 0.0
        local = (i % 1800) / 22050.0
        env = math.exp(-local * 40.0) if local < 0.08 else 0.0
        f = 2400.0 + (seed % 1600)
        return (0.35 * math.sin(2 * math.pi * f * t) + 0.12 * noise) * env
    if layer_id == "steam_hiss":
        hiss = abs(noise) * 0.55 + 0.2 * abs(n2)
        swell = 0.55 + 0.45 * math.sin(2 * math.pi * 0.09 * t + 1.2)
        return hiss * swell * 0.22
    if layer_id == "crowd_muffled":
        # Distant rowdy bar bleed — low murmur, no intelligible speech.
        murmur = (
            0.18 * math.sin(2 * math.pi * 95 * t + noise * 2)
            + 0.12 * math.sin(2 * math.pi * 140 * t + n2)
            + 0.2 * noise
        )
        throb = 0.6 + 0.4 * math.sin(2 * math.pi * 0.35 * t)
        return murmur * throb * 0.28 * density
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


def _image_to_data_uri(image_path: Path) -> str:
    import base64

    raw = image_path.read_bytes()
    suffix = image_path.suffix.lower()
    mime = "image/png"
    if suffix in {".jpg", ".jpeg"}:
        mime = "image/jpeg"
    elif suffix == ".webp":
        mime = "image/webp"
    return f"data:{mime};base64," + base64.b64encode(raw).decode("ascii")


def generate_imagine_video(
    api_key: str,
    prompt: str,
    *,
    image_path: Path | None = None,
    duration: int = 10,
    aspect_ratio: str = "16:9",
    resolution: str = "720p",
    model: str = "grok-imagine-video-1.5",
    poll_interval_sec: float = 5.0,
    poll_timeout_sec: float = 600.0,
) -> tuple[str, Path]:
    """
    Start an Imagine Video job, poll until done, download mp4.
    Returns (video_url, local_mp4_path) — caller owns cleanup of the temp file
    if they pass no destination (we always write a NamedTemporaryFile).
    """
    payload: dict = {
        "model": model,
        "prompt": prompt,
        "duration": int(max(1, min(15, duration))),
        "aspect_ratio": aspect_ratio,
        "resolution": resolution,
        "generate_audio": True,
    }
    if image_path is not None and image_path.is_file():
        payload["image"] = {"url": _image_to_data_uri(image_path)}

    req = urllib.request.Request(
        "https://api.x.ai/v1/videos/generations",
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            start_body = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"video generations HTTP {exc.code}: {detail}") from exc

    request_id = start_body.get("request_id") or start_body.get("id")
    if not request_id:
        raise RuntimeError(f"video generations missing request_id: {start_body}")
    print(f"  [video] request_id={request_id} (polling…)")

    import time

    deadline = time.time() + poll_timeout_sec
    video_url = ""
    while time.time() < deadline:
        poll_req = urllib.request.Request(
            f"https://api.x.ai/v1/videos/{request_id}",
            headers={"Authorization": f"Bearer {api_key}"},
            method="GET",
        )
        try:
            with urllib.request.urlopen(poll_req, timeout=60) as resp:
                data = json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise RuntimeError(f"video poll HTTP {exc.code}: {detail}") from exc

        status = str(data.get("status") or "").lower()
        if status == "done":
            video = data.get("video") or {}
            video_url = str(video.get("url") or "")
            if not video_url:
                raise RuntimeError(f"video done but missing url: {data}")
            break
        if status in {"failed", "expired"}:
            raise RuntimeError(f"video generation {status}: {data}")
        time.sleep(poll_interval_sec)
    else:
        raise RuntimeError(f"video generation timed out after {poll_timeout_sec:.0f}s")

    with urllib.request.urlopen(video_url, timeout=180) as vid_resp:
        mp4_bytes = vid_resp.read()
    tmp = tempfile.NamedTemporaryFile(suffix=".mp4", delete=False)
    tmp_path = Path(tmp.name)
    try:
        tmp.write(mp4_bytes)
        tmp.close()
    except Exception:
        tmp.close()
        tmp_path.unlink(missing_ok=True)
        raise
    return video_url, tmp_path


def resolve_ffmpeg_exe() -> str:
    """Prefer a working ffmpeg. Homebrew builds are often broken via dyld — try bundled."""
    candidates: list[str] = []
    try:
        import imageio_ffmpeg  # type: ignore

        candidates.append(imageio_ffmpeg.get_ffmpeg_exe())
    except Exception:
        pass
    for name in ("ffmpeg", "/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg"):
        found = shutil_which(name) if "/" not in name else (name if Path(name).is_file() else None)
        if found:
            candidates.append(found)
    # Probe each briefly; skip dyld-broken installs.
    for exe in candidates:
        try:
            probe = subprocess.run(
                [exe, "-version"],
                capture_output=True,
                text=True,
                timeout=5,
            )
            if probe.returncode == 0 and "ffmpeg" in (probe.stdout + probe.stderr).lower():
                return exe
        except Exception:
            continue
    raise RuntimeError(
        "No working ffmpeg found (needed for xai_video ambient extract). "
        "Install ffmpeg or `pip install imageio-ffmpeg`."
    )


def validate_audio_mp3(path: Path, *, min_bytes: int = 4000, label: str = "audio") -> None:
    """Reject empty / tiny MP3 writes before they clobber a live bed."""
    if not path.is_file():
        raise RuntimeError(f"{label} missing after generate: {path}")
    size = path.stat().st_size
    if size < min_bytes:
        raise RuntimeError(
            f"{label} too small ({size} bytes < {min_bytes}); refusing to keep junk"
        )


def decode_mp3_to_wav(mp3_path: Path, wav_path: Path) -> None:
    """Decode MP3 → WAV using afconvert (macOS) or lame --decode."""
    wav_path.parent.mkdir(parents=True, exist_ok=True)
    if shutil_which("afconvert"):
        result = subprocess.run(
            [
                "afconvert",
                "-f",
                "WAVE",
                "-d",
                "LEI16@44100",
                str(mp3_path),
                str(wav_path),
            ],
            capture_output=True,
            text=True,
        )
        if result.returncode == 0 and wav_path.is_file():
            return
    if shutil_which("lame"):
        result = subprocess.run(
            ["lame", "--decode", str(mp3_path), str(wav_path)],
            capture_output=True,
            text=True,
        )
        if result.returncode == 0 and wav_path.is_file():
            return
    if shutil_which("ffmpeg"):
        result = subprocess.run(
            ["ffmpeg", "-y", "-i", str(mp3_path), str(wav_path)],
            capture_output=True,
            text=True,
        )
        if result.returncode == 0 and wav_path.is_file():
            return
    raise RuntimeError(
        f"Could not decode {mp3_path.name} to WAV (need afconvert, lame, or ffmpeg)"
    )


def measure_wav_peak(wav_path: Path) -> float:
    """Return peak amplitude 0..1 for a 16-bit WAV."""
    with wave.open(str(wav_path), "rb") as wf:
        if wf.getsampwidth() != 2:
            return 0.0
        raw = wf.readframes(wf.getnframes())
    samples = array.array("h")
    samples.frombytes(raw)
    if not samples:
        return 0.0
    return max(abs(int(v)) for v in samples) / 32767.0


def _apply_one_pole_lowpass_inplace(
    samples: "array.array[int]", nch: int, fr: int, cutoff_hz: float
) -> None:
    """Gentle HF roll-off to tame hiss when boosting quiet beds."""
    cutoff_hz = max(200.0, min(float(fr) * 0.45, float(cutoff_hz)))
    # y[n] = y[n-1] + alpha * (x[n] - y[n-1])
    rc = 1.0 / (2.0 * math.pi * cutoff_hz)
    dt = 1.0 / float(fr)
    alpha = dt / (rc + dt)
    prev = [0.0] * max(1, nch)
    for i, v in enumerate(samples):
        ch = i % nch
        x = float(v)
        y = prev[ch] + alpha * (x - prev[ch])
        prev[ch] = y
        nv = int(y)
        if nv > 32767:
            nv = 32767
        elif nv < -32768:
            nv = -32768
        samples[i] = nv


def normalize_mp3_peak(
    mp3_path: Path,
    *,
    target_peak: float = 0.70,
    min_peak: float = 0.08,
    max_gain: float = 12.0,
    hiss_cut_hz: float | None = None,
    label: str = "audio",
) -> float:
    """
    Peak-normalize an MP3 in place when it is too quiet.

    Returns the applied linear gain (1.0 = unchanged).
    Large boosts apply a low-pass (hiss_cut_hz) so we don't turn the noise
    floor into harsh treble — that was the “weird audio” in the scene editor.
    """
    target_peak = max(0.1, min(0.99, float(target_peak)))
    min_peak = max(0.01, min(target_peak, float(min_peak)))
    max_gain = max(1.0, min(20.0, float(max_gain)))

    with tempfile.TemporaryDirectory() as tmp:
        wav_in = Path(tmp) / "in.wav"
        wav_out = Path(tmp) / "out.wav"
        decode_mp3_to_wav(mp3_path, wav_in)
        with wave.open(str(wav_in), "rb") as wf:
            nch = wf.getnchannels()
            sw = wf.getsampwidth()
            fr = wf.getframerate()
            nframes = wf.getnframes()
            raw = wf.readframes(nframes)
        if sw != 2:
            raise RuntimeError(f"{label}: expected 16-bit WAV for normalize")
        samples = array.array("h")
        samples.frombytes(raw)
        if not samples:
            raise RuntimeError(f"{label}: empty WAV after decode")
        peak = max(abs(int(v)) for v in samples)
        peak_f = peak / 32767.0
        if peak_f >= min_peak:
            print(
                f"  [{label}-loudness] peak={peak_f:.3f} (ok, no boost)",
                flush=True,
            )
            return 1.0
        if peak <= 0:
            raise RuntimeError(f"{label}: decoded audio is pure silence")
        needed = (target_peak * 32767.0) / float(peak)
        if needed > max_gain:
            raise RuntimeError(
                f"{label}: peak only {peak_f:.4f} full-scale — would need "
                f"×{needed:.0f} boost (cap ×{max_gain:.0f}). Refusing; "
                f"regenerate instead."
            )
        gain = needed
        # Cut hiss before/with boost when the bed was extremely quiet.
        cut = hiss_cut_hz
        if cut is None and gain >= 4.0:
            cut = 5500.0
        if cut is not None and cut > 0:
            _apply_one_pole_lowpass_inplace(samples, nch, fr, cut)
            # Recompute peak after filter (may drop).
            peak = max(abs(int(v)) for v in samples) or peak
            gain = min(max_gain, (target_peak * 32767.0) / float(peak))
        for i, v in enumerate(samples):
            nv = int(v * gain)
            if nv > 32767:
                nv = 32767
            elif nv < -32768:
                nv = -32768
            samples[i] = nv
        with wave.open(str(wav_out), "wb") as wf:
            wf.setnchannels(nch)
            wf.setsampwidth(2)
            wf.setframerate(fr)
            wf.writeframes(samples.tobytes())
        wav_to_mp3(wav_out, mp3_path)
        print(
            f"  [{label}-loudness] peak was {peak_f:.4f}; "
            f"boosted ×{gain:.1f} → target {target_peak:.2f}"
            + (f" (lpf {cut:.0f} Hz)" if cut else ""),
            flush=True,
        )
        return gain


def extract_audio_mp3_from_video(video_path: Path, out_mp3: Path) -> None:
    """Pull the audio stream from an mp4 into a loop-friendly mono/stereo MP3."""
    ffmpeg = resolve_ffmpeg_exe()
    out_mp3.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        ffmpeg,
        "-y",
        "-i",
        str(video_path),
        "-vn",
        "-acodec",
        "libmp3lame",
        "-q:a",
        "2",
        "-ar",
        "44100",
        str(out_mp3),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"ffmpeg extract failed (rc={result.returncode}) using {ffmpeg}: "
            f"{(result.stderr or result.stdout)[-800:]}"
        )
    validate_audio_mp3(out_mp3, min_bytes=4000, label="ffmpeg ambient extract")


def render_ambient_via_xai_video(
    api_key: str,
    prompt: str,
    out_path: Path,
    *,
    image_path: Path | None = None,
    duration: int = 10,
) -> str:
    """
    Grok Imagine Video (with native audio) → ffmpeg extract → ambient MP3.
    Prefer image-to-video when a scene plate exists so ambience matches the room.
    """
    ambient_prompt = (
        "Generate continuous diegetic ENVIRONMENTAL AUDIO only for this game scene. "
        "Hard rules: no spoken dialogue, no whispering, no humming, no singing, "
        "no narrator, no melodic music score, no UI beeps. "
        "Seamless loopable room bed: natural room tone, activity, distant bleed. "
        "Keep the camera nearly still so sound is primary.\n\n"
        f"{prompt}"
    )
    _url, mp4_path = generate_imagine_video(
        api_key,
        ambient_prompt,
        image_path=image_path,
        duration=duration,
        aspect_ratio="16:9",
        resolution="720p",
    )
    try:
        # Keep a debug copy under .authoring when writing into resources/audio/.
        # out_path like resources/audio/ambient/foo.mp3 → resources/.authoring/
        parts = list(out_path.parts)
        if "resources" in parts:
            idx = parts.index("resources")
            authoring = Path(*parts[: idx + 1]) / ".authoring"
            authoring.mkdir(parents=True, exist_ok=True)
            debug_mp4 = authoring / f"{out_path.stem}_ambient_source.mp4"
            debug_mp4.write_bytes(mp4_path.read_bytes())
            print(f"  [video] kept source clip {debug_mp4}")
        extract_audio_mp3_from_video(mp4_path, out_path)
    finally:
        mp4_path.unlink(missing_ok=True)
    return "xai_video_extract_v1"


def render_ambient_local_layers(api_key: str, prompt: str, out_path: Path) -> str:
    """Chat layer plan + LocalLayerSynth (offline-capable fallback)."""
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
        validate_audio_mp3(out_path, min_bytes=2000, label="local ambient")
    return backend


def resolve_music_preset_key(job: dict) -> str:
    preset_key = str(job.get("musicStylePreset") or job.get("stylePreset") or "").strip()
    if preset_key in MUSIC_STYLE_PRESETS:
        return preset_key
    hint = (
        str(job.get("outPath") or "")
        + " "
        + str(job.get("itemId") or "")
        + " "
        + str(job.get("prompt") or "")
    ).lower()
    if "saloon" in hint or "pub" in hint or "blackjack" in hint:
        return "saloon_piano"
    if "trail" in hint or "alpine" in hint or "ridge" in hint:
        return "trail_folk"
    if "cabin" in hint or "bedroom" in hint:
        return "cabin_hearth"
    if "mine" in hint or "camp" in hint:
        return "mining_camp"
    if "title" in hint or "menu" in hint:
        return "scarlet_whispers"
    if "tragic" in hint or "violin" in hint or "scarlet" in hint or "mourn" in hint:
        return "scarlet_whispers"
    return "cabin_hearth"


def _strip_visual_prompt_noise(scene_bits: str) -> str:
    """Drop image/world casting blocks that poison music generation."""
    text = (scene_bits or "").strip()
    if not text:
        return ""
    # Cut at known visual constraint headers from the editor.
    for marker in (
        "World / casting / tone constraints",
        "painterly realistic light",
        "no UI text",
        "period-accurate clothing",
    ):
        idx = text.find(marker)
        if idx > 0 and marker.startswith("World"):
            text = text[:idx].strip()
        elif marker.startswith("World") and idx == 0:
            # Whole prompt is constraints — try to keep only Scene overview tail.
            so = text.find("Scene overview:")
            if so >= 0:
                text = text[so:]
            else:
                text = ""
            break
    # Prefer "Scene overview" / "Scene mood" sections when present.
    for header in ("Scene mood", "Scene overview:", "Scene overview"):
        idx = text.find(header)
        if idx >= 0:
            text = text[idx:]
            break
    # Drop leftover bullet art-direction lines.
    lines = []
    for line in text.splitlines():
        low = line.strip().lower()
        if not low:
            continue
        if low.startswith("- ") and any(
            bad in low
            for bad in (
                "clothing",
                "painterly",
                "ui text",
                "modern objects",
                "casting",
                "tools",
            )
        ):
            continue
        lines.append(line.strip())
    text = " ".join(lines)
    if len(text) > 400:
        text = text[:400].rsplit(" ", 1)[0] + "…"
    return text.strip()


def build_period_music_prompt(job: dict) -> str:
    """Fallback text prompt when composition_plan is unavailable."""
    preset_key = resolve_music_preset_key(job)
    style = MUSIC_STYLE_PRESETS.get(preset_key, MUSIC_STYLE_PRESETS["cabin_hearth"])
    scene_bits = _strip_visual_prompt_noise(str(job.get("prompt") or ""))
    pos = ", ".join(style["positive"][:6])
    neg = ", ".join(style["negative"][:8])
    mood = f"\nScene mood: {scene_bits}" if scene_bits else ""
    return (
        f"Instrumental only {style['label']} game underscore bed. "
        f"No vocals, no lyrics, no choir, no humming.\n"
        f"Setting: late-1890s Colorado high-country frontier detective adventure.\n"
        f"Style: {pos}. Tempo about {style['bpm']}.\n"
        f"Avoid: {neg}.\n"
        f"Seamless loopable acoustic period bed, moderate dynamics.{mood}"
    )


def build_music_composition_plan(job: dict, length_ms: int) -> dict:
    """ElevenLabs v2/v2.5 composition plan — stronger style control than free prompt."""
    preset_key = resolve_music_preset_key(job)
    style = MUSIC_STYLE_PRESETS.get(preset_key, MUSIC_STYLE_PRESETS["cabin_hearth"])
    scene_bits = _strip_visual_prompt_noise(str(job.get("prompt") or ""))
    length_ms = max(10000, min(60000, int(length_ms)))
    # Single loopable bed chunk. force_instrumental is prompt-mode only; encode
    # instrumental intent in styles + section cue instead.
    positive = list(style["positive"])
    if style["bpm"] not in positive:
        positive.append(style["bpm"])
    if scene_bits:
        # Short mood tag only — keep styles English and musical.
        positive.append(f"mood: {scene_bits[:120]}")
    text = f"[Instrumental Bed]\n{style['cue']}"
    return {
        "chunks": [
            {
                "text": text,
                "duration_ms": length_ms,
                "positive_styles": positive[:50],
                "negative_styles": list(style["negative"])[:50],
                "context_adherence": "high",
            }
        ]
    }


def render_music_via_elevenlabs(
    api_key: str,
    prompt: str,
    out_path: Path,
    *,
    length_ms: int = 24000,
    model_id: str = "music_v2_5",
    job: dict | None = None,
) -> str:
    """
    ElevenLabs Music compose → MP3 bed.
    Prefers a composition_plan (v2.5) for period presets; falls back to prompt
    + force_instrumental. Never falls back to procedural pads.
    """
    if not api_key:
        raise RuntimeError(
            "Missing ELEVENLABS_API_KEY for generate_music. "
            "Export ELEVENLABS_API_KEY or place it in "
            "~/.config/highline-ridge/elevenlabs_api_key."
        )
    length_ms = max(8000, min(60000, int(length_ms)))
    # v2.5 defaults to 48 kHz in auto mode; request that explicitly for quality.
    output_format = "mp3_48000_192"

    def _post(payload: dict) -> tuple[bytes, str]:
        req = urllib.request.Request(
            f"https://api.elevenlabs.io/v1/music?output_format={output_format}",
            data=json.dumps(payload).encode("utf-8"),
            headers={
                "xi-api-key": api_key,
                "Content-Type": "application/json",
                "Accept": "audio/mpeg",
            },
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=300) as resp:
            return resp.read(), (resp.headers.get("Content-Type") or "").lower()

    audio = b""
    content_type = ""
    mode = ""
    if job is not None:
        plan = build_music_composition_plan(job, length_ms)
        print(
            f"  [music-plan] preset={resolve_music_preset_key(job)} "
            f"chunks={len(plan.get('chunks') or [])} "
            f"dur_ms={length_ms}",
            flush=True,
        )
        try:
            audio, content_type = _post(
                {"composition_plan": plan, "model_id": model_id}
            )
            mode = "composition_plan"
        except urllib.error.HTTPError as plan_exc:
            detail = plan_exc.read().decode("utf-8", errors="replace")
            try:
                err_obj = json.loads(detail)
            except json.JSONDecodeError:
                err_obj = {}
            detail_obj = err_obj.get("detail")
            suggestion = err_obj.get("composition_plan_suggestion")
            if suggestion is None and isinstance(detail_obj, dict):
                suggestion = detail_obj.get("composition_plan_suggestion")
            if suggestion:
                print("  [music-plan] retrying with API suggestion", flush=True)
                try:
                    audio, content_type = _post(
                        {"composition_plan": suggestion, "model_id": model_id}
                    )
                    mode = "composition_plan_suggested"
                except urllib.error.HTTPError as sug_exc:
                    sug_detail = sug_exc.read().decode("utf-8", errors="replace")
                    print(
                        f"  [music-plan] suggestion failed HTTP {sug_exc.code}; "
                        f"falling back to prompt ({sug_detail[:160]})",
                        flush=True,
                    )
            else:
                print(
                    f"  [music-plan] plan failed HTTP {plan_exc.code}; "
                    f"falling back to prompt ({detail[:180]})",
                    flush=True,
                )

    if not audio:
        if job is not None:
            prompt = build_period_music_prompt(job)
        print(
            f"  [music-prompt] {prompt[:220].replace(chr(10), ' / ')}…",
            flush=True,
        )
        try:
            audio, content_type = _post(
                {
                    "prompt": prompt,
                    "music_length_ms": length_ms,
                    "model_id": model_id,
                    "force_instrumental": True,
                }
            )
            mode = "prompt"
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise RuntimeError(
                f"ElevenLabs music HTTP {exc.code}: {detail[:1200]}"
            ) from exc

    if not audio or len(audio) < 4000:
        raise RuntimeError(
            f"ElevenLabs music returned empty/tiny body ({len(audio) if audio else 0} bytes)"
        )
    if "json" in content_type or audio[:1] == b"{":
        raise RuntimeError(
            f"ElevenLabs music returned non-audio payload: {audio[:400]!r}"
        )
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(audio)
    validate_audio_mp3(out_path, min_bytes=4000, label="elevenlabs music")
    return f"elevenlabs_{model_id}_{mode}"


def resolve_ambient_image_path(asset_root: Path, out_path: Path, job: dict | None = None) -> Path | None:
    """Prefer job imagePath; else resources/images/<stem>.png next to ambient out."""
    if job:
        rel = str(job.get("imagePath") or job.get("image") or "").strip()
        if rel:
            candidate = asset_root / rel
            if candidate.is_file():
                return candidate
    stem = out_path.stem  # saloon_kitchen
    for name in (f"{stem}.png", f"{stem}.jpg", f"{stem}.jpeg"):
        candidate = asset_root / "resources" / "images" / name
        if candidate.is_file():
            return candidate
    return None


def build_ambient_sfx_prompt(job: dict | None, prompt: str) -> str:
    """ElevenLabs looping ambient prompt — max 450 chars (API hard limit)."""
    raw = _strip_visual_prompt_noise(str((job or {}).get("prompt") or prompt or ""))
    for header in ("Scene overview:", "Scene overview", "Scene mood"):
        idx = raw.find(header)
        if idx >= 0:
            raw = raw[idx + len(header) :].lstrip(" :\n")
            break
    # Drop leftover rule lines from the C++ job wrapper.
    keep = []
    for line in raw.splitlines():
        low = line.strip().lower()
        if not low:
            continue
        if low.startswith("hard rules") or low.startswith("seamless looping"):
            continue
        if low.startswith("diegetic ambient"):
            continue
        keep.append(line.strip())
    place = " ".join(keep) if keep else raw
    place = " ".join(place.split())
    # Prefer concrete sonic language; visual scene prose yields thin/noisy beds.
    sonic = _ambient_sonic_from_place(place)
    text = (
        f"Looping outdoor ambient soundscape, clearly audible moderate level. "
        f"{sonic} Continuous even bed, no sudden events. No music, melody, "
        f"instruments, speech, footsteps, or UI beeps."
    )
    return text[:450]


def _ambient_sonic_from_place(place: str) -> str:
    """Map scene overview text to concrete Foley-style ambient descriptors."""
    low = (place or "").lower()
    if any(k in low for k in ("kitchen", "stove", "pantry")):
        return (
            "Quiet wooden kitchen room tone, soft distant pot clinks, gentle "
            "steam hiss, muffled voices far away."
        )
    if any(k in low for k in ("saloon", "bar", "pub", "crowd")):
        return (
            "Muffled saloon room tone, distant low murmur of patrons, soft "
            "wood creaks, no piano lead."
        )
    if any(k in low for k in ("cabin", "hearth", "bedroom", "interior")):
        return (
            "Quiet wooden cabin interior room tone, soft fire crackle far "
            "away, gentle wind against walls."
        )
    if any(k in low for k in ("mine", "camp", "shaft")):
        return (
            "Open mining camp ambience, soft wind, distant quiet activity, "
            "dry dusty air tone."
        )
    if any(k in low for k in ("trail", "alpine", "ridge", "forest", "mountain", "wind")):
        return (
            "High-country mountain trail ambience: soft steady wind through "
            "pine trees, distant forest hush, open air."
        )
    if place:
        clipped = place[:160].rsplit(" ", 1)[0] if len(place) > 160 else place
        return f"Environmental ambience for: {clipped}."
    return "Soft outdoor nature ambience, gentle wind, distant forest."


def _elevenlabs_sound_post(
    api_key: str,
    text: str,
    *,
    duration_seconds: float,
    loop: bool,
    prompt_influence: float,
) -> bytes:
    payload = {
        "text": text[:450],
        "duration_seconds": duration_seconds,
        "loop": loop,
        "prompt_influence": prompt_influence,
        "model_id": "eleven_text_to_sound_v2",
    }
    req = urllib.request.Request(
        "https://api.elevenlabs.io/v1/sound-generation?output_format=mp3_44100_128",
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "xi-api-key": api_key,
            "Content-Type": "application/json",
            "Accept": "audio/mpeg",
        },
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=180) as resp:
        audio = resp.read()
        content_type = (resp.headers.get("Content-Type") or "").lower()
    if not audio or len(audio) < 2000:
        raise RuntimeError(
            f"ElevenLabs ambient returned empty/tiny body "
            f"({len(audio) if audio else 0} bytes)"
        )
    if "json" in content_type or audio[:1] == b"{":
        raise RuntimeError(
            f"ElevenLabs ambient returned non-audio payload: {audio[:400]!r}"
        )
    return audio


def render_ambient_via_elevenlabs(
    api_key: str,
    prompt: str,
    out_path: Path,
    *,
    duration_seconds: float = 16.0,
) -> str:
    """
    ElevenLabs Text-to-Sound Effects with loop=true → seamless ambient MP3 bed.
    Retries once with a louder concrete prompt if the first take is too quiet
    to normalize safely (avoids ×40 hiss boosts).
    """
    if not api_key:
        raise RuntimeError(
            "Missing ELEVENLABS_API_KEY for ambient. "
            "Write ~/.config/highline-ridge/elevenlabs_api_key "
            "(Sound Effects scope required)."
        )
    duration_seconds = max(4.0, min(30.0, float(duration_seconds)))
    out_path = out_path if out_path.suffix.lower() == ".mp3" else out_path.with_suffix(".mp3")
    out_path.parent.mkdir(parents=True, exist_ok=True)

    # loop=True often returns near-silent beds from ElevenLabs; non-loop takes
    # are usually louder and the game already loops MusicStream playback.
    loud_prompt = (
        "Loud clear outdoor mountain wind through pine trees, strong continuous "
        "breeze, forest ambience, full level nature bed, no music no speech "
        "no footsteps no birds as melody."
    )
    attempts: list[tuple[str, float, bool]] = [
        (prompt, 0.6, True),
        (prompt, 0.75, False),
        (loud_prompt, 0.8, False),
    ]
    last_err: Exception | None = None
    for i, (text, influence, do_loop) in enumerate(attempts):
        try:
            print(
                f"  [ambient-try {i + 1}/{len(attempts)}] "
                f"loop={do_loop} influence={influence} "
                f"text={text[:100].replace(chr(10), ' ')}…",
                flush=True,
            )
            audio = _elevenlabs_sound_post(
                api_key,
                text,
                duration_seconds=duration_seconds,
                loop=do_loop,
                prompt_influence=influence,
            )
            out_path.write_bytes(audio)
            validate_audio_mp3(out_path, min_bytes=2000, label="elevenlabs ambient")
            normalize_mp3_peak(
                out_path,
                target_peak=0.55,
                min_peak=0.10,
                max_gain=18.0,
                hiss_cut_hz=5000.0,
                label="elevenlabs ambient",
            )
            return (
                "elevenlabs_sound_loop_v2"
                if do_loop
                else "elevenlabs_sound_v2_editor_loop"
            )
        except Exception as exc:  # noqa: BLE001
            last_err = exc
            print(f"  [ambient-try {i + 1}] failed: {exc}", flush=True)
    raise RuntimeError(f"ElevenLabs ambient failed after retries: {last_err}")


def render_ambient_backend(
    api_key: str,
    prompt: str,
    out_path: Path,
    *,
    image_path: Path | None = None,
    elevenlabs_key: str | None = None,
    job: dict | None = None,
) -> str:
    """
    AmbientAudioBackend entry point.

    TIMBERLINE_AUDIO_BACKEND:
      auto       — ElevenLabs looping SFX, else local layers (default)
      elevenlabs — require ElevenLabs looping SFX
      xai_video  — legacy Imagine Video extract (needs working ffmpeg)
      local      — procedural layered synth only
    """
    mode = os.environ.get("TIMBERLINE_AUDIO_BACKEND", "auto").strip().lower() or "auto"
    if mode not in {"auto", "elevenlabs", "xai_video", "local"}:
        print(f"  [ambient] unknown TIMBERLINE_AUDIO_BACKEND={mode!r}; using auto")
        mode = "auto"

    el_key = (elevenlabs_key or "").strip() or None
    if not el_key and job is not None:
        el_key = str(job.get("elevenLabsApiKey") or "").strip() or None

    if mode in {"auto", "elevenlabs"} and el_key:
        amb_prompt = build_ambient_sfx_prompt(job, prompt)
        print(
            f"  [ambient-prompt] {amb_prompt[:220].replace(chr(10), ' / ')}…",
            flush=True,
        )
        # With an ElevenLabs key, do NOT silently fall back to procedural
        # local layers (those sound like garbage). Fail loud instead.
        return render_ambient_via_elevenlabs(el_key, amb_prompt, out_path)

    if mode == "elevenlabs" and not el_key:
        raise RuntimeError(
            "TIMBERLINE_AUDIO_BACKEND=elevenlabs (or auto) needs "
            "ELEVENLABS_API_KEY / ~/.config/highline-ridge/elevenlabs_api_key "
            "with Sound Effects scope."
        )

    if mode == "xai_video" or (mode == "auto" and api_key and not el_key):
        # Legacy path: only when explicitly requested, or auto with no EL key.
        try:
            print(
                f"  [ambient] trying xai_video"
                + (
                    f" with image {image_path.name}"
                    if image_path
                    else " (text-to-video)"
                ),
                flush=True,
            )
            return render_ambient_via_xai_video(
                api_key,
                prompt,
                out_path,
                image_path=image_path,
                duration=10,
            )
        except Exception as exc:  # noqa: BLE001
            if mode == "xai_video":
                raise
            print(
                f"  [ambient] xai_video failed ({exc}); falling back to local layers",
                flush=True,
            )

    if mode == "local" or mode == "auto":
        return render_ambient_local_layers(api_key or "", prompt, out_path)

    return render_ambient_local_layers(api_key or "", prompt, out_path)




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


def soften_imagine_prompt(prompt: str) -> str:
    """Rewrite gore/violence phrasing for Imagine; keep period atmosphere.

    Game JSON keeps full forensic prose — only the Imagine API prompt is softened
    so scenes like sawtooth_ridge_body can still generate plates.
    """
    import re

    text = prompt
    replacements = [
        (r"\bblood\b", "dark staining"),
        (r"\bbloody\b", "stained"),
        (r"\bgunshot wound\b", "bandaged injury"),
        (r"\bwound channel\b", "bandage packing"),
        (r"\bwound\b", "injury"),
        (r"\bwounds\b", "injuries"),
        (r"\bbludgeon(?:ing|ed)?\b", "struck"),
        (r"\bskull has been severly damaged\b", "hat covers his head"),
        (r"\bskull has been severely damaged\b", "hat covers his head"),
        (r"\bseverly damaged by\b", "hidden by"),
        (r"\bcorpse\b", "fallen traveler"),
        (r"\bthe body\b", "the fallen man"),
        (r"\bThe body\b", "The fallen man"),
        (r"\bdead body\b", "fallen traveler"),
        (r"\btime of death\b", "how long he has lain here"),
        (r"\bcause of death\b", "what happened"),
        (r"\bkilled him\b", "brought him down"),
        (r"\bdie[sd]?\b", "fell"),
        (r"\bpeak rigidity\b", "stiff with cold"),
        (r"\bfrozen stiff to the wool\b", "iced to the wool"),
        (r"\bmeet cold metal\b", "feel something hard"),
        (r"\bbullet that struck him\b", "round that hit him"),
        (r"\bburied in the drift\b", "half-covered by the drift"),
        (r"\bfield examination\b", "careful look"),
    ]
    for pattern, repl in replacements:
        text = re.sub(pattern, repl, text, flags=re.IGNORECASE)

    # Drop the most forensic examine paragraphs for image jobs — keep overview.
    # If "Examine / detail notes:" is present, truncate after a short preview.
    marker = "Examine / detail notes:"
    idx = text.find(marker)
    if idx >= 0:
        head = text[:idx].rstrip()
        # Keep a mild one-liner instead of the full autopsy-style notes.
        text = (
            head
            + " A man in rancher's clothes lies still in the snow, hat covering "
            "his face, no gore visible. Respectful distance."
        )

    suffix = (
        " Strict visual rules for this image: PG-13, no gore, no blood, no open "
        "wounds, no weapons mid-strike, no graphic injury. Show snow, ridge, "
        "period clothing, and a still figure at a distance if needed."
    )
    if "no gore" not in text.lower():
        text = text.rstrip() + suffix
    return text


def generate_image(
    api_key: str,
    prompt: str,
    out_path: Path,
    aspect_ratio: str,
    *,
    resolution: str = "2k",
    model: str = "grok-imagine-image-2.0",
    soften: bool = True,
) -> None:
    """Generate a scene/item plate via xAI Imagine.

    Scene masters default to 16:9 at resolution=2k (API max; ~2K class, not 4K).
    Icons stay 1:1; callers may pass resolution=\"1k\" to save cost.
    When soften=True (default), gore/forensic phrasing is rewritten for Imagine.
    """
    out_path.parent.mkdir(parents=True, exist_ok=True)
    safe_prompt = soften_imagine_prompt(prompt) if soften else prompt
    payload = {
        "model": model,
        "prompt": safe_prompt,
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
        # Surface Imagine moderation / auth clearly for the editor status line.
        hint = ""
        try:
            parsed = json.loads(err)
            code = str(parsed.get("code") or "")
            if "content-moderated" in code or "moderated" in err.lower():
                hint = (
                    " (content moderation — the runner already softens gore; try a "
                    "shorter scene overview without forensic wound detail, then "
                    "Generate again)"
                )
            elif "incorrect api key" in err.lower() or "invalid" in code.lower():
                hint = (
                    " (bad API key — paste a valid xAI key in AI Assist, or update "
                    "~/.config/highline-ridge/xai_api_key)"
                )
        except Exception:
            pass
        raise RuntimeError(f"Image API HTTP {exc.code}: {err}{hint}") from exc

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
    """Legacy procedural SFX (beeps). Prefer render_sfx_via_elevenlabs."""
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


def build_sfx_prompt(job: dict, action: str) -> str:
    """Tighten job prompt into an ElevenLabs sound-generation text."""
    raw = str(job.get("prompt") or "").strip()
    raw = " ".join(raw.split())
    if len(raw) > 420:
        raw = raw[:420].rsplit(" ", 1)[0] + "…"
    action = (action or "examine").lower()
    if raw and ("foley" in raw.lower() or "sound" in raw.lower() or "sfx" in raw.lower()):
        base = raw
    elif action == "examine":
        base = (
            "Short soft Foley examine / pickup sound for a period adventure game item. "
            + (raw or "small wooden or metal object handled carefully")
        )
    elif action == "use":
        base = (
            "Short Foley use / activate interaction sound for a period adventure game item. "
            + (raw or "mechanical click or soft impact")
        )
    elif action == "enter":
        base = (
            "Short Foley wood door open / enter room sound, 1890s frontier cabin or saloon. "
            + (raw or "soft latch and hinge")
        )
    elif action == "exit":
        base = (
            "Short Foley wood door close / leave room sound, 1890s frontier. "
            + (raw or "soft latch click")
        )
    else:
        base = raw or "Short soft period game Foley one-shot sound effect"
    return (
        f"{base}. One-shot sound effect only. No music, no melody, no dialogue, "
        "no narrator, no UI beep, no modern electronics."
    )


def sfx_duration_seconds(action: str) -> float:
    action = (action or "").lower()
    if action in ("enter", "exit"):
        return 1.2
    if action == "use":
        return 0.9
    return 0.7  # examine


def render_sfx_via_elevenlabs(
    api_key: str,
    prompt: str,
    out_path: Path,
    *,
    action: str = "examine",
    duration_seconds: float | None = None,
) -> str:
    """
    ElevenLabs Text-to-Sound Effects → MP3.
    POST /v1/sound-generation  (model eleven_text_to_sound_v2)
    """
    if not api_key:
        raise RuntimeError(
            "Missing ELEVENLABS_API_KEY for SFX. "
            "Export ELEVENLABS_API_KEY or place it in "
            "~/.config/highline-ridge/elevenlabs_api_key."
        )
    dur = duration_seconds if duration_seconds is not None else sfx_duration_seconds(action)
    dur = max(0.5, min(5.0, float(dur)))
    payload = {
        "text": prompt,
        "duration_seconds": dur,
        "prompt_influence": 0.45,
        "model_id": "eleven_text_to_sound_v2",
    }
    req = urllib.request.Request(
        "https://api.elevenlabs.io/v1/sound-generation?output_format=mp3_44100_128",
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "xi-api-key": api_key,
            "Content-Type": "application/json",
            "Accept": "audio/mpeg",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            audio = resp.read()
            content_type = (resp.headers.get("Content-Type") or "").lower()
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(
            f"ElevenLabs sound-generation HTTP {exc.code}: {detail[:1200]}"
        ) from exc
    if not audio or len(audio) < 800:
        raise RuntimeError(
            f"ElevenLabs SFX returned empty/tiny body ({len(audio) if audio else 0} bytes)"
        )
    if "json" in content_type or audio[:1] == b"{":
        raise RuntimeError(
            f"ElevenLabs SFX returned non-audio payload: {audio[:400]!r}"
        )
    out_path = out_path if out_path.suffix.lower() == ".mp3" else out_path.with_suffix(".mp3")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(audio)
    validate_audio_mp3(out_path, min_bytes=800, label="elevenlabs sfx")
    normalize_mp3_peak(
        out_path,
        target_peak=0.85,
        min_peak=0.12,
        max_gain=8.0,
        label="elevenlabs sfx",
    )
    return "elevenlabs_text_to_sound_v2"


def process_job(
    api_key: str,
    asset_root: Path,
    job: dict,
    *,
    backup_rotate: bool = False,
    elevenlabs_key: str | None = None,
) -> str:
    jtype = job.get("type", "")
    out_rel = job.get("outPath", "")
    prompt = job.get("prompt", "")
    action = job.get("action", "examine")
    if not out_rel:
        raise ValueError(f"Job missing outPath: {job}")

    out_norm = str(out_rel).replace("\\", "/")
    item_id = str(job.get("itemId") or "")

    # Legacy tokens → real files under resources/.authoring/
    if out_norm in LOGICAL_OUT_PATHS:
        leaf = LOGICAL_OUT_PATHS[out_norm]
        prefix = item_id if item_id else "item"
        out_rel = f"resources/.authoring/{prefix}_{leaf}"
        out_norm = out_rel
        job["outPath"] = out_rel

    # Refuse garbage outPaths (e.g. a pasted API key mistaken for a path).
    if not out_norm.startswith("resources/"):
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
        soften = bool(job.get("softenPrompt", True))
        print(
            f"  [image] {out_path.relative_to(asset_root)} "
            f"({aspect}, {resolution}, {model}"
            f"{'' if soften else ', soften=off'}) …",
            flush=True,
        )
        generate_image(
            api_key,
            prompt,
            out_path,
            aspect,
            resolution=resolution,
            model=model,
            soften=soften,
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
            image_path = resolve_ambient_image_path(asset_root, out_path, job)
            el_key = resolve_elevenlabs_api_key(
                asset_root,
                (elevenlabs_key or str(job.get("elevenLabsApiKey") or "") or None),
            )
            backend = render_ambient_backend(
                api_key,
                prompt,
                out_path,
                image_path=image_path,
                elevenlabs_key=el_key,
                job=job,
            )
            print(f"  [ambient-backend] {backend}", flush=True)
            if backend.startswith("local_layers"):
                print(
                    "  [ambient-note] local procedural fallback — set "
                    "ElevenLabs key (Sound Effects) or TIMBERLINE_AUDIO_BACKEND="
                    "elevenlabs after fixing permissions",
                    flush=True,
                )
        elif jtype == "generate_music":
            el_key = resolve_elevenlabs_api_key(
                asset_root,
                (elevenlabs_key or str(job.get("elevenLabsApiKey") or "") or None),
            )
            music_prompt = build_period_music_prompt(job)
            length_ms = int(job.get("musicLengthMs") or job.get("lengthMs") or 24000)
            backend = render_music_via_elevenlabs(
                el_key,
                music_prompt,
                out_path,
                length_ms=length_ms,
                job=job,
            )
            print(f"  [music-backend] {backend}", flush=True)
        else:
            # examine / use / enter / exit — ElevenLabs sound-generation.
            # TIMBERLINE_SFX_BACKEND=procedural forces the old beep fallback.
            sfx_backend = (os.environ.get("TIMBERLINE_SFX_BACKEND") or "elevenlabs").strip().lower()
            if sfx_backend in ("procedural", "local", "beep"):
                print("  [sfx-backend] procedural (TIMBERLINE_SFX_BACKEND)", flush=True)
                generate_sound(out_path, action)
            else:
                el_key = resolve_elevenlabs_api_key(
                    asset_root,
                    (elevenlabs_key or str(job.get("elevenLabsApiKey") or "") or None),
                )
                if not el_key:
                    raise RuntimeError(
                        "Missing ELEVENLABS_API_KEY for SFX generation. "
                        "Options → Configure API keys, or write "
                        "~/.config/highline-ridge/elevenlabs_api_key "
                        "(Sound Effects + Music scopes). "
                        "Set TIMBERLINE_SFX_BACKEND=procedural only as emergency fallback."
                    )
                sfx_prompt = build_sfx_prompt(job, action)
                print(
                    f"  [sfx-prompt] {sfx_prompt[:200].replace(chr(10), ' / ')}…",
                    flush=True,
                )
                backend = render_sfx_via_elevenlabs(
                    el_key, sfx_prompt, out_path, action=action
                )
                print(f"  [sfx-backend] {backend}", flush=True)
        xz = xz_compress(out_path, remove_source=False)
        print(f"  [ok] wrote {out_path.name} + {xz.name}")
        return str(out_path.relative_to(asset_root))

    if jtype in SCENE_TTS_TEXT_TYPES or jtype in ITEM_TEXT_TYPES:
        if not api_key:
            raise RuntimeError(
                "Missing XAI_API_KEY for TTS/text generation. "
                "Export XAI_API_KEY or place key in ~/.config/highline-ridge/xai_api_key."
            )
        label = "tts-text" if "tts" in jtype else "text"
        print(f"  [{label}] {jtype} …")
        text = generate_chat_text(api_key, prompt)
        job["resultText"] = text
        out_path.parent.mkdir(parents=True, exist_ok=True)
        if not out_path.suffix:
            out_path = out_path.with_suffix(".txt")
        out_path.write_text(text + "\n", encoding="utf-8")
        print(f"  [ok] wrote text ({len(text)} chars) → {out_path.name}")
        return str(out_path.relative_to(asset_root))

    print(f"  [skip] {jtype} (unknown job type)")
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
    parser.add_argument("--key", type=str, default=None, help="xAI API key override")
    parser.add_argument(
        "--elevenlabs-key",
        type=str,
        default=None,
        help="ElevenLabs API key override (music only)",
    )
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
    elevenlabs_key = resolve_elevenlabs_api_key(asset_root, args.elevenlabs_key)
    data = load_jobs(jobs_path)
    item_id = data.get("itemId", args.item_id or "?")
    jobs = data.get("jobs") or []
    backup_rotate = bool(data.get("backupRotate", False))
    print(f"Running {len(jobs)} authoring job(s) for {item_id}"
          + (" (backupRotate)" if backup_rotate else ""), flush=True)
    has_music = any(
        isinstance(j, dict) and j.get("type") == "generate_music" for j in jobs
    )
    if has_music and not elevenlabs_key:
        print(
            "  [warn] generate_music jobs present but no ELEVENLABS_API_KEY "
            "(~/.config/highline-ridge/elevenlabs_api_key)",
            flush=True,
        )

    errors: list[str] = []
    produced: list[str] = []
    for job in jobs:
        if not isinstance(job, dict):
            continue
        job["itemId"] = item_id
        try:
            rel = process_job(
                api_key,
                asset_root,
                job,
                backup_rotate=backup_rotate,
                elevenlabs_key=elevenlabs_key,
            )
            if rel:
                produced.append(rel)
        except Exception as exc:  # noqa: BLE001 — report per-job
            msg = f"{job.get('type', '?')}: {exc}"
            print(f"  [fail] {msg}", file=sys.stderr)
            errors.append(msg)

    # Persist resultText / rewritten outPaths alongside lastRun status.
    data["jobs"] = jobs
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
