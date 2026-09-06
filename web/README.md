# FlugVel web

The public site, user manual, and device-monitor login/dashboard for the
FlugVel project. Built as a Cloudflare Pages site: static HTML/CSS in
`public/`, a tiny JSON API in `functions/` (Cloudflare Pages Functions)
backed by a Cloudflare D1 (SQLite) database.

## What's here

```
public/
  index.html         Landing page
  manual.html        User manual / setup guide
  p-f838d1.html      Password login for the device monitor (deliberately
                     un-guessable filename - never linked from the site's
                     own nav; script.js's arrow-key sequence is the only way in)
  m-223d0f.html      Protected page listing all FlugVel devices (same
                     naming reasoning - only reachable after logging in)
  styles.css         Shared styling for all pages

functions/
  api/login.js         POST — checks the admin password, issues a JWT cookie
  api/logout.js        POST — clears the session cookie
  api/me.js            GET  — "am I logged in?" check used by m-223d0f.html
  api/devices/index.js GET (list) / POST (create)
  api/devices/[id].js  PUT (update) / DELETE
  _lib/jwt.js          Minimal HS256 JWT sign/verify (Web Crypto, no deps)
  _lib/auth.js         Cookie parsing + auth check shared by the API routes

schema.sql        D1 table definition (run once, see DEPLOY.md)
wrangler.toml     Cloudflare project config (needs your D1 database_id filled in)
```

## How auth works

There's a single admin password (an `ADMIN_PASSWORD` secret, not stored in
the database or in code). Logging in on `p-f838d1.html` posts the password to
`/api/login`; on success the server signs a JWT and sets it as an
`HttpOnly`, `Secure` cookie. Every `/api/devices*` request checks that
cookie before touching the database. There's no per-user accounts — this
is intentionally a single shared login, matching what was asked for.

## About device status

The FlugVel firmware doesn't report anything back to a server yet (it only
*pulls* flight/weather data). So for now, device status/notes on the
dashboard are entered by hand — it's a fleet log you update when you check
on a unit, not live telemetry. The API and database are already shaped so
that a future firmware "heartbeat" (a periodic HTTP POST to
`/api/devices/:id`) could update status automatically without changing the
dashboard at all.

See `DEPLOY.md` for how to actually get this live on your domain.
