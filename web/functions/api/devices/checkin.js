import { json } from "../../_lib/auth.js";

// A device is considered "online" if it checked in within this many
// seconds. Devices report every 30s (see src/api/checkin_api.cpp), so 90s
// is 3 missed heartbeats before we call it offline — enough slack for a
// dropped packet or a slow WiFi reconnect without flapping the status.
const STALE_AFTER_SECONDS = 90;

// POST /api/devices/checkin — called by the device itself, not the admin
// dashboard. Auth is a single shared secret (DEVICE_API_KEY, a Cloudflare
// Pages env var, mirroring how ADMIN_PASSWORD/JWT_SECRET are configured)
// sent as a header, since there's no per-user login on the device side —
// every FlugVel unit shares one key, the same way they'd share a Wi-Fi
// password on a home network.
//
// Body: { device_id, location?, firmware_version?, signal_dbm? }
// device_id is CaptivePortal::deviceName() on the firmware side — a stable
// "flugvel-xxxx" string derived from the device's own WiFi MAC, so a unit
// registers itself the first time it ever checks in. No manual pairing.
//
// Upsert semantics: on a device's FIRST check-in this creates a new row
// with a generic name the owner can rename later from the dashboard. On
// every check-in after that, only device-reported columns are touched —
// name/owner/notes (whatever a human has set from the dashboard) are left
// alone, so renaming a unit "Mom's unit" never gets clobbered by the next
// heartbeat.
export async function onRequestPost(context) {
  const expectedKey = context.env.DEVICE_API_KEY;
  if (!expectedKey) {
    // Misconfiguration guard, same pattern as requireAuth() in auth.js.
    return json({ error: "Server not configured for device check-ins" }, { status: 500 });
  }
  const providedKey = context.request.headers.get("X-Device-Key");
  if (!providedKey || providedKey !== expectedKey) {
    return json({ error: "Unauthorized" }, { status: 401 });
  }

  let body;
  try {
    body = await context.request.json();
  } catch {
    return json({ error: "Invalid request body" }, { status: 400 });
  }

  const deviceId = (body.device_id || "").trim();
  if (!deviceId) return json({ error: "device_id is required" }, { status: 400 });

  const location = (body.location || "").trim();
  const firmwareVersion = (body.firmware_version || "").trim();
  const signalDbm = Number.isFinite(body.signal_dbm) ? Math.trunc(body.signal_dbm) : null;

  // A friendly placeholder name for a device nobody has named yet — shown
  // in the dashboard until the owner edits it. Not used on conflict.
  const defaultName = `New device (${deviceId})`;

  await context.env.DB.prepare(
    `INSERT INTO devices (id, name, location, status, firmware_version, signal_dbm, last_seen_at, updated_at)
     VALUES (?, ?, ?, 'online', ?, ?, datetime('now'), datetime('now'))
     ON CONFLICT(id) DO UPDATE SET
       -- COALESCE guards against a device that reports a blank location
       -- (not yet configured) wiping out one an admin typed by hand.
       location = COALESCE(NULLIF(excluded.location, ''), devices.location),
       status = 'online',
       firmware_version = excluded.firmware_version,
       signal_dbm = excluded.signal_dbm,
       last_seen_at = excluded.last_seen_at,
       updated_at = excluded.updated_at`
  ).bind(deviceId, defaultName, location, firmwareVersion, signalDbm).run();

  return json({ ok: true, stale_after_seconds: STALE_AFTER_SECONDS });
}
