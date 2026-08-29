import json
from pathlib import Path
import tempfile
import unittest

from .session_logger import SessionManager


class TestSessionLogger(unittest.TestCase):
    def setUp(self):
        self._temporary_directory = tempfile.TemporaryDirectory()
        self.temporary_path = Path(self._temporary_directory.name)

    def tearDown(self):
        self._temporary_directory.cleanup()

    def test_session_autosaves_exact_received_bytes(self):
        session = SessionManager(
            log_dir=str(self.temporary_path),
            source="COM5",
            baudrate=115200,
        )

        self.assertTrue(session.start())
        chunks = [b"\x05\x01", b"\x02\x03", b"\x04\xaa\x00"]
        for chunk in chunks:
            self.assertTrue(session.write(chunk))

        expected = b"".join(chunks)
        self.assertTrue(session.force_flush())
        self.assertEqual(session.temporary_raw_path.read_bytes(), expected)
        self.assertTrue(session.finalize("COMPLETED"))
        self.assertEqual(session.final_raw_path.read_bytes(), expected)
        self.assertFalse(session.temporary_raw_path.exists())

        metadata = json.loads(session.metadata_path.read_text(encoding="utf-8"))
        self.assertEqual(metadata["status"], "COMPLETED")
        self.assertEqual(metadata["raw_bytes"], len(expected))

    def test_error_exit_keeps_recoverable_temporary_log(self):
        session = SessionManager(
            log_dir=str(self.temporary_path),
            source="COM5",
            baudrate=115200,
        )

        self.assertTrue(session.start())
        self.assertTrue(session.write(b"partial raw data"))
        self.assertFalse(session.finalize("ERROR"))

        self.assertEqual(session.temporary_raw_path.read_bytes(), b"partial raw data")
        self.assertFalse(session.final_raw_path.exists())
        metadata = json.loads(session.metadata_path.read_text(encoding="utf-8"))
        self.assertEqual(metadata["status"], "ERROR")

    def test_force_flush_records_event(self):
        session = SessionManager(
            log_dir=str(self.temporary_path),
            source="COM5",
            baudrate=115200,
        )

        self.assertTrue(session.start())
        self.assertTrue(session.write(b"abc"))
        self.assertTrue(session.force_flush())
        self.assertIn("LOG_FLUSHED", session.events_path.read_text(encoding="utf-8"))
        self.assertGreaterEqual(session.raw_logger.fsync_count, 1)
        session.finalize("COMPLETED")

    def test_periodic_flush_and_fsync(self):
        now = [0.0]
        session = SessionManager(
            log_dir=str(self.temporary_path),
            source="COM5",
            baudrate=115200,
            flush_interval=1.0,
            fsync_interval=5.0,
            clock=lambda: now[0],
        )

        self.assertTrue(session.start())
        self.assertTrue(session.write(b"periodic data"))

        now[0] = 1.1
        session.tick()
        self.assertGreaterEqual(session.raw_logger.flush_count, 1)
        self.assertEqual(session.temporary_raw_path.read_bytes(), b"periodic data")

        now[0] = 5.1
        session.tick()
        self.assertGreaterEqual(session.raw_logger.fsync_count, 1)
        session.finalize("COMPLETED")

    def test_session_directories_do_not_collide(self):
        first = SessionManager(str(self.temporary_path), "COM5", 115200)
        second = SessionManager(str(self.temporary_path), "COM5", 115200)

        self.assertNotEqual(first.session_dir, second.session_dir)
        self.assertTrue(first.start())
        self.assertTrue(second.start())
        first.finalize("COMPLETED")
        second.finalize("COMPLETED")

    def test_explicit_output_is_never_overwritten(self):
        output = self.temporary_path / "existing.bin"
        output.write_bytes(b"keep me")
        session = SessionManager(
            log_dir=str(self.temporary_path / "logs"),
            source="COM5",
            baudrate=115200,
            output_path=str(output),
        )

        self.assertFalse(session.start())
        self.assertEqual(session.status_label, "LOG ERROR")
        self.assertEqual(output.read_bytes(), b"keep me")


if __name__ == "__main__":
    unittest.main()
