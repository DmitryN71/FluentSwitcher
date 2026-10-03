"""Makes the sounds of FluentSwitcher: bin_files/sounds/switch.wav (the layout switched) and fix.wav (text fixed).

Synthesized here, so they are the project's own (no sounds of other programs or of Windows): a click - a burst
of noise a millisecond and a half long ringing in a resonator, with a short low thump under it, some 40 ms in
all; the fix is two clicks. The engine (src/LayoutSound.h) plays them; users may put their own WAV files in the
sounds folder next to the program, also en.wav, ru.wav... for a sound of each language.

    python tools/make_sounds.py
"""
import math
import random
import struct
import wave
from pathlib import Path

RATE = 44100
OUT = Path(__file__).resolve().parent.parent / "bin_files" / "sounds"


def click(freq: float, ms: int = 40, seed: int = 1) -> list[float]:
    rnd = random.Random(seed)
    n = int(RATE * ms / 1000)
    r = math.exp(-math.pi * (freq / 5) / RATE)   # bandwidth: a fifth of the frequency
    a1, a2 = 2 * r * math.cos(2 * math.pi * freq / RATE), -r * r
    y1 = y2 = 0.0
    ring, thump = [], []
    for i in range(n):
        t = i / RATE
        x = rnd.uniform(-1, 1) * math.exp(-t / 0.0015)
        y = x + a1 * y1 + a2 * y2
        y2, y1 = y1, y
        ring.append(y)
        thump.append(math.sin(2 * math.pi * freq * 0.3 * t) * math.exp(-t / 0.005))
    top = max(abs(v) for v in ring)
    out = [0.8 * v / top + 0.35 * th for v, th in zip(ring, thump)]
    fade = int(RATE * 0.005)  # the last 5 ms to silence
    for i in range(fade):
        out[n - 1 - i] *= i / fade
    return out


def normalized(samples: list[float], peak: float = 0.6) -> list[float]:
    top = max(abs(v) for v in samples)
    return [v / top * peak for v in samples]


def write(name: str, samples: list[float]) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / name
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(round(s * 32767))) for s in samples))
    print(path, len(samples) * 1000 // RATE, "ms", path.stat().st_size, "bytes")


write("switch.wav", normalized(click(2600)))
gap = [0.0] * int(RATE * 0.045)
write("fix.wav", normalized(click(1800, seed=2) + gap + click(2600, seed=3)))
