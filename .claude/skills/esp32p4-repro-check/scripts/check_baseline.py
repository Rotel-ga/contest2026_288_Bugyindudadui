#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Read-only preflight for the ESP32-P4X openvela contest baseline."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Any
import xml.etree.ElementTree as ET

EXPECTED_COMMIT = "35a953cc3673c0329b6a8de569604d492e1b64f0"
EXPECTED_TREE = "0598b69891e19663a0ba3f559f03293746bc7974"
SELFTEST_CONFIG = "CONFIG_LVX_USE_DEMO_CONTEST2026_288_P4X_SELFTEST=y"

REQUIRED_PATHS = (
    "contest2026_288_Bugyindudadui.xml",
    "app/p4x_selftest/p4x_selftest_main.c",
    "board/contest_board/configs/nsh/defconfig",
    "board/contest_board/configs/uart0/defconfig",
    "board/contest_board/configs/i2c/defconfig",
    "board/contest_board/configs/demo/defconfig",
    "board/contest_board/src/esp32p4_bringup.c",
    "board/contest_board/src/esp32p4_board_i2c.c",
    "board/contest_board/tools/prepare_esp_hal.sh",
    "docs/bringup/p4x_selftest.md",
    "docs/bringup/p4x_selftest_serial_raw.log",
    "docs/bringup/p4x_selftest_serial_report.json",
    "docs/bringup/run_openocd_jtag.sh",
)

BASELINE_PATHS = (
    "contest2026_288_Bugyindudadui.xml",
    "app/p4x_selftest",
    "board/contest_board",
    "docs/bringup/p4x_selftest.md",
    "docs/bringup/p4x_selftest_serial_raw.log",
    "docs/bringup/p4x_selftest_serial_report.json",
)

EVIDENCE_HASHES = {
    "docs/bringup/p4x_selftest_serial_raw.log":
        "dd13caebc28022cbf8d8de60d42d06254bf57fbf9bafa851ff611e3d41a968b7",
    "docs/bringup/p4x_selftest_serial_report.json":
        "f7da8240d2aff8ccf2a89ad531d3609b18b211585b03619cd4cbd1cb26174b9e",
}

LINKFILES = {
    "app/p4x_selftest": "packages/demos/contest2026_288_p4x_selftest",
    "board/contest_board": "vendor/openvela/boards/contest2026_288_board",
}


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

    def check_required_paths(self) -> None:
        missing = [item for item in REQUIRED_PATHS if not (self.repo / item).is_file()]
        self.record(
            "required paths",
            not missing,
            "all present" if not missing else "missing=" + ",".join(missing),
        )

    def check_baseline_paths_unchanged(self, expected_commit: str) -> None:
        tracked = self.git(
            "diff", "--name-only", expected_commit, "--", *BASELINE_PATHS
        )
        process = subprocess.run(
            [
                "git", "-C", str(self.repo), "status", "--porcelain",
                "--untracked-files=all", "--", *BASELINE_PATHS,
            ],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        uncommitted = process.stdout.strip()
        changed = "; ".join(item for item in (tracked, uncommitted) if item)
        self.record(
            "baseline paths unchanged",
            changed == "",
            "no changes from expected baseline" if changed == ""
            else changed.replace("\n", "; "),
        )

    def check_linkfiles(self) -> None:
        root = ET.parse(self.repo / "contest2026_288_Bugyindudadui.xml")
        actual = {
            node.get("src"): node.get("dest")
            for node in root.findall(".//linkfile")
        }
        mismatches = [
            f"{source}->{actual.get(source)!r}"
            for source, destination in LINKFILES.items()
            if actual.get(source) != destination
        ]
        self.record(
            "manifest linkfiles",
            not mismatches,
            "expected mappings present" if not mismatches
            else "mismatch=" + ",".join(mismatches),
        )

    def check_demo_delta(self) -> None:
        i2c = (self.repo / "board/contest_board/configs/i2c/defconfig").read_text(
            encoding="utf-8"
        ).splitlines()
        demo = (self.repo / "board/contest_board/configs/demo/defconfig").read_text(
            encoding="utf-8"
        ).splitlines()
        count = demo.count(SELFTEST_CONFIG)
        filtered = [line for line in demo if line != SELFTEST_CONFIG]
        passed = count == 1 and filtered == i2c
        self.record(
            "demo configuration delta",
            passed,
            "demo=i2c+selftest" if passed
            else f"selftest_count={count} remaining_equal={filtered == i2c}",
        )

    def check_source_contract(self) -> None:
        source = (self.repo / "app/p4x_selftest/p4x_selftest_main.c").read_text(
            encoding="utf-8"
        )
        bringup = (self.repo / "board/contest_board/src/esp32p4_bringup.c").read_text(
            encoding="utf-8"
        )
        i2c = (self.repo / "board/contest_board/src/esp32p4_board_i2c.c").read_text(
            encoding="utf-8"
        )
        tokens = (
            'SELFTEST_GPIO_DEVICE       "/dev/gpio0"',
            'SELFTEST_I2C_DEVICE        "/dev/i2c1"',
            "SELFTEST_I2C_ADDRESS       0x18",
            "SELFTEST_TIMER_DELAY_US    500000",
            "selftest_print_json",
        )
        missing = [token for token in tokens if token not in source]
        if "esp_gpio_init" not in bringup:
            missing.append("esp_gpio_init")
        if "board_i2c_init" not in bringup:
            missing.append("board_i2c_init")
        if "esp_i2cbus_initialize(ESPRESSIF_I2C1)" not in i2c:
            missing.append("esp_i2cbus_initialize(I2C1)")
        if "i2c_register(i2c, ESPRESSIF_I2C1)" not in i2c:
            missing.append("i2c_register(I2C1)")
        self.record(
            "source contract",
            not missing,
            "expected devices and call chain present" if not missing
            else "missing=" + ",".join(missing),
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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Read-only preflight for ESP32-P4X contest reproduction"
    )
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--expected-commit", default=EXPECTED_COMMIT)
    parser.add_argument("--expected-tree", default=EXPECTED_TREE)
    parser.add_argument("--json", action="store_true", dest="json_output")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
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
        identity_ok = preflight.check_identity(
            args.expected_commit, args.expected_tree
        )
        preflight.check_required_paths()
        if identity_ok:
            preflight.check_baseline_paths_unchanged(args.expected_commit)
        else:
            preflight.record(
                "baseline paths unchanged", False,
                "expected baseline commit/tree unavailable",
            )
        preflight.check_linkfiles()
        preflight.check_demo_delta()
        preflight.check_source_contract()
        preflight.check_evidence_hashes()
    except (OSError, subprocess.CalledProcessError, ET.ParseError) as error:
        preflight.record("preflight execution", False, str(error))

    payload = {
        "application": "esp32p4-repro-check",
        "repository": str(repo),
        "result": "PASS" if preflight.passed else "FAIL",
        "checks": preflight.results,
    }
    if args.json_output:
        print(json.dumps(payload, ensure_ascii=False, sort_keys=True))
    else:
        print("ESP32-P4X reproduction preflight")
        print(f"repository={repo}")
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
