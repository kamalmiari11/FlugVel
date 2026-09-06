CREATE TABLE IF NOT EXISTS devices (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  owner TEXT DEFAULT '',
  location TEXT DEFAULT '',
  status TEXT NOT NULL DEFAULT 'unknown',   -- 'online' | 'offline' | 'unknown' — manual fallback, used only until a device has ever checked in (see last_seen_at)
  notes TEXT DEFAULT '',
  firmware_version TEXT DEFAULT '',
  signal_dbm INTEGER,
  last_seen_at TEXT,                        -- last successful check-in from the device itself (UTC, datetime('now') format). NULL = never checked in, still on manual status.
  updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);
