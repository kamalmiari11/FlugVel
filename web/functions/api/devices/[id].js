import { requireAuth, unauthorized, json } from "../../_lib/auth.js";

const ALLOWED_STATUS = new Set(["online", "offline", "unknown"]);

// PUT /api/devices/:id — update an existing device's fields.
export async function onRequestPut(context) {
  const auth = await requireAuth(context);
  if (!auth) return unauthorized();

  const id = context.params.id;
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

  const result = await context.env.DB.prepare(
    `UPDATE devices SET name = ?, owner = ?, location = ?, status = ?, notes = ?, updated_at = datetime('now')
     WHERE id = ?`
  ).bind(name, owner, location, status, notes, id).run();

  if (result.meta.changes === 0) {
    return json({ error: "Device not found" }, { status: 404 });
  }
  return json({ ok: true });
}

// DELETE /api/devices/:id
export async function onRequestDelete(context) {
  const auth = await requireAuth(context);
  if (!auth) return unauthorized();

  const id = context.params.id;
  const result = await context.env.DB.prepare("DELETE FROM devices WHERE id = ?").bind(id).run();

  if (result.meta.changes === 0) {
    return json({ error: "Device not found" }, { status: 404 });
  }
  return json({ ok: true });
}
