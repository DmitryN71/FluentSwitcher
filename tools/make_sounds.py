"""Makes the sounds of a layout change: bin_files/sounds/en.wav, ru.wav and other.wav (any other language).

Synthesized here, so they are the project's own (no sounds of other programs or of Windows): a short soft
"plink" - a sine with a little of its octave and a click of an attack, fading out in about a tenth of a
second. English is higher, Russian lower, the others in between, so the ear tells which layout is on.
The engine (src/LayoutSound.h) plays <language>.wav, e.g. en.wav for en-US; users may put their own files
in the sounds folder next to the program.

    python tools/make_sounds.py
"""
import math
import struct
import wave
from pathlib import Path

RATE = 44100
OUT = Path(__file__).resolve().parent.parent / "bin_files" / "sounds"


def plink(freq: float, ms: int, peak: float = 0.5) -> list[float]:
    n = int(RATE * ms / 1000)
    attack = int(RATE * 0.003)  # 3 ms: no click from a jump, yet a crisp start
    out = []
    for i in range(n):
        t = i / RATE
        body = math.sin(2 * math.pi * freq * t) * math.exp(-t / 0.035)
        octave = 0.22 * math.sin(2 * math.pi * 2 * freq * t) * math.exp(-t / 0.015)
        fifth = 0.06 * math.sin(2 * math.pi * 3 * freq * t) * math.exp(-t / 0.008)
        v = body + octave + fifth
        if i < attack:
            v *= i / attack
        tail = n - i  # the last 10 ms fade to silence
        if tail < RATE * 0.010:
            v *= tail / (RATE * 0.010)
        out.append(v)
    top = max(abs(v) for v in out)
    return [v / top * peak for v in out]


def write(name: str, samples: list[float]) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / name
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(round(s * 32767))) for s in samples))
    print(path, len(samples) * 1000 // RATE, "ms", path.stat().st_size, "bytes")


write("en.wav", plink(1174.7, 100))   # D6
write("ru.wav", plink(698.5, 120))    # F5
write("other.wav", plink(880.0, 110)) # A5
