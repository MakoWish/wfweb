"""Build and run the protocol-aware WSJT-X NetworkMessage unit test."""

import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def test_wsjtx_network_message_serialization(tmp_path):
    if not shutil.which("g++") or not shutil.which("pkg-config"):
        pytest.skip("C++ compiler/pkg-config unavailable")
    flags = subprocess.run(
        ["pkg-config", "--cflags", "--libs", "Qt5Core"],
        check=False,
        capture_output=True,
        text=True,
    )
    if flags.returncode:
        pytest.skip("Qt5Core development package unavailable")

    executable = tmp_path / "wsjtx_protocol_test"
    command = [
        "g++", "-std=c++17", "-fPIC",
        "-I", str(ROOT / "include"),
        str(ROOT / "tests" / "wsjtx_protocol_test.cpp"),
        str(ROOT / "src" / "wsjtxmessage.cpp"),
        *flags.stdout.split(),
        "-o", str(executable),
    ]
    subprocess.run(command, check=True, cwd=ROOT)
    subprocess.run([str(executable)], check=True, cwd=ROOT)
