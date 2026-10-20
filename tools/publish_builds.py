"""Build every dongle variant and publish it into hoja-device-fw/builds, where the config app finds
it: builds/dongle_<board>/dongle_<board>.uf2, .bin and manifest.json (fw_version + SHA-256 of the
.bin, the same format as the gamepad builds).

    python tools/publish_builds.py [path/to/hoja-device-fw]

Each build is configured again first, so it gets a fresh firmware version (the UTC time CMake ran).
CMake and the toolchain come from PATH, or the Pico VS Code extension's folders under ~/.pico-sdk.
"""

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# Board build directory and the build id the config app knows it by
BOARDS = [
    ("build/hoja", "dongle_hoja"),
    ("build/pico-w", "dongle_pico_w"),
    ("build/pico-2w", "dongle_pico2_w"),
    ("build/rp2350b-plus-w", "dongle_rp2350b_plus_w"),
]

PICO_SDK = os.path.join(os.path.expanduser("~"), ".pico-sdk")
TOOL_DIRS = [
    os.path.join(PICO_SDK, "cmake", "v3.31.5", "bin"),
    os.path.join(PICO_SDK, "ninja", "v1.12.1"),
    os.path.join(PICO_SDK, "toolchain", "14_2_Rel1", "bin"),
]


def _env():
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([d for d in TOOL_DIRS if os.path.isdir(d)] + [env.get("PATH", "")])
    return env


def _cmake(env):
    return shutil.which("cmake", path=env["PATH"]) or sys.exit("cmake not found")


def _version(build_dir):
    with open(os.path.join(build_dir, "generated", "dongle_version.h")) as f:
        m = re.search(r"DONGLE_FW_VERSION (\d+)u", f.read())
    return int(m.group(1)) if m else sys.exit(f"{build_dir}: no firmware version")


def main():
    dest_root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "..", "hoja-device-fw"))
    builds = os.path.join(dest_root, "builds")
    if not os.path.isdir(builds):
        sys.exit(f"no builds folder in {dest_root}")

    env = _env()
    cmake = _cmake(env)

    for build_rel, build_id in BOARDS:
        build_dir = os.path.join(ROOT, build_rel)
        if not os.path.isfile(os.path.join(build_dir, "CMakeCache.txt")):
            sys.exit(f"{build_rel} is not configured yet (configure it once with its preset)")

        subprocess.run([cmake, build_dir], cwd=ROOT, env=env, check=True, stdout=subprocess.DEVNULL)
        subprocess.run([cmake, "--build", build_dir, "-j", "16"], cwd=ROOT, env=env, check=True,
                       stdout=subprocess.DEVNULL)

        out = os.path.join(builds, build_id)
        os.makedirs(out, exist_ok=True)
        for ext in ("uf2", "bin"):
            shutil.copyfile(os.path.join(build_dir, f"hoja-dongle-fw.{ext}"), os.path.join(out, f"{build_id}.{ext}"))

        with open(os.path.join(out, f"{build_id}.bin"), "rb") as f:
            checksum = hashlib.sha256(f.read()).hexdigest()

        manifest = {"changelog": "", "fw_version": _version(build_dir), "checksum": checksum}
        with open(os.path.join(out, "manifest.json"), "w", newline="\n") as f:
            json.dump(manifest, f, indent=2)

        print(f"{build_id}: fw_version {manifest['fw_version']}")


if __name__ == "__main__":
    main()
