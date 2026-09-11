# Deploying flugvel

This deploys to **Cloudflare Pages** (free tier) with a **D1** database.
You'll need to do the account creation and login steps yourself — I can't
create accounts or enter passwords on your behalf — everything else here
is copy/paste commands.

Replace `flugvel.com` below with your actual domain throughout.

## 1. Create a Cloudflare account

Go to https://dash.cloudflare.com/sign-up and sign up (free plan is fine).

## 2. Install the CLI and authenticate

Run these in your own terminal on your computer (PowerShell, Command
Prompt, or WSL) — not through a sandboxed/remote shell, since Cloudflare's
API is blocked from some sandboxed environments.

From inside this `web` folder, either log in interactively:

```bash
npx wrangler login
```

(opens a browser tab — approve it there)

...or, if you already created a scoped API token (Account > Cloudflare
Pages > Edit, Account > D1 > Edit, Account > Account Settings > Read) at
https://dash.cloudflare.com/profile/api-tokens, skip the browser step
entirely:

PowerShell:
```powershell
$env:CLOUDFLARE_API_TOKEN="your-token-here"
```

macOS/Linux:
```bash
export CLOUDFLARE_API_TOKEN="your-token-here"
```

Every `wrangler` command below then authenticates automatically using
that token for the rest of the terminal session.

## 3. Create the database

```bash
wrangler d1 create flugvel
```

This prints a `database_id`. Open `wrangler.toml` and paste it in, replacing
`REPLACE_WITH_YOUR_D1_DATABASE_ID`.

Then create the `devices` table:

```bash
wrangler d1 execute flugvel --remote --file=schema.sql
```

## 4. Create the Pages project and set secrets

```bash
wrangler pages project create flugvel
```

Set the secrets the API needs (pick your own values — a real password, and
a long random string for signing tokens):

```bash
wrangler pages secret put ADMIN_PASSWORD --project-name=flugvel
wrangler pages secret put JWT_SECRET --project-name=flugvel
```

For `JWT_SECRET`, something like the output of `openssl rand -hex 32` is a
good pick — you'll never need to type it again yourself.

If you want the "Get updates" email signup on the landing page to actually
send a welcome email (it'll still save signups to D1 without this, it just
won't email anyone), also set:

```bash
wrangler pages secret put RESEND_API_KEY --project-name=flugvel
wrangler pages secret put RESEND_FROM --project-name=flugvel
```

`RESEND_API_KEY` comes from your Resend account's API Keys page.
`RESEND_FROM` must be an address on a domain you've verified in Resend
(e.g. `FlugVel <updates@flugvel.com>`) — without a verified domain, Resend
restricts sending to only your own account email, which is easy to mistake
for a broken integration. See resend.com/domains.

## 5. Deploy

```bash
wrangler pages deploy public --project-name=flugvel
```

This prints a URL like `https://flugvel.pages.dev`. Open it and check:

- `/` — the landing page
- `/manual.html` — the manual
- `/p-f838d1.html` — log in with the `ADMIN_PASSWORD` you set
- `/m-223d0f.html` — should load (empty) after logging in

If the dashboard 500s or the D1 binding isn't found, go to the Cloudflare
dashboard → **Workers & Pages** → `flugvel` → **Settings** → **Functions**
→ **D1 database bindings**, and bind variable name `DB` to the `flugvel`
database by hand — some wrangler versions don't yet pick up the
`[[d1_databases]]` block in `wrangler.toml` for Pages projects.

## 6. Point your domain at it

The simplest path is to let Cloudflare manage DNS for the domain (you keep
Namecheap as the registrar — nothing about ownership changes):

1. In the Cloudflare dashboard: **Add a site** → enter `flugvel.com` →
   let it scan your existing DNS records → it gives you two nameservers
   (something like `ada.ns.cloudflare.com` / `bob.ns.cloudflare.com`).
2. In Namecheap: **Domain List** → **Manage** next to your domain →
   **Nameservers** → choose **Custom DNS** → paste in Cloudflare's two
   nameservers → save.
3. Wait for it to take effect (usually well under an hour, sometimes up to
   24h). Cloudflare emails you once it's active.
4. Back in Cloudflare: **Workers & Pages** → `flugvel` → **Custom
   domains** → **Add a custom domain** → enter `flugvel.com` (and again
   for `www.flugvel.com` if you want both). Cloudflare wires up the DNS
   records and SSL certificate automatically since it already manages the
   zone.

After that, `https://flugvel.com`, `/manual.html`, `/p-f838d1.html`, and
`/m-223d0f.html` all serve straight from this project — no separate
subdomain needed.

## Redeploying after changes

Any time you edit files in `public/` or `functions/`:

```bash
wrangler pages deploy public --project-name=flugvel
```

That's the whole update flow — no rebuild step, no server to restart.

## Adding a device

Once logged in, use **+ Add device** on the dashboard. There's no need to
touch the database directly — but if you ever want to seed a few devices
from the command line instead:

```bash
wrangler d1 execute flugvel --remote --command \
  "INSERT INTO devices (id, name, owner, location, status) VALUES ('unit-1', 'Kamal''s desk unit', 'Kamal', 'Amsterdam', 'online')"
```
