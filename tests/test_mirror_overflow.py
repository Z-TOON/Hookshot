"""Compile and run the rules by which a busy pool the mirrors' budget refuses is made past it (gpu/vk/mirror_overflow.h; no
game or GPU required), and check that vk_memory.cpp's mirror_make uses them: a refusal is counted, a busy pool asks the
driver's count before it is made, a made mirror's count starts over, and RX_VK_MIRROR_OVERFLOW=0 turns it off.

python -I -B tests/test_mirror_overflow.py [--cxx g++]
"""
import argparse
import subprocess
import tempfile
from pathlib import Path

RUN_ALL_ARGS = ["--cxx", "{cxx}"]

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="mirror-overflow-") as tmp:
        exe = Path(tmp) / "mirror-overflow.exe"
        subprocess.run([args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", str(ROOT / "tests" / "test_mirror_overflow.cpp"),
                        "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    memory = (ROOT / "gpu" / "vk" / "vk_memory.cpp").read_text(encoding="utf-8")
    make = memory[memory.index("bool mirror_make(Pool& pool) {"):]
    make = make[:make.index("vkGetBufferDeviceAddress")]
    at = -1
    for needle in ("RX_VK_MIRROR_OVERFLOW", "mirror_overflow::note(p->refused, mirror_epoch)", "mirror_overflow::busy(",
                   "vram_usage(", "mirror_overflow::allowed(", "if (!over && (mirror_bytes + n > mirror_budget()",
                   "create_device_buffer(", "p->refused = {};"):
        nxt = make.find(needle, at + 1)
        assert nxt >= 0, "mirror_make no longer has, in this order: " + needle
        at = nxt
    assert "n <= (1ull << 30) && !tight" in make, "a pool over 1 GB, or one RX_VK_MIRROR_TIGHT refuses, is never made past the budget"
    print("PASS: mirror_make counts refusals and makes a busy pool's mirror past the budget by the rules")


if __name__ == "__main__":
    main()
