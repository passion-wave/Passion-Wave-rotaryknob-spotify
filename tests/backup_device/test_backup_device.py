"""Backup-tool file/error checks; esptool is mocked, no USB device is accessed."""
from contextlib import redirect_stderr, redirect_stdout
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location(
    'backup_device', Path(__file__).resolve().parents[2] / 'tools' / 'backup_device.py')
backup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(backup)


class BackupTests(unittest.TestCase):
    SIZE = 4 * 1024 * 1024
    CHUNK = 131072

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='pw-backup-test-')
        self.dest = Path(self.temporary.name)
        self.old_umask = os.umask(0o077)
        self.identity = {'chip': 'esp32', 'size': self.SIZE,
                         'port': '/dev/USB-TEST-NOT-REAL', 'chunk_size': self.CHUNK}
        self.calls = []

    def tearDown(self):
        os.umask(self.old_umask)
        self.temporary.cleanup()

    def payload(self, offset):
        return bytes([offset // self.CHUNK]) * self.CHUNK

    def seed_resume(self):
        (self.dest / 'identity.json').write_text(json.dumps(self.identity))
        (self.dest / 'verified.json').write_text('{"old":true}')
        for offset in range(0, self.SIZE, self.CHUNK):
            (self.dest / f'chunk-{offset:08x}.bin').write_bytes(self.payload(offset))

    def run_backup(self, process):
        args = ['--port', self.identity['port'], '--chip', self.identity['chip'],
                '--size', str(self.SIZE), '--destination', str(self.dest)]
        with patch.object(backup.subprocess, 'run', side_effect=process), \
                redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            return backup.main(args)

    def check_command(self, command, **kwargs):
        self.calls.append(command)
        self.assertEqual(command[command.index('--before') + 1], 'default_reset')
        self.assertEqual(command[command.index('--after') + 1], 'no_reset')
        self.assertNotIn('hard_reset', command)
        self.assertFalse((self.dest / 'verified.json').exists())
        self.assertIn(kwargs['timeout'], (90, 300))

    def successful_process(self, command, **kwargs):
        self.check_command(command, **kwargs)
        if 'read_flash' in command:
            index = command.index('read_flash')
            offset, size = int(command[index + 1], 0), int(command[index + 2], 0)
            self.assertEqual(size, self.CHUNK)
            Path(command[index + 3]).write_bytes(self.payload(offset))
        else:
            self.assertIn('verify_flash', command)
            image = Path(command[-1]).read_bytes()
            expected = b''.join(self.payload(offset) for offset in range(0, self.SIZE, self.CHUNK))
            self.assertEqual(image, expected)
        return SimpleNamespace(returncode=0)

    def test_complete_read_and_verify_keep_loader_and_publish_matching_digest(self):
        self.assertEqual(self.run_backup(self.successful_process), 0)
        self.assertEqual(len(self.calls), 33)
        marker = json.loads((self.dest / 'verified.json').read_text())
        image = self.dest / marker['image']
        self.assertEqual(marker['sha256'], hashlib.sha256(image.read_bytes()).hexdigest())
        self.assertEqual(marker['device_verify'], 'passed')
        self.assertEqual(image.stat().st_size, self.SIZE)
        self.assertFalse((self.dest / 'verified.incomplete').exists())
        self.assertFalse((self.dest / f'{image.name}.incomplete').exists())
        self.assertEqual((self.dest / 'verified.json').stat().st_mode & 0o777, 0o600)

    def test_resume_reuses_complete_blocks_and_rereads_only_wrong_size(self):
        self.seed_resume()
        (self.dest / 'chunk-00020000.bin').write_bytes(b'partial')
        self.assertEqual(self.run_backup(self.successful_process), 0)
        self.assertEqual(len(self.calls), 2)
        self.assertIn('read_flash', self.calls[0])
        self.assertEqual(self.calls[0][-3], '0x20000')
        self.assertIn('verify_flash', self.calls[1])

    def test_failure_cannot_reuse_an_old_complete_length_temporary_file(self):
        self.seed_resume()
        chunk = self.dest / 'chunk-00000000.bin'
        chunk.unlink()
        temporary = self.dest / 'chunk-00000000.incomplete'
        temporary.write_bytes(self.payload(0))

        def process(command, **kwargs):
            self.check_command(command, **kwargs)
            self.assertIn('read_flash', command)
            self.assertFalse(temporary.exists())
            if len(self.calls) == 1:
                temporary.write_bytes(self.payload(0))
                return SimpleNamespace(returncode=1)
            # A zero exit code without a freshly produced file is not success.
            return SimpleNamespace(returncode=0)

        self.assertEqual(self.run_backup(process), 1)
        self.assertEqual(len(self.calls), 3)
        self.assertFalse(chunk.exists())
        self.assertFalse((self.dest / 'verified.json').exists())

    def test_full_resume_still_requires_fresh_device_verification(self):
        self.seed_resume()
        (self.dest / 'verified.incomplete').write_text('{"old":true}')
        # A full-size but corrupted resumed block must not be qualified by size.
        (self.dest / 'chunk-00000000.bin').write_bytes(b'X' * self.CHUNK)

        def process(command, **kwargs):
            self.check_command(command, **kwargs)
            self.assertIn('verify_flash', command)
            self.assertFalse((self.dest / 'verified.incomplete').exists())
            self.assertEqual(Path(command[-1]).read_bytes()[:self.CHUNK], b'X' * self.CHUNK)
            return SimpleNamespace(returncode=1)

        self.assertEqual(self.run_backup(process), 1)
        self.assertEqual(len(self.calls), 1)
        self.assertFalse((self.dest / 'verified.json').exists())

    def test_verify_timeout_and_process_failure_invalidate_old_success(self):
        for failure in (subprocess.TimeoutExpired(['fake-esptool'], 300),
                        OSError('simulated process launch failure')):
            with self.subTest(failure=type(failure).__name__):
                self.seed_resume()

                def process(command, **kwargs):
                    self.check_command(command, **kwargs)
                    self.assertIn('verify_flash', command)
                    raise failure

                self.assertEqual(self.run_backup(process), 1)
                self.assertFalse((self.dest / 'verified.json').exists())

    def test_read_timeouts_never_leave_old_success_marker(self):
        self.seed_resume()
        (self.dest / 'chunk-00000000.bin').unlink()

        def process(command, **kwargs):
            self.check_command(command, **kwargs)
            self.assertIn('read_flash', command)
            raise subprocess.TimeoutExpired(command, 90)

        self.assertEqual(self.run_backup(process), 1)
        self.assertEqual(len(self.calls), 3)
        self.assertFalse((self.dest / 'verified.json').exists())

    def test_interruption_during_verify_leaves_backup_unqualified(self):
        self.seed_resume()

        def process(command, **kwargs):
            self.check_command(command, **kwargs)
            raise KeyboardInterrupt

        with self.assertRaises(KeyboardInterrupt):
            self.run_backup(process)
        self.assertFalse((self.dest / 'verified.json').exists())

    def test_identity_mismatch_refuses_before_touching_another_backup(self):
        self.seed_resume()
        original = (self.dest / 'verified.json').read_bytes()
        foreign = {**self.identity, 'port': '/dev/ANOTHER-DEVICE'}
        (self.dest / 'identity.json').write_text(json.dumps(foreign))
        with self.assertRaises(SystemExit) as raised:
            self.run_backup(self.successful_process)
        self.assertEqual(raised.exception.code, 2)
        self.assertEqual(self.calls, [])
        self.assertEqual((self.dest / 'verified.json').read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
