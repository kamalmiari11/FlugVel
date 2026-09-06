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
  pending_update_version TEXT,              -- set by POST /api/devices/:id/push-update when an admin queues a remote update; handed to the device on its next check-in, then cleared.
  pending_update_url TEXT,
  pending_command TEXT,                     -- set by POST /api/devices/:id/command (e.g. 'factory_reset'); handed to the device on its next check-in, then cleared.
  updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);
