import { requireAuth, unauthorized, json } from "../../../_lib/auth.js";

// POST /api/devices/:id/push-update — queues a specific firmware version
// for one device. Body: { version, url } (both come straight from an
// /api/releases entry — the dashboard never lets the admin type a URL by
// hand). The device picks this up on its NEXT check-in (see checkin.js,
// which reads and clears these two columns) and flashes it silently in
// the background — no on-device prompt, no waiting for the owner to do
// anything. That's the whole point: fixing one struggling unit remotely
// without needing the person at the other end to touch it.
export async function onRequestPost(context) {
  const auth = await requireAuth(context);
  if (!auth) return unauthorized();

  const id = context.params.id;
  let body;
  try {
    body = await context.request.json();
  } catch {
    return json({ error: "Invalid request body" }, { status: 400 });
  }

  const version = (body.version || "").trim();
  const url = (body.url || "").trim();
  if (!version || !url) {
    return json({ error: "version and url are required" }, { status: 400 });
  }

  const result = await context.env.DB.prepare(
    "UPDATE devices SET pending_update_version = ?, pending_update_url = ? WHERE id = ?"
  ).bind(version, url, id).run();

  if (result.meta.changes === 0) {
    return json({ error: "Device not found" }, { status: 404 });
  }
  return json({ ok: true });
}

// DELETE /api/devices/:id/push-update — cancels a queued update that
// hasn't been picked up yet (the device only sees it on its next
// check-in, so there's a window where cancelling still matters).
export async function onRequestDelete(context) {
  const auth = await requireAuth(context);
  if (!auth) return unauthorized();

  const id = context.params.id;
  const result = await context.env.DB.prepare(
    "UPDATE devices SET pending_update_version = NULL, pending_update_url = NULL WHERE id = ?"
  ).bind(id).run();

  if (result.meta.changes === 0) {
    return json({ error: "Device not found" }, { status: 404 });
  }
  return json({ ok: true });
}
