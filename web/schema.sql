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

-- Emails collected from the "Get updates" signup form on the landing page
-- (POST /api/subscribe). UNIQUE on email so a repeat signup from the same
-- address is just a no-op (INSERT OR IGNORE), not a duplicate row or an
-- error shown to the visitor.
CREATE TABLE IF NOT EXISTS subscribers (
  id TEXT PRIMARY KEY,
  email TEXT NOT NULL UNIQUE,
  created_at TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Failed-attempt throttling for POST /api/login. There's only one admin
-- account (a single shared password, no separate usernames), so this is
-- keyed by client IP rather than anything account-related. A row only
-- exists while an IP has at least one recent failure - a correct password
-- deletes it, so normal use never accumulates rows here.
CREATE TABLE IF NOT EXISTS login_attempts (
  ip TEXT PRIMARY KEY,
  fail_count INTEGER NOT NULL DEFAULT 0,
  first_fail_at TEXT NOT NULL,  -- start of the current failure window
  locked_until TEXT             -- NULL until fail_count hits the threshold
);

-- Soft per-IP rate limit for POST /api/subscribe (see the honeypot check
-- and RATE_LIMIT_* constants in subscribe.js). Unlike login_attempts this
-- has no lockout - it's just a rolling window count, since a spammed
-- signup form isn't a security event, just something worth capping.
CREATE TABLE IF NOT EXISTS subscribe_attempts (
  ip TEXT PRIMARY KEY,
  count INTEGER NOT NULL DEFAULT 0,
  window_start TEXT NOT NULL
);
