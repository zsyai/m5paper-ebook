#!/usr/bin/env python3
"""Builds the single-file release image for the M5Paper reader.

Merges bootloader, partition table, boot_app0 and the app into one image that
flashes at offset 0x0, so a single esptool command installs or upgrades the
reader from any previous state. The version comes from include/core/version.h.

Usage:
  python3 tools/package_release.py              # build, then merge
  python3 tools/package_release.py --skip-build # merge existing build

Output: firmware/release/m5paper-reader-v<version>.bin (and its SHA-256).
"""

import argparse
import hashlib
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENV = "m5paper"
VERSION_HEADER = os.path.join(ROOT, "include", "core", "version.h")
PIO_HOME = os.path.expanduser("~/.platformio")
ESPTOOL = os.path.join(PIO_HOME, "packages", "tool-esptoolpy", "esptool.py")
BOOT_APP0 = os.path.join(
    PIO_HOME, "packages", "framework-arduinoespressif32", "tools",
    "partitions", "boot_app0.bin")

# Same layout and header flags as `pio run -t upload` for this board: the
# platform writes "dio" into image headers for QIO boards, and the 80 MHz
# clock comes from `board_build.f_flash` in platformio.ini.
FLASH_ARGS = ["--flash_mode", "dio", "--flash_freq", "80m",
              "--flash_size", "16MB"]


def read_version() -> str:
  """Returns the version string declared in include/core/version.h."""
  with open(VERSION_HEADER, encoding="utf-8") as f:
    match = re.search(r'kFirmwareVersion\[\]\s*=\s*"([^"]+)"', f.read())
  if not match:
    sys.exit(f"cannot find kFirmwareVersion in {VERSION_HEADER}")
  return match.group(1)


def image_parts(build_dir: str) -> list[tuple[str, str]]:
  """Returns (offset, path) pairs for every image in the merged file."""
  parts = [
      ("0x1000", os.path.join(build_dir, "bootloader.bin")),
      ("0x8000", os.path.join(build_dir, "partitions.bin")),
      ("0xe000", BOOT_APP0),
      ("0x10000", os.path.join(build_dir, "firmware.bin")),
  ]
  for _, path in parts:
    if not os.path.exists(path):
      sys.exit(f"missing {path}; run without --skip-build")
  return parts


def sha256(path: str) -> str:
  """Returns the hex SHA-256 digest of the file at `path`."""
  digest = hashlib.sha256()
  with open(path, "rb") as f:
    for chunk in iter(lambda: f.read(1 << 20), b""):
      digest.update(chunk)
  return digest.hexdigest()


def main() -> None:
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument("--skip-build", action="store_true",
                      help="merge the existing .pio build without rebuilding")
  args = parser.parse_args()

  if not args.skip_build:
    venv_pio = os.path.join(ROOT, ".venv", "bin", "pio")
    pio = venv_pio if os.path.exists(venv_pio) else "pio"
    subprocess.run([pio, "run", "-e", ENV], cwd=ROOT, check=True)

  version = read_version()
  build_dir = os.path.join(ROOT, ".pio", "build", ENV)
  out_dir = os.path.join(ROOT, "firmware", "release")
  os.makedirs(out_dir, exist_ok=True)
  out = os.path.join(out_dir, f"m5paper-reader-v{version}.bin")

  cmd = [sys.executable, ESPTOOL, "--chip", "esp32", "merge_bin", "-o", out]
  cmd += FLASH_ARGS
  for offset, path in image_parts(build_dir):
    cmd += [offset, path]
  subprocess.run(cmd, check=True)

  print(f"\n{os.path.relpath(out, ROOT)}")
  print(f"  size   {os.path.getsize(out)} bytes")
  print(f"  sha256 {sha256(out)}")


if __name__ == "__main__":
  main()
