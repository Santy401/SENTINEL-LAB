"""Persistent SQLite history for the gateway (stdlib sqlite3).

All timestamps are stored twice: ISO 8601 UTC (`at`) for humans and epoch
milliseconds (`at_ms`) for queries. Writers pass `now_ms` explicitly so the
module is deterministic under test.
"""

from __future__ import annotations

import json
import os
import sqlite3
import threading
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

DEFAULT_DB = Path.home() / ".local" / "share" / "sentinel-lab" / "history.db"
DEFAULT_MAX_AGE_DAYS = 30
DEFAULT_MAX_BYTES = 200 * 1024 * 1024

SCHEMA = """
CREATE TABLE IF NOT EXISTS sessions(
  id INTEGER PRIMARY KEY, started_at TEXT, started_ms INTEGER, ended_at TEXT,
  gateway_version TEXT, device TEXT);
CREATE TABLE IF NOT EXISTS status_samples(
  id INTEGER PRIMARY KEY, session_id INTEGER, at TEXT, at_ms INTEGER,
  boot_id TEXT, firmware TEXT, reset_reason TEXT, uptime_ms INTEGER,
  free_heap INTEGER, min_free_heap INTEGER, largest_block INTEGER,
  stack_free INTEGER, clients INTEGER, clock_synced INTEGER,
  dropped_frames INTEGER, rejected INTEGER, rgb_mode TEXT,
  phone_intensity REAL, phone_peak REAL);
CREATE TABLE IF NOT EXISTS events(
  id INTEGER PRIMARY KEY, session_id INTEGER, at TEXT, at_ms INTEGER,
  name TEXT, client_id INTEGER, rssi REAL);
CREATE TABLE IF NOT EXISTS rf_samples(
  id INTEGER PRIMARY KEY, session_id INTEGER, at TEXT, at_ms INTEGER,
  client_id INTEGER, rssi REAL, filtered REAL, connected INTEGER);
CREATE TABLE IF NOT EXISTS motion_summaries(
  id INTEGER PRIMARY KEY, session_id INTEGER, at TEXT, at_ms INTEGER,
  samples INTEGER, peak REAL, mean REAL, stable INTEGER, movement INTEGER,
  alert INTEGER);
CREATE TABLE IF NOT EXISTS alerts(
  id TEXT PRIMARY KEY, session_id INTEGER, at TEXT, at_ms INTEGER, rule TEXT,
  severity TEXT, kind TEXT, title TEXT, message TEXT, payload TEXT,
  delivered_at TEXT, attempts INTEGER DEFAULT 0, next_attempt_ms INTEGER DEFAULT 0,
  last_error TEXT);
CREATE TABLE IF NOT EXISTS lab_snapshots(
  id INTEGER PRIMARY KEY, session_id INTEGER, at TEXT, at_ms INTEGER, data TEXT);
CREATE INDEX IF NOT EXISTS idx_status_at ON status_samples(at_ms);
CREATE INDEX IF NOT EXISTS idx_rf_at ON rf_samples(at_ms);
CREATE INDEX IF NOT EXISTS idx_motion_at ON motion_summaries(at_ms);
CREATE INDEX IF NOT EXISTS idx_events_at ON events(at_ms);
CREATE INDEX IF NOT EXISTS idx_alerts_pending ON alerts(delivered_at, next_attempt_ms);
"""

PRUNABLE = ("status_samples", "events", "rf_samples", "motion_summaries", "lab_snapshots")


def now_ms() -> int:
    return int(time.time() * 1000)


def iso(ms: int) -> str:
    moment = datetime.fromtimestamp(ms / 1000, tz=timezone.utc)
    return moment.strftime("%Y-%m-%dT%H:%M:%S.") + f"{moment.microsecond // 1000:03d}Z"


class MotionAggregator:
    """Folds relayed motion.sample frames into one summary per second."""

    def __init__(self) -> None:
        self.reset(None)

    def reset(self, second: int | None) -> None:
        self.second = second
        self.samples = 0
        self.peak = 0.0
        self.total = 0.0
        self.states = {"STABLE": 0, "MOVEMENT": 0, "ALERT": 0}

    def add(self, magnitude: float, state: str, at_ms: int) -> dict[str, Any] | None:
        """Adds a sample; returns the finished previous second, if any."""
        second = at_ms // 1000
        finished = None
        if self.second is not None and second != self.second and self.samples:
            finished = self.flush()
        if self.second != second:
            self.reset(second)
        self.samples += 1
        self.total += magnitude
        self.peak = max(self.peak, magnitude)
        if state in self.states:
            self.states[state] += 1
        return finished

    def flush(self) -> dict[str, Any] | None:
        if not self.samples or self.second is None:
            return None
        summary = {
            "at_ms": self.second * 1000, "samples": self.samples, "peak": self.peak,
            "mean": self.total / self.samples, **{k.lower(): v for k, v in self.states.items()},
        }
        self.reset(None)
        return summary


class History:
    def __init__(self, path: Path | str = DEFAULT_DB) -> None:
        self.path = Path(path)
        if str(path) != ":memory:":
            self.path.parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(str(path), check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.lock = threading.Lock()
        self._last_rf: dict[int, int] = {}
        with self.lock:
            self.db.executescript(SCHEMA)
            self.db.execute("PRAGMA journal_mode=WAL")
            self.db.commit()
        if str(path) != ":memory:":
            os.chmod(self.path, 0o600)

    # ------------------------------------------------------------ helpers
    def _exec(self, sql: str, args: tuple = ()) -> sqlite3.Cursor:
        with self.lock:
            cursor = self.db.execute(sql, args)
            self.db.commit()
            return cursor

    def _query(self, sql: str, args: tuple = ()) -> list[sqlite3.Row]:
        with self.lock:
            return self.db.execute(sql, args).fetchall()

    def close(self) -> None:
        with self.lock:
            self.db.close()

    # ------------------------------------------------------------ writers
    def start_session(self, gateway_version: str, device: str, at_ms: int | None = None) -> int:
        at_ms = at_ms or now_ms()
        cursor = self._exec(
            "INSERT INTO sessions(started_at, started_ms, gateway_version, device) VALUES(?,?,?,?)",
            (iso(at_ms), at_ms, gateway_version, device))
        return int(cursor.lastrowid)

    def end_session(self, session_id: int, at_ms: int | None = None) -> None:
        self._exec("UPDATE sessions SET ended_at=? WHERE id=?", (iso(at_ms or now_ms()), session_id))

    def add_status(self, session_id: int, status: dict[str, Any], at_ms: int) -> None:
        rgb = status.get("rgb") or {}
        phone = status.get("phoneMotion") or {}
        self._exec(
            "INSERT INTO status_samples(session_id, at, at_ms, boot_id, firmware, reset_reason,"
            " uptime_ms, free_heap, min_free_heap, largest_block, stack_free, clients,"
            " clock_synced, dropped_frames, rejected, rgb_mode, phone_intensity, phone_peak)"
            " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (session_id, iso(at_ms), at_ms, status.get("bootId"),
             status.get("firmwareVersion") or status.get("realtimeVersion"),
             status.get("resetReason"), status.get("uptimeMs"), status.get("freeHeapBytes"),
             status.get("minFreeHeapBytes"), status.get("largestFreeBlockBytes"),
             status.get("minLoopStackFreeBytes"), status.get("clientCount"),
             int(bool(status.get("clockSynced"))), status.get("droppedFrames"),
             status.get("rejectedMessages"), rgb.get("mode"), phone.get("intensity"),
             phone.get("peakMs2")))

    def add_event(self, session_id: int, name: str, client_id: int | None,
                  rssi: float | None, at_ms: int) -> None:
        self._exec("INSERT INTO events(session_id, at, at_ms, name, client_id, rssi) VALUES(?,?,?,?,?,?)",
                   (session_id, iso(at_ms), at_ms, name, client_id, rssi))

    def add_rf(self, session_id: int, client_id: int, rssi: float | None,
               filtered: float | None, connected: bool, at_ms: int) -> bool:
        """Stores at most one sample per client per second."""
        last = self._last_rf.get(client_id)
        if last is not None and at_ms - last < 1000:
            return False
        self._last_rf[client_id] = at_ms
        self._exec(
            "INSERT INTO rf_samples(session_id, at, at_ms, client_id, rssi, filtered, connected)"
            " VALUES(?,?,?,?,?,?,?)",
            (session_id, iso(at_ms), at_ms, client_id, rssi, filtered, int(bool(connected))))
        return True

    def add_motion_summary(self, session_id: int, summary: dict[str, Any]) -> None:
        self._exec(
            "INSERT INTO motion_summaries(session_id, at, at_ms, samples, peak, mean, stable,"
            " movement, alert) VALUES(?,?,?,?,?,?,?,?,?)",
            (session_id, iso(summary["at_ms"]), summary["at_ms"], summary["samples"],
             summary["peak"], summary["mean"], summary["stable"], summary["movement"],
             summary["alert"]))

    def add_lab_snapshot(self, session_id: int, data: dict[str, Any], at_ms: int) -> bool:
        """Stores /api/lab only when it changed since the previous snapshot."""
        text = json.dumps(data, sort_keys=True)
        rows = self._query("SELECT data FROM lab_snapshots ORDER BY id DESC LIMIT 1")
        if rows and rows[0]["data"] == text:
            return False
        self._exec("INSERT INTO lab_snapshots(session_id, at, at_ms, data) VALUES(?,?,?,?)",
                   (session_id, iso(at_ms), at_ms, text))
        return True

    # ------------------------------------------------------------ alerts
    def add_alert(self, session_id: int, alert: dict[str, Any], delivered: str | None = None) -> None:
        self._exec(
            "INSERT OR IGNORE INTO alerts(id, session_id, at, at_ms, rule, severity, kind, title,"
            " message, payload, delivered_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)",
            (alert["id"], session_id, alert["occurredAt"], alert["occurredMs"], alert["rule"],
             alert["severity"], alert["kind"], alert["title"], alert["message"],
             json.dumps(alert, sort_keys=True), delivered))

    def pending_alerts(self, at_ms: int, limit: int = 20) -> list[dict[str, Any]]:
        rows = self._query(
            "SELECT id, payload, attempts FROM alerts WHERE delivered_at IS NULL"
            " AND next_attempt_ms <= ? ORDER BY at_ms LIMIT ?", (at_ms, limit))
        return [{"id": r["id"], "attempts": r["attempts"], "payload": json.loads(r["payload"])}
                for r in rows]

    def mark_delivered(self, alert_id: str, at_ms: int) -> None:
        self._exec("UPDATE alerts SET delivered_at=?, last_error=NULL WHERE id=?",
                   (iso(at_ms), alert_id))

    def mark_failed(self, alert_id: str, next_attempt_ms: int, error: str) -> None:
        self._exec("UPDATE alerts SET attempts=attempts+1, next_attempt_ms=?, last_error=? WHERE id=?",
                   (next_attempt_ms, error[:200], alert_id))

    def mark_rejected(self, alert_id: str, error: str) -> None:
        """Permanent failure (e.g. 401): stop retrying, keep it visible."""
        self._exec("UPDATE alerts SET attempts=attempts+1, delivered_at=?, last_error=? WHERE id=?",
                   ("rechazada", error[:200], alert_id))

    def recent_alerts(self, limit: int = 50) -> list[dict[str, Any]]:
        rows = self._query(
            "SELECT id, at, rule, severity, kind, title, message, delivered_at, attempts, last_error"
            " FROM alerts ORDER BY at_ms DESC LIMIT ?", (limit,))
        return [dict(r) for r in rows]

    # ------------------------------------------------------------ reading
    def sessions(self) -> list[dict[str, Any]]:
        return [dict(r) for r in self._query("SELECT * FROM sessions ORDER BY id")]

    def resolve_session(self, spec: str | int) -> int | None:
        if str(spec) == "latest":
            rows = self._query("SELECT id FROM sessions ORDER BY id DESC LIMIT 1")
            return int(rows[0]["id"]) if rows else None
        rows = self._query("SELECT id FROM sessions WHERE id=?", (int(spec),))
        return int(rows[0]["id"]) if rows else None

    def rows(self, table: str, session_id: int, order: str = "at_ms") -> list[dict[str, Any]]:
        if table not in PRUNABLE + ("alerts",):
            raise ValueError(table)
        return [dict(r) for r in self._query(
            f"SELECT * FROM {table} WHERE session_id=? ORDER BY {order}", (session_id,))]

    def summary(self) -> dict[str, Any]:
        counts = {}
        for table in PRUNABLE + ("alerts", "sessions"):
            counts[table] = self._query(f"SELECT COUNT(*) AS n FROM {table}")[0]["n"]
        pending = self._query("SELECT COUNT(*) AS n FROM alerts WHERE delivered_at IS NULL")[0]["n"]
        last = self._query("SELECT * FROM sessions ORDER BY id DESC LIMIT 1")
        return {"counts": counts, "pendingAlerts": pending,
                "latestSession": dict(last[0]) if last else None, "bytes": self.size_bytes()}

    def size_bytes(self) -> int:
        if str(self.path) == ":memory:" or not self.path.exists():
            return 0
        total = self.path.stat().st_size
        for suffix in ("-wal", "-shm"):
            extra = Path(str(self.path) + suffix)
            if extra.exists():
                total += extra.stat().st_size
        return total

    # ------------------------------------------------------------ retention
    def prune(self, at_ms: int, max_age_days: int = DEFAULT_MAX_AGE_DAYS,
              max_bytes: int = DEFAULT_MAX_BYTES) -> int:
        """Deletes rows older than max_age_days, then the oldest 10 % of the
        bulk tables while the file exceeds max_bytes. Delivered alerts are
        kept for the age window; undelivered alerts are never pruned."""
        cutoff = at_ms - max_age_days * 86_400_000
        removed = 0
        with self.lock:
            for table in PRUNABLE:
                removed += self.db.execute(f"DELETE FROM {table} WHERE at_ms < ?", (cutoff,)).rowcount
            removed += self.db.execute(
                "DELETE FROM alerts WHERE at_ms < ? AND delivered_at IS NOT NULL", (cutoff,)).rowcount
            self.db.commit()
        for _ in range(20):
            if self.size_bytes() <= max_bytes:
                break
            with self.lock:
                for table in PRUNABLE:
                    n = self.db.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]
                    if n:
                        removed += self.db.execute(
                            f"DELETE FROM {table} WHERE id IN (SELECT id FROM {table}"
                            f" ORDER BY at_ms LIMIT ?)", (max(1, n // 10),)).rowcount
                self.db.commit()
                self.db.execute("PRAGMA wal_checkpoint(TRUNCATE)")
                self.db.execute("VACUUM")
        return removed
