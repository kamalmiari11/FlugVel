import { requireAuth, unauthorized, json } from "../_lib/auth.js";

// The repo that publishes firmware builds (see .github/workflows/release.yml
// and src/api/update_check.cpp's UPDATE_MANIFEST_URL, which points at the
// same repo). Public repo, so the GitHub API needs no token here.
const REPO = "kamalmiari11/FlugVel";

// GET /api/releases — lists published firmware releases so the dashboard
// can offer them in the "Push update" picker (see push-update.js). Reads
// straight from GitHub's Releases API on every request rather than
// caching a copy — this only runs when an admin opens the picker, so
// there's no meaningful traffic to save.
export async function onRequestGet(context) {
  const auth = await requireAuth(context);
  if (!auth) return unauthorized();

  let res;
  try {
    res = await fetch(`https://api.github.com/repos/${REPO}/releases?per_page=20`, {
      headers: {
        // GitHub's API rejects requests with no User-Agent.
        "User-Agent": "flugvel-dashboard",
        "Accept": "application/vnd.github+json",
      },
    });
  } catch (err) {
    return json({ error: "Couldn't reach GitHub" }, { status: 502 });
  }

  if (!res.ok) {
    return json({ error: `GitHub returned ${res.status}` }, { status: 502 });
  }

  const releases = await res.json();

  // Each release's firmware binary is named flugvel_X_Y_Z.bin (see
  // release.yml's "Package the release" step) — pick that asset out and
  // shape the response the same way update_manifest.json does, so the
  // dashboard and the device are working from the same {version, url}
  // idea even though they get it from different places.
  const options = releases
    .map((r) => {
      const asset = (r.assets || []).find((a) => a.name.endsWith(".bin"));
      if (!asset) return null;
      return {
        version: r.tag_name.replace(/^v/, ""),
        tag_name: r.tag_name,
        url: asset.browser_download_url,
        notes: r.body || r.name || "",
        published_at: r.published_at,
      };
    })
    .filter(Boolean);

  return json(options);
}
