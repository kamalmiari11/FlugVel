import { requireAuth, unauthorized, json } from "../../../_lib/auth.js";

// Only one remote command exists so far. "restart" (a plain reboot) was
// deliberately left OUT of this remote path — it stays a physical-only
// action for now (long-press on the device, or its Settings menu) — see
// the "factory_reset" note below for why this one command was still
// worth adding remotely despite the same device going offline afterward.
const ALLOWED_COMMANDS = new Set(["factory_reset"]);

// POST /api/devices/:id/command — queues a remote command for one device.
// Body: { command: "factory_reset" }
//
// factory_reset wipes EEPROM + NVS on the device (WiFi credentials
// included, via CaptivePortal::factoryReset() - the same code the
// on-device Settings > Factory reset menu item and the physical 15s
// button-hold both already call) and reboots into first-time setup mode.
// That means the device goes offline and CANNOT check in again until
// someone is physically there to rejoin its setup WiFi - there is no way
// back from the dashboard once this is delivered. Used deliberately
// anyway: for a unit that's stuck in a bad state no restart will fix.
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

  const command = (body.command || "").trim();
  if (!ALLOWED_COMMANDS.has(command)) {
    return json({ error: "Unsupported command" }, { status: 400 });
  }

  const result = await context.env.DB.prepare(
    "UPDATE devices SET pending_command = ? WHERE id = ?"
  ).bind(command, id).run();

  if (result.meta.changes === 0) {
    return json({ error: "Device not found" }, { status: 404 });
  }
  return json({ ok: true });
}

// DELETE /api/devices/:id/command — cancels a queued command that hasn't
// been picked up yet.
export async function onRequestDelete(context) {
  const auth = await requireAuth(context);
  if (!auth) return unauthorized();

  const id = context.params.id;
  const result = await context.env.DB.prepare(
    "UPDATE devices SET pending_command = NULL WHERE id = ?"
  ).bind(id).run();

  if (result.meta.changes === 0) {
    return json({ error: "Device not found" }, { status: 404 });
  }
  return json({ ok: true });
}
