import importlib.util
import os
import shutil
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("deploy_oh2p_service", REPO / "deploy-oh2p.py")
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)
STATE = Path("/tmp/xiaomi-spectrum-oh2p-service")


@unittest.skipUnless(os.name == "posix" and shutil.which("start-stop-daemon"), "Linux service test")
class ServiceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.version = self.base / "version"
        self.version.mkdir()
        self.service = self.version / "service-oh2p.sh"
        shutil.copyfile(REPO / "service-oh2p.sh", self.service)
        self.ready = self.base / "ready"
        self.events = self.base / "events"
        self.helper = self.version / "run-oh2p.sh"
        self.helper.write_text(
            '#!/bin/sh\nbase=$(dirname "$(dirname "$0")")\n'
            'if [ "${1:-}" = --check ]; then [ -f "$base/ready" ] && exit 0; exit 4; fi\n'
            'echo "$$" >> "$base/events"\n'
            "trap 'exit 0' TERM INT HUP\n"
            'while [ -f "$base/ready" ]; do sleep 0.1; done\n')
        self.assertFalse(STATE.exists(), "Do not interfere with an existing service")
        self.addCleanup(lambda: self.call("stop"))

    def call(self, action):
        return subprocess.run(["sh", str(self.service), action], capture_output=True, text=True, timeout=15)

    def unrelated_process(self):
        process = subprocess.Popen(["sleep", "30"])

        def cleanup():
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=5)

        self.addCleanup(cleanup)
        return process

    def wait_for(self, predicate, timeout=12):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if predicate():
                return
            time.sleep(0.1)
        self.fail("Timed out waiting for service transition: " + self.call("status").stdout)

    def test_singleton_detach_and_scoped_stop(self):
        self.ready.touch()
        unrelated = self.unrelated_process()
        self.assertEqual(self.call("start").returncode, 0)
        owner = (STATE / "owner").read_text()
        self.assertEqual(self.call("start").returncode, 0)
        self.assertEqual((STATE / "owner").read_text(), owner)
        self.wait_for(lambda: self.events.exists())
        self.assertEqual(self.call("stop").returncode, 0)
        self.assertFalse(STATE.exists())
        self.assertIsNone(unrelated.poll())

    def test_wait_yield_and_automatic_resume(self):
        self.assertEqual(self.call("start").returncode, 0)
        time.sleep(0.5)
        self.assertFalse(self.events.exists())
        self.ready.touch()
        self.wait_for(lambda: self.events.exists())
        first = self.events.read_text().splitlines()
        self.ready.unlink()
        self.wait_for(lambda: not (STATE / "child").exists())
        self.ready.touch()
        self.wait_for(lambda: len(self.events.read_text().splitlines()) > len(first))
        self.assertEqual(self.call("stop").returncode, 0)
        self.assertFalse(STATE.exists())

    def test_stale_pid_cannot_kill_unrelated_process(self):
        unrelated = self.unrelated_process()
        STATE.mkdir()
        (STATE / "owner").write_text(f"{unrelated.pid} 0\n")
        (STATE / "child").write_text(f"{unrelated.pid} 0\n")
        self.assertEqual(self.call("stop").returncode, 0)
        self.assertIsNone(unrelated.poll())
        self.assertFalse(STATE.exists())


@unittest.skipUnless(os.name == "posix", "POSIX boot hook test")
class BootHookTests(unittest.TestCase):
    def test_original_bytes_permissions_and_idempotent_hook(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            root = base / "spectrum"
            root.mkdir()
            init = base / "init.sh"
            original = b'#!/bin/sh\nprintf original\nexit 0\n# retain this tail without newline'
            init.write_bytes(original)
            init.chmod(0o750)
            hook = deploy.boot_hook(str(root), str(init))
            subprocess.run(["sh", "-c", hook], check=True, capture_output=True)
            first = init.read_bytes()
            self.assertTrue(first.endswith(original))
            self.assertEqual((root / "init.sh.before-spectrum").read_bytes(), original)
            self.assertEqual(init.stat().st_mode & 0o777, 0o750)
            subprocess.run(["sh", "-c", hook], check=True, capture_output=True)
            self.assertEqual(init.read_bytes(), first)
            self.assertEqual(first.count(b"# BEGIN xiaomi-sound-spectrum OH2P"), 1)
            self.assertEqual(subprocess.check_output(["sh", str(init)]), b"original")
            (root / "service-oh2p.sh").write_text('#!/bin/sh\nprintf started > "' + str(root / "started") + '"\n')
            (root / "enabled").touch()
            self.assertEqual(subprocess.check_output(["sh", str(init)]), b"original")
            self.assertEqual((root / "started").read_text(), "started")

    def test_modified_hook_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            init = root / "init.sh"
            original = b"#!/bin/sh\n# BEGIN xiaomi-sound-spectrum OH2P\nexit 0\n"
            init.write_bytes(original)
            result = subprocess.run(["sh", "-c", deploy.boot_hook(str(root), str(init))], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(init.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
