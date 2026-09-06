import { requireAuth, unauthorized, json } from "../../_lib/auth.js";

const ALLOWED_STATUS = new Set(["online", "offline", "unknown"]);

// A device counts as online if it checked in within this many seconds —
// matches the threshold used by the check-in endpoint's response
// (see checkin.js) and the firmware's 30s heartbeat interval.
const STALE_AFTER_SECONDS = 90;

// GET /api/devices — list every device, newest name first.
// Status is computed here rather than trusted from the stored column: a
// device that has ever checked in (last_seen_at is set) is "online" while
// recent, "offline" once stale, regardless of what status happens to be
// sitting in the row. A device that has NEVER checked in (last_seen_at is
// NULL — added by hand, or a fresh unit that hasn't phoned home yet) falls
// back to the manually-set status column, same as before auto-reporting.
export async function onRequestGet(context) {
  const auth = await requireAuth(context);
  if (!auth) return unauthorized();

  const { results } = await context.env.DB.prepare(
    `SELECT
       id, name, owner, location, notes, firmware_version, signal_dbm,
       last_seen_at, updated_at,
       CASE
         WHEN last_seen_at IS NULL THEN status
         WHEN (julianday('now') - julianday(last_seen_at)) * 86400.0 <= ${STALE_AFTER_SECONDS} THEN 'online'
         ELSE 'offline'
       END AS status
     FROM devices
     ORDER BY name COLLATE NOCASE`
  ).all();

  return json(results);
}

// POST /api/devices — add a device to the fleet by hand (auto-registered
// devices are created by their own first check-in instead — see
// checkin.js). Useful for pre-naming a unit before it's ever powered on,
// or logging something that isn't a real FlugVel at all.
export async function onRequestPost(context) {
  const auth = await requireAuth(context);
  if (!auth) return unauthorized();

  let body;
  try {
    body = await context.request.json();
  } catch {
    return json({ error: "Invalid request body" }, { status: 400 });
  }

  const name = (body.name || "").trim();
  if (!name) return json({ error: "Name is required" }, { status: 400 });

  const owner = (body.owner || "").trim();
  const location = (body.location || "").trim();
  const notes = (body.notes || "").trim();
  const status = ALLOWED_STATUS.has(body.status) ? body.status : "unknown";

  const id = crypto.randomUUID();

  await context.env.DB.prepare(
    `INSERT INTO devices (id, name, owner, location, status, notes, updated_at)
     VALUES (?, ?, ?, ?, ?, ?, datetime('now'))`
  ).bind(id, name, owner, location, status, notes).run();

  return json({ id }, { status: 201 });
}
