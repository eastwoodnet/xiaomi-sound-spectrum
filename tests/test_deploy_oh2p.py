import importlib.util
import io
import subprocess
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "deploy_oh2p", Path(__file__).resolve().parents[1] / "deploy-oh2p.py")
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)


class DeploymentGuardTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        header = bytearray(20)
        header[:6] = b"\x7fELF\x02\x01"
        header[18:20] = b"\xb7\x00"
        (self.directory / "led_music_oh2p").write_bytes(header)
        (self.directory / "run-oh2p.sh").write_text("#!/bin/sh\n", encoding="utf-8")
        (self.directory / "service-oh2p.sh").write_text("#!/bin/sh\n", encoding="utf-8")

    def invoke(self, result):
        with patch.object(deploy, "run", side_effect=result) as remote, \
             patch.object(deploy.shutil, "which", return_value="openssh"), \
             redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            rc = deploy.main(["speaker", "--source-dir", str(self.directory), "--temporary"])
        return rc, remote

    def test_incompatible_device_never_uploads(self):
        rc, remote = self.invoke([subprocess.CalledProcessError(3, ["ssh"])])
        self.assertEqual(rc, 1)
        self.assertEqual(remote.call_count, 1)
        self.assertEqual(remote.call_args.args[0][0], "ssh")

    def test_unexpected_staging_path_never_uploads(self):
        rc, remote = self.invoke([subprocess.CompletedProcess([], 0, "/data\n")])
        self.assertEqual(rc, 1)
        self.assertEqual(remote.call_count, 1)

    def test_failed_upload_never_installs(self):
        rc, remote = self.invoke([
            subprocess.CompletedProcess([], 0, "/tmp/oh2p-spectrum-upload.ABC123\n"),
            subprocess.CalledProcessError(1, ["scp"]),
        ])
        self.assertEqual(rc, 1)
        self.assertEqual(remote.call_count, 2)
        self.assertEqual(remote.call_args.args[0][0], "scp")

    def test_wrong_architecture_never_connects(self):
        (self.directory / "led_music_oh2p").write_bytes(b"not an ARM64 binary")
        rc, remote = self.invoke([])
        self.assertEqual(rc, 1)
        remote.assert_not_called()


if __name__ == "__main__":
    unittest.main()
