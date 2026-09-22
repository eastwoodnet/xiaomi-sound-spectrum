"""Run the ARM64 mode query without LED/audio hardware (QEMU user emulation)."""
import shutil
import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("qemu-aarch64"), "requires qemu-aarch64")
class ModeQueryTests(unittest.TestCase):
    def invoke(self, *args):
        return subprocess.run(
            ["qemu-aarch64", str(REPO / "led_music_oh2p"), *args],
            capture_output=True, timeout=3)

    def test_shared_modes_can_be_queried_without_hardware(self):
        for mode in ("auto", "1", "2", "3", "4"):
            with self.subTest(mode=mode):
                self.assertEqual(self.invoke("--check-mode", mode).returncode, 0)

    def test_unknown_or_partial_modes_are_rejected(self):
        for mode in ("", "0", "5", "11", "01", "-1", "1x", "a", "automatic", "9" * 80):
            with self.subTest(mode=mode):
                self.assertEqual(self.invoke("--check-mode", mode).returncode, 2)
                self.assertEqual(self.invoke(mode, "20").returncode, 2)

    def test_query_argument_count_is_checked(self):
        for args in (("--check-mode",), ("--check-mode", "1", "20")):
            self.assertEqual(self.invoke(*args).returncode, 2)


if __name__ == "__main__":
    unittest.main()
