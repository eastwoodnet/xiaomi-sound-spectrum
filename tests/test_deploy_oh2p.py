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
    def test_mode_argument_does_not_duplicate_engine_mode_list(self):
        for mode in ("auto", "1", "3", "4", "99"):
            self.assertEqual(deploy.mode_argument(mode), mode)
        for mode in ("0", "01", "-1", "1;echo", "", "automatic"):
            with self.assertRaises(deploy.argparse.ArgumentTypeError):
                deploy.mode_argument(mode)

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

    def test_interactive_and_explicit_hosts_use_same_deployment(self):
        commands = []
        for host_args in (["speaker"], []):
            responses = [
                subprocess.CompletedProcess([], 0, "/tmp/oh2p-spectrum-upload.ABC123\n"),
                subprocess.CompletedProcess([], 0),
                subprocess.CompletedProcess([], 0),
            ]
            with patch("builtins.input", return_value="  speaker  ") as prompt, \
                 patch.object(deploy, "run", side_effect=responses) as remote, \
                 patch.object(deploy.shutil, "which", return_value="openssh"), \
                 redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                rc = deploy.main([*host_args, "--source-dir", str(self.directory), "--temporary"])
            self.assertEqual(rc, 0)
            self.assertEqual(prompt.call_count, 0 if host_args else 1)
            self.assertEqual(remote.call_count, 3)
            commands.append(remote.call_args_list)
        self.assertEqual(commands[0], commands[1])

    def test_invalid_interactive_host_never_connects(self):
        for host in ("", "   ", "bad host", "speaker;command", "-oProxyCommand=command"):
            with self.subTest(host=host), \
                 patch("builtins.input", return_value=host), \
                 patch.object(deploy, "run") as remote, \
                 redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    deploy.main([])
                self.assertEqual(error.exception.code, 2)
                remote.assert_not_called()

    def test_cancelled_interactive_input_never_connects(self):
        for failure in (EOFError, KeyboardInterrupt):
            with self.subTest(failure=failure), \
                 patch("builtins.input", side_effect=failure), \
                 patch.object(deploy, "run") as remote, \
                 redirect_stderr(io.StringIO()):
                self.assertEqual(deploy.main([]), 1)
                remote.assert_not_called()


if __name__ == "__main__":
    unittest.main()
