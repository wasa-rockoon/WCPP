"""Crash-tolerant raw serial logging for wcpp-util."""

from __future__ import annotations

from datetime import datetime
import json
import os
from pathlib import Path
import secrets
import time
from typing import Callable, Optional


def _timestamp() -> str:
    return datetime.now().astimezone().isoformat(timespec="microseconds")


class RawLogger:
    """Append-only raw byte logger with periodic flush and fsync."""

    def __init__(
        self,
        temporary_path: Path,
        final_path: Path,
        flush_interval: float = 1.0,
        fsync_interval: float = 5.0,
        clock: Callable[[], float] = time.monotonic,
    ):
        if flush_interval < 0 or fsync_interval < 0:
            raise ValueError("flush/fsync intervals must be non-negative")

        self.temporary_path = Path(temporary_path)
        self.final_path = Path(final_path)
        self.flush_interval = flush_interval
        self.fsync_interval = fsync_interval
        self._clock = clock
        self._file = None
        self.bytes_written = 0
        self.flush_count = 0
        self.fsync_count = 0
        self.last_flush_at = None
        self.last_fsync_at = None
        self.error: Optional[str] = None

    @property
    def is_open(self) -> bool:
        return self._file is not None and not self._file.closed

    def open(self) -> None:
        self.temporary_path.parent.mkdir(parents=True, exist_ok=True)
        # Exclusive creation prevents accidental destruction of an old flight log.
        self._file = self.temporary_path.open("xb")
        now = self._clock()
        self.last_flush_at = now
        self.last_fsync_at = now

    def write(self, data: bytes) -> bool:
        if not data:
            return True
        if not self.is_open:
            self.error = self.error or "raw log is not open"
            return False
        try:
            self._file.write(data)
            self.bytes_written += len(data)
            return True
        except OSError as exc:
            self.error = f"{type(exc).__name__}: {exc}"
            return False

    def tick(self) -> None:
        if not self.is_open or self.error:
            return
        now = self._clock()
        if now - self.last_fsync_at >= self.fsync_interval:
            self.flush(sync=True)
        elif now - self.last_flush_at >= self.flush_interval:
            self.flush(sync=False)

    def flush(self, sync: bool = False) -> bool:
        if not self.is_open:
            self.error = self.error or "raw log is not open"
            return False
        try:
            self._file.flush()
            self.flush_count += 1
            self.last_flush_at = self._clock()
            if sync:
                os.fsync(self._file.fileno())
                self.fsync_count += 1
                self.last_fsync_at = self._clock()
            return True
        except OSError as exc:
            self.error = f"{type(exc).__name__}: {exc}"
            return False

    def close(self, completed: bool) -> bool:
        if self.is_open:
            self.flush(sync=True)
            try:
                self._file.close()
            except OSError as exc:
                self.error = f"{type(exc).__name__}: {exc}"
            finally:
                self._file = None

        if completed and not self.error and self.temporary_path.exists():
            try:
                self.temporary_path.replace(self.final_path)
            except OSError as exc:
                self.error = f"{type(exc).__name__}: {exc}"
        return self.error is None


class SessionManager:
    """Own the files and metadata for one serial monitoring session."""

    def __init__(
        self,
        log_dir: str,
        source: str,
        baudrate: int,
        output_path: Optional[str] = None,
        flush_interval: float = 1.0,
        fsync_interval: float = 5.0,
        clock: Callable[[], float] = time.monotonic,
    ):
        self.started_at = _timestamp()
        unique = datetime.now().strftime("%Y%m%d_%H%M%S_%f") + "_" + secrets.token_hex(2)

        if output_path:
            self.final_raw_path = Path(output_path).expanduser().resolve()
            self.session_dir = self.final_raw_path.parent / f"{self.final_raw_path.stem}_session_{unique}"
        else:
            self.session_dir = Path(log_dir).expanduser().resolve() / unique
            self.final_raw_path = self.session_dir / "raw.bin"

        self.temporary_raw_path = self.final_raw_path.with_name(self.final_raw_path.name + ".tmp")
        self.events_path = self.session_dir / "events.log"
        self.metadata_path = self.session_dir / "session.json"
        self.source = source
        self.baudrate = baudrate
        self.error: Optional[str] = None
        self.raw_logger = RawLogger(
            self.temporary_raw_path,
            self.final_raw_path,
            flush_interval=flush_interval,
            fsync_interval=fsync_interval,
            clock=clock,
        )

    @property
    def bytes_written(self) -> int:
        return self.raw_logger.bytes_written

    @property
    def status_label(self) -> str:
        return "LOG ERROR" if self.error or self.raw_logger.error else "RECORDING"

    @property
    def last_error(self) -> Optional[str]:
        return self.error or self.raw_logger.error

    def start(self) -> bool:
        try:
            self.session_dir.mkdir(parents=True, exist_ok=False)
            if self.final_raw_path.exists() or self.temporary_raw_path.exists():
                raise FileExistsError(f"raw output already exists: {self.final_raw_path}")
            self.raw_logger.open()
            self.log_event("SESSION_STARTED", source=self.source, baud=self.baudrate)
            self._write_metadata("RUNNING")
            return True
        except OSError as exc:
            self.error = f"{type(exc).__name__}: {exc}"
            return False

    def write(self, data: bytes) -> bool:
        ok = self.raw_logger.write(data)
        if not ok:
            self._record_logger_error()
        return ok

    def tick(self) -> None:
        previous_error = self.raw_logger.error
        self.raw_logger.tick()
        if self.raw_logger.error and not previous_error:
            self._record_logger_error()

    def force_flush(self) -> bool:
        ok = self.raw_logger.flush(sync=True)
        if ok:
            self.log_event("LOG_FLUSHED", raw_bytes=self.bytes_written)
        else:
            self._record_logger_error()
        return ok

    def log_event(self, event: str, **details) -> None:
        fields = " ".join(f"{key}={value!r}" for key, value in details.items())
        line = f"{_timestamp()} {event}"
        if fields:
            line += " " + fields
        try:
            self.events_path.parent.mkdir(parents=True, exist_ok=True)
            with self.events_path.open("a", encoding="utf-8") as event_file:
                event_file.write(line + "\n")
                event_file.flush()
        except OSError as exc:
            self.error = f"{type(exc).__name__}: {exc}"

    def finalize(self, reason: str = "COMPLETED") -> bool:
        completed = reason == "COMPLETED" and not self.last_error
        self.raw_logger.close(completed=completed)
        if self.raw_logger.error:
            self.error = self.error or self.raw_logger.error

        final_status = "COMPLETED" if completed and not self.last_error else reason
        if self.last_error:
            final_status = "LOG_ERROR"
        self.log_event(
            "SESSION_FINALIZED",
            status=final_status,
            raw_bytes=self.bytes_written,
            error=self.last_error,
        )
        self._write_metadata(final_status, ended_at=_timestamp())
        return final_status == "COMPLETED"

    def _record_logger_error(self) -> None:
        if self.raw_logger.error and not self.error:
            # Preserve the error in memory even when the event file is on the same failed disk.
            logger_error = self.raw_logger.error
            self.log_event("LOG_ERROR", error=logger_error, raw_bytes=self.bytes_written)
            self.error = self.error or logger_error

    def _write_metadata(self, status: str, ended_at: Optional[str] = None) -> None:
        metadata = {
            "schema_version": 1,
            "status": status,
            "started_at": self.started_at,
            "ended_at": ended_at,
            "source": self.source,
            "baudrate": self.baudrate,
            "raw_path": str(self.final_raw_path),
            "temporary_raw_path": str(self.temporary_raw_path),
            "raw_bytes": self.bytes_written,
            "flush_count": self.raw_logger.flush_count,
            "fsync_count": self.raw_logger.fsync_count,
            "error": self.last_error,
        }
        temporary_metadata = self.metadata_path.with_name(self.metadata_path.name + ".new")
        try:
            self.metadata_path.parent.mkdir(parents=True, exist_ok=True)
            with temporary_metadata.open("w", encoding="utf-8") as metadata_file:
                json.dump(metadata, metadata_file, ensure_ascii=False, indent=2)
                metadata_file.write("\n")
                metadata_file.flush()
                os.fsync(metadata_file.fileno())
            os.replace(temporary_metadata, self.metadata_path)
        except OSError as exc:
            self.error = self.error or f"{type(exc).__name__}: {exc}"
