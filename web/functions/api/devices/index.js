import { requireAuth, unauthorized, json } from "../../_lib/auth.js";

const ALLOWED_STATUS = new Set(["online", "offline", "unknown"]);

// GET /api/devices — list every device, newest name first.
export async function onRequestGet(context) {
  const auth = await requireAuth(context);
  if (!auth) return unauthorized();

  const { results } = await context.env.DB.prepare(
    "SELECT id, name, owner, location, status, notes, updated_at FROM devices ORDER BY name COLLATE NOCASE"
  ).all();

  return json(results);
}

// POST /api/devices — add a device to the fleet.
// Body: { name, owner?, location?, status?, notes? }
// There's no device-reported telemetry yet (the firmware doesn't phone
// home) — status here is whatever the admin sets by hand from the
// dashboard, a manual log until real reporting is added later.
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
