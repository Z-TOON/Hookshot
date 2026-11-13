"""The margin of a temporal upscaler's inputs (gpu/vk/input_margin.h): the copies that repeat the scene's last column and row
into the part of the inputs no pass writes, which the upscaler's filters read all the same. Built as C++ and run, and the
chain is checked to clear its images when it makes them and to fill the margin before the engine reads them. No game, GPU
or upscaler SDK required.

python -I -B tests/test_input_margin.py [--cxx g++]
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
    with tempfile.TemporaryDirectory(prefix="input-margin-") as tmp:
        exe = Path(tmp) / "input-margin.exe"
        subprocess.run([args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", str(ROOT / "tests" / "test_input_margin.cpp"),
                        "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)

    chain = (ROOT / "gpu" / "vk" / "vk_temporal.cpp").read_text(encoding="utf-8")
    # (the images are cleared when they are made: the margin past kMargin, and an output no engine has written yet)
    make = chain[chain.index("bool make_image("):chain.index("void pad_input(")]
    assert "vkCmdClearColorImage(" in make, "the chain's images must be cleared when they are made"
    assert "VK_IMAGE_USAGE_TRANSFER_DST_BIT" in make and "VK_IMAGE_USAGE_TRANSFER_SRC_BIT" in make, \
        "a cleared and blitted image is a transfer's source and destination"
    # (the margin is filled after the last pass that writes the inputs, the actors' motion, and before the engine reads them)
    upscale = chain[chain.index("bool temporal_upscale("):]
    actors, pad, evaluate = upscale.index("actor_motion(cmd"), upscale.index("pad_input(cmd"), upscale.index("engine.e->evaluate(cmd")
    assert actors < pad < evaluate, "the margin is filled between the last write of the inputs and the engine's evaluation"
    for image in ("s.color", "s.depth", "s.motion"):
        assert "pad_input(cmd, %s, rw, rh)" % image in upscale, "%s's margin is not filled" % image
    between = upscale[pad:evaluate]
    assert "full_barrier(cmd)" in between, "the copies must be done before the engine reads the inputs"
    print("PASS: the chain clears its images and fills its inputs' margin before the engine reads them")


if __name__ == "__main__":
    main()
