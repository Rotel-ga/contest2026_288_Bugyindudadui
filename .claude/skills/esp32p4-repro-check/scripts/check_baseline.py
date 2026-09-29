#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Read-only preflight for the ESP32-P4X openvela contest baselines.

Two profiles are supported:

  final (default)  functional source baseline of the complete work
                   (display, touch, desktop/lock screen, camera, fall monitor,
                   photo identification) plus the seven build configurations
  p0               the frozen 2026-09-17 board-bring-up baseline (four
                   configurations, demo = i2c + selftest); only meaningful on a
                   checkout from that period

Every check reads the repository or a temporary copy; nothing is modified.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Callable
import xml.etree.ElementTree as ET

MANIFEST = "contest2026_288_Bugyindudadui.xml"
SELFTEST_CONFIG = "CONFIG_LVX_USE_DEMO_CONTEST2026_288_P4X_SELFTEST=y"
CONFIG_DIR = "board/contest_board/configs"

# Stored p4x_selftest evidence (unchanged since the P0 baseline).
EVIDENCE_HASHES = {
    "docs/bringup/p4x_selftest_serial_raw.log":
        "dd13caebc28022cbf8d8de60d42d06254bf57fbf9bafa851ff611e3d41a968b7",
    "docs/bringup/p4x_selftest_serial_report.json":
        "f7da8240d2aff8ccf2a89ad531d3609b18b211585b03619cd4cbd1cb26174b9e",
}

P0_REQUIRED = (
    MANIFEST,
    "app/p4x_selftest/p4x_selftest_main.c",
    f"{CONFIG_DIR}/nsh/defconfig",
    f"{CONFIG_DIR}/uart0/defconfig",
    f"{CONFIG_DIR}/i2c/defconfig",
    f"{CONFIG_DIR}/demo/defconfig",
    "board/contest_board/src/esp32p4_bringup.c",
    "board/contest_board/src/esp32p4_board_i2c.c",
    "board/contest_board/tools/prepare_esp_hal.sh",
    "docs/bringup/p4x_selftest.md",
    "docs/bringup/p4x_selftest_serial_raw.log",
    "docs/bringup/p4x_selftest_serial_report.json",
    "docs/bringup/run_openocd_jtag.sh",
)

FINAL_REQUIRED = P0_REQUIRED + (
    f"{CONFIG_DIR}/lcd/defconfig",
    f"{CONFIG_DIR}/desktop/defconfig",
    f"{CONFIG_DIR}/desktop_camera/defconfig",
    "board/contest_board/chip/espressif/esp32p4_lcd.c",
    "board/contest_board/chip/esp_lcd/openvela.patch",
    "board/contest_board/chip/esp_lcd/upstream-sha256.json",
    "board/contest_board/src/esp32p4_touch.c",
    "board/contest_board/src/esp32p4_desktop_storage.c",
    "app/desktop/Makefile",
    "app/desktop/desktop_boot.c",
    "app/desktop/core/settings.c",
    "app/desktop/ui/lockscreen.c",
    "app/fallguard/fallguard.c",
    "app/fallguard/panel_control.c",
    "app/photo_identify/photo_identify.c",
    "app/photo_identify/identify_bridge.c",
    "app/p4x_selftest/p4x_camera_csi.c",
    "app/p4x_selftest/jpeg_sw.c",
    "tools/monitor/fall_watch.py",
    "tools/monitor/board_console.py",
    "tools/monitor/ai_client.py",
    "tools/monitor/jpeg_frame.py",
    "tools/photo_identify/identify_watch.py",
)

P0_LINKFILES = {
    "app/p4x_selftest": "packages/demos/contest2026_288_p4x_selftest",
    "board/contest_board": "vendor/openvela/boards/contest2026_288_board",
}

FINAL_LINKFILES = dict(P0_LINKFILES)
FINAL_LINKFILES["app/desktop"] = "packages/demos/contest2026_288_desktop"

# (file, token) pairs that must be present verbatim.
P0_TOKENS = (
    ("app/p4x_selftest/p4x_selftest_main.c", 'SELFTEST_GPIO_DEVICE       "/dev/gpio0"'),
    ("app/p4x_selftest/p4x_selftest_main.c", 'SELFTEST_I2C_DEVICE        "/dev/i2c1"'),
    ("app/p4x_selftest/p4x_selftest_main.c", "SELFTEST_I2C_ADDRESS       0x18"),
    ("app/p4x_selftest/p4x_selftest_main.c", "SELFTEST_TIMER_DELAY_US    500000"),
    ("app/p4x_selftest/p4x_selftest_main.c", "selftest_print_json"),
    ("board/contest_board/src/esp32p4_bringup.c", "esp_gpio_init"),
    ("board/contest_board/src/esp32p4_bringup.c", "board_i2c_init"),
    ("board/contest_board/src/esp32p4_board_i2c.c", "esp_i2cbus_initialize(ESPRESSIF_I2C1)"),
    ("board/contest_board/src/esp32p4_board_i2c.c", "i2c_register(i2c, ESPRESSIF_I2C1)"),
)

FINAL_TOKENS = P0_TOKENS + (
    ("board/contest_board/src/esp32p4_bringup.c", "fb_register(0, 0)"),
    ("board/contest_board/src/esp32p4_bringup.c", "board_touch_initialize"),
    ("board/contest_board/src/esp32p4_bringup.c", "board_desktop_storage_initialize"),
    ("board/contest_board/chip/espressif/esp32p4_lcd.c", "int up_fbinitialize(int display)"),
    ("board/contest_board/src/esp32p4_desktop_storage.c", "#define DESKTOP_DATA_OFFSET 0xf80000"),
    ("board/contest_board/chip/espressif/esp_irq.c", "int esp_alloc_native_irq("),
    ("board/contest_board/chip/espressif/esp_usbserial.c", "int esp_usbserial_frame_begin(void)"),
    ("app/desktop/Makefile", "PROGNAME = desktop desktop_boot fgctl pictl"),
    ("app/desktop/core/settings.c", "#define PIN_ROUNDS 8192"),
    ("app/desktop/core/settings.c", '#define SETTINGS_PATH "/data/desktop/settings.bin"'),
    ("app/p4x_selftest/p4x_camera_csi.c", "int camera_live_start(void)"),
    ("app/photo_identify/identify_bridge.c", "int identify_command(int argc, char **argv)"),
    ("app/fallguard/panel_control.c", "int panel_control_command(int argc, char **argv)"),
    ("tools/monitor/ai_client.py", 'os.environ.get("MIMO_API_KEY"'),
    ("tools/photo_identify/identify_watch.py", "os.environ['MIMO_API_KEY']"),
)

# Credentials must only come from the environment.
SECRET_PATTERNS = (
    re.compile(r"sk-[A-Za-z0-9]{20,}"),
    re.compile(r"open-apis/bot/v2/hook/[0-9A-Za-z-]{8,}"),
)
SECRET_SCAN_ROOTS = ("app", "board/contest_board", "tools")
SECRET_SCAN_SUFFIXES = {".c", ".h", ".py", ".sh", ".mk", ".cmake", ".txt", ".json"}


def lines_of(path: Path) -> list[str]:
    return path.read_text(encoding="utf-8").splitlines()


class Preflight:
    """Collect deterministic checks without modifying the repository."""

    def __init__(self, repo: Path) -> None:
        self.repo = repo
        self.results: list[dict[str, Any]] = []

    def record(self, name: str, passed: bool, detail: str) -> None:
        self.results.append({"name": name, "passed": passed, "detail": detail})

    def git(self, *args: str) -> str:
        process = subprocess.run(
            ["git", "-C", str(self.repo), *args],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        return process.stdout.strip()

    # ------------------------------------------------------------ identity
    def check_identity(self, expected_commit: str, expected_tree: str) -> bool:
        actual_commit = self.git("rev-parse", "HEAD")
        exists = subprocess.run(
            ["git", "-C", str(self.repo), "cat-file", "-e",
             f"{expected_commit}^{{commit}}"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode == 0
        ancestor = exists and subprocess.run(
            ["git", "-C", str(self.repo), "merge-base", "--is-ancestor",
             expected_commit, actual_commit],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode == 0
        self.record(
            "baseline commit",
            ancestor,
            f"expected={expected_commit} head={actual_commit} "
            f"ancestor={'yes' if ancestor else 'no'}",
        )
        actual_tree = self.git("show", "-s", "--format=%T", expected_commit) \
            if exists else "missing"
        tree_matches = exists and actual_tree == expected_tree
        self.record(
            "baseline tree",
            tree_matches,
            f"expected={expected_tree} actual={actual_tree}",
        )
        return ancestor and tree_matches

    def check_required_paths(self, required: tuple[str, ...]) -> None:
        missing = [item for item in required if not (self.repo / item).is_file()]
        self.record(
            "required paths",
            not missing,
            f"all {len(required)} present" if not missing
            else "missing=" + ",".join(missing),
        )

    def check_unchanged(self, name: str, expected_commit: str,
                        paths: tuple[str, ...],
                        ignore: Callable[[str], bool]) -> None:
        tracked = [
            item for item in self.git(
                "diff", "--name-only", expected_commit, "--", *paths
            ).splitlines() if item and not ignore(item)
        ]
        # Do not strip the whole output: porcelain lines start with a status
        # column that may be a space (" M path").
        status = subprocess.run(
            ["git", "-C", str(self.repo), "status", "--porcelain",
             "--untracked-files=all", "--", *paths],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        ).stdout.splitlines()
        uncommitted = [
            line for line in status
            if line and not ignore(line[3:].strip().strip('"'))
        ]
        changed = tracked + uncommitted
        self.record(
            name,
            not changed,
            "no changes from expected baseline" if not changed
            else "; ".join(changed),
        )

    # ------------------------------------------------------------ manifest
    def check_linkfiles(self, expected: dict[str, str]) -> None:
        root = ET.parse(self.repo / MANIFEST)
        actual = {
            node.get("src"): node.get("dest")
            for node in root.findall(".//linkfile")
        }
        mismatches = [
            f"{source}->{actual.get(source)!r}"
            for source, destination in expected.items()
            if actual.get(source) != destination
        ]
        self.record(
            "manifest linkfiles",
            not mismatches,
            f"{len(expected)} expected mappings present" if not mismatches
            else "mismatch=" + ",".join(mismatches),
        )

    # ------------------------------------------------------------ configs
    def config(self, name: str) -> list[str]:
        return lines_of(self.repo / CONFIG_DIR / name / "defconfig")

    def check_demo_is_i2c_plus_selftest(self) -> None:
        demo = self.config("demo")
        count = demo.count(SELFTEST_CONFIG)
        filtered = [line for line in demo if line != SELFTEST_CONFIG]
        passed = count == 1 and filtered == self.config("i2c")
        self.record(
            "demo configuration delta",
            passed,
            "demo=i2c+selftest" if passed
            else f"selftest_count={count} remaining_equal={filtered == self.config('i2c')}",
        )

    def check_final_configs(self) -> None:
        problems: list[str] = []

        def delta(base: str, derived: str) -> tuple[set[str], set[str]]:
            a, b = self.config(base), self.config(derived)
            return set(b) - set(a), set(a) - set(b)

        added, removed = delta("desktop", "desktop_camera")
        if added != {SELFTEST_CONFIG} or removed:
            problems.append(f"desktop_camera!=desktop+selftest(+{sorted(added)} -{sorted(removed)})")

        camera_required = {SELFTEST_CONFIG, "CONFIG_ESPRESSIF_SPIRAM=y", "CONFIG_MM_REGIONS=2"}
        camera_optional = {"CONFIG_I2C_TRACE=y"}
        added, removed = delta("i2c", "demo")
        if removed or not camera_required <= added or added - camera_required - camera_optional:
            problems.append(f"demo!=i2c+camera(+{sorted(added)} -{sorted(removed)})")

        if "CONFIG_I2C_TRACE=y" in self.config("desktop_camera"):
            problems.append("desktop_camera enables I2C_TRACE (corrupts USB image frames)")

        needs = {
            "lcd": ("CONFIG_ESP32P4_BOARD_LCD=y", "CONFIG_VIDEO_FB=y",
                    "CONFIG_GRAPHICS_LVGL=y", "CONFIG_MM_REGIONS=2"),
            "desktop": ("CONFIG_ESP32P4_DESKTOP=y",
                        'CONFIG_INIT_ENTRYPOINT="desktop_boot_main"',
                        "CONFIG_ESP32P4_BOARD_LCD=y", "CONFIG_VIDEO_FB=y",
                        "CONFIG_MM_REGIONS=2"),
        }
        for name, tokens in needs.items():
            have = set(self.config(name))
            missing = [token for token in tokens if token not in have]
            if missing:
                problems.append(f"{name} missing {missing}")
        self.record(
            "configuration contracts",
            not problems,
            "desktop_camera=desktop+selftest; demo=i2c+camera; lcd/desktop display options"
            if not problems else "; ".join(problems),
        )

    # ------------------------------------------------------------ sources
    def check_tokens(self, tokens: tuple[tuple[str, str], ...]) -> None:
        missing = []
        cache: dict[str, str] = {}
        for relative, token in tokens:
            if relative not in cache:
                path = self.repo / relative
                cache[relative] = path.read_text(encoding="utf-8") if path.is_file() else ""
            if token not in cache[relative]:
                missing.append(f"{relative}:{token}")
        self.record(
            "source contract",
            not missing,
            f"{len(tokens)} device/entry-point tokens present" if not missing
            else "missing=" + ",".join(missing),
        )

    def check_no_hardcoded_secrets(self) -> None:
        hits = []
        for root in SECRET_SCAN_ROOTS:
            for path in sorted((self.repo / root).rglob("*")):
                if not path.is_file() or path.suffix not in SECRET_SCAN_SUFFIXES:
                    continue
                if "esp-hal-3rdparty" in path.parts:
                    continue
                text = path.read_text(encoding="utf-8", errors="ignore")
                if any(pattern.search(text) for pattern in SECRET_PATTERNS):
                    hits.append(str(path.relative_to(self.repo)))
        self.record(
            "credentials from environment only",
            not hits,
            "no API key or webhook token in app/board/tools sources" if not hits
            else "hits=" + ",".join(hits),
        )

    def check_lcd_provenance(self) -> None:
        directory = self.repo / "board/contest_board/chip/esp_lcd"
        expected = json.loads((directory / "upstream-sha256.json").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as scratch:
            work = Path(scratch)
            for name in expected:
                shutil.copy2(directory / name, work / name)
            shutil.copy2(directory / "openvela.patch", work / "openvela.patch")
            applied = subprocess.run(
                ["git", "apply", "--reverse", "openvela.patch"],
                cwd=work, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )
            matched = sum(
                1 for name, digest in expected.items()
                if hashlib.sha256((work / name).read_bytes()).hexdigest() == digest
            ) if applied.returncode == 0 else 0
        passed = applied.returncode == 0 and matched == len(expected)
        self.record(
            "esp_lcd upstream provenance",
            passed,
            f"reverse openvela.patch -> {matched}/{len(expected)} upstream hashes"
            if applied.returncode == 0 else f"git apply --reverse failed: {applied.stderr.strip()}",
        )

    def check_evidence_hashes(self) -> None:
        for relative, expected in EVIDENCE_HASHES.items():
            digest = hashlib.sha256((self.repo / relative).read_bytes()).hexdigest()
            self.record(
                f"sha256 {Path(relative).name}",
                digest == expected,
                f"expected={expected} actual={digest}",
            )

    @property
    def passed(self) -> bool:
        return all(item["passed"] for item in self.results)


def ignore_docs_and_markers(path: str) -> bool:
    """Documentation and local build markers are not build inputs."""
    return path.endswith(".md") or Path(path).name == ".built"


def ignore_markers(path: str) -> bool:
    return Path(path).name == ".built"


PROFILES = {
    "final": {
        "commit": "79b565e814a5d8850edfbaa1a423a35be8eb92d7",
        "tree": "1c574b39baa21787d5ca2eb2baa535db462f6c14",
        "description": "complete work: display, touch, desktop/PIN lock, camera, fall monitor, photo identification",
    },
    "p0": {
        "commit": "35a953cc3673c0329b6a8de569604d492e1b64f0",
        "tree": "0598b69891e19663a0ba3f559f03293746bc7974",
        "description": "2026-09-17 board bring-up baseline (nsh/uart0/i2c/demo)",
    },
}


def run_final(preflight: Preflight, commit: str, identity_ok: bool) -> None:
    preflight.check_required_paths(FINAL_REQUIRED)
    if identity_ok:
        preflight.check_unchanged(
            "firmware and host tools unchanged", commit,
            ("app", "board/contest_board", "tools"), ignore_docs_and_markers,
        )
    else:
        preflight.record("firmware and host tools unchanged", False,
                         "expected baseline commit/tree unavailable")
    preflight.check_linkfiles(FINAL_LINKFILES)
    preflight.check_final_configs()
    preflight.check_tokens(FINAL_TOKENS)
    preflight.check_no_hardcoded_secrets()
    preflight.check_lcd_provenance()
    preflight.check_evidence_hashes()


def run_p0(preflight: Preflight, commit: str, identity_ok: bool) -> None:
    preflight.check_required_paths(P0_REQUIRED)
    if identity_ok:
        preflight.check_unchanged(
            "baseline paths unchanged", commit,
            (MANIFEST, "app/p4x_selftest", "board/contest_board",
             "docs/bringup/p4x_selftest.md",
             "docs/bringup/p4x_selftest_serial_raw.log",
             "docs/bringup/p4x_selftest_serial_report.json"),
            ignore_markers,
        )
    else:
        preflight.record("baseline paths unchanged", False,
                         "expected baseline commit/tree unavailable")
    preflight.check_linkfiles(P0_LINKFILES)
    preflight.check_demo_is_i2c_plus_selftest()
    preflight.check_tokens(P0_TOKENS)
    preflight.check_evidence_hashes()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Read-only preflight for ESP32-P4X contest reproduction"
    )
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--profile", choices=sorted(PROFILES), default="final",
                        help="final (default): complete work; p0: 2026-09-17 bring-up baseline")
    parser.add_argument("--expected-commit", default=None,
                        help="override the profile's baseline commit")
    parser.add_argument("--expected-tree", default=None,
                        help="override the profile's baseline tree")
    parser.add_argument("--json", action="store_true", dest="json_output")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    profile = PROFILES[args.profile]
    commit = args.expected_commit or profile["commit"]
    tree = args.expected_tree or profile["tree"]
    try:
        repo = Path(
            subprocess.run(
                ["git", "-C", str(args.repo), "rev-parse", "--show-toplevel"],
                check=True,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            ).stdout.strip()
        )
    except subprocess.CalledProcessError as error:
        print(f"ERROR: not a Git repository: {args.repo}", file=sys.stderr)
        if error.stderr:
            print(error.stderr.strip(), file=sys.stderr)
        return 2

    preflight = Preflight(repo)
    try:
        identity_ok = preflight.check_identity(commit, tree)
        if args.profile == "final":
            run_final(preflight, commit, identity_ok)
        else:
            run_p0(preflight, commit, identity_ok)
    except (OSError, subprocess.CalledProcessError, ET.ParseError, ValueError) as error:
        preflight.record("preflight execution", False, str(error))

    payload = {
        "application": "esp32p4-repro-check",
        "profile": args.profile,
        "repository": str(repo),
        "result": "PASS" if preflight.passed else "FAIL",
        "checks": preflight.results,
    }
    if args.json_output:
        print(json.dumps(payload, ensure_ascii=False, sort_keys=True))
    else:
        print("ESP32-P4X reproduction preflight")
        print(f"repository={repo}")
        print(f"profile={args.profile} ({profile['description']})")
        for item in preflight.results:
            status = "PASS" if item["passed"] else "FAIL"
            print(f"[{status}] {item['name']}: {item['detail']}")
        passed = sum(1 for item in preflight.results if item["passed"])
        print(
            f"Summary: PASS={passed} FAIL={len(preflight.results) - passed} "
            f"RESULT={payload['result']}"
        )

    return 0 if preflight.passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
