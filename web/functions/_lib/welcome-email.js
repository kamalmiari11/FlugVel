// Content for the "you're on the list" email sent from /api/subscribe.
//
// Both parts are functions of the unsubscribe URL rather than constants,
// because that link is signed per address (see _lib/unsubscribe-token.js) -
// there is no one string that works for every recipient.
// Split out of subscribe.js purely so that file stays readable - this one
// is mostly markup, that one is the actual endpoint logic.
//
// Resend wants both `text` and `html` on the same send: `text` is the
// plain-text part (shown by clients that don't render HTML, and used by
// spam filters as a sanity check that it roughly matches the HTML), `html`
// is what most inboxes actually render.

// Deliverability notes (applied to every FlugVel email):
// - Subject names the brand; a bare "You're on the list" reads like bait.
// - Avoid trigger phrases like "no spam" - filters score the words, not intent.
// - A hidden preheader, so inbox previews show real text instead of "[ FLUGVEL ]".
// - Say why the recipient is getting it, and keep the text part matching the HTML.
export const WELCOME_EMAIL_SUBJECT = "FlugVel updates: you're subscribed";

export const welcomeEmailText = (unsubUrl) =>
  "Hi,\n\n" +
  "Thanks for subscribing to FlugVel updates. You'll get an email when " +
  "there's new firmware or a new feature. There's no fixed schedule, only " +
  "real releases.\n\n" +
  "In the meantime, the manual shows what the device can do: " +
  "https://flugvel.com/manual\n\n" +
  "- Kamal, FlugVel\n\n" +
  "You're receiving this because this address was subscribed at " +
  "https://flugvel.com. To stop these emails, unsubscribe here:\n" +
  unsubUrl + "\n";

// Email HTML is its own dialect - no external stylesheets or web fonts (most
// inboxes strip or ignore them), everything inline, layout done with
// <table> rather than flex/grid (Outlook desktop still renders email with
// Word's engine, which ignores most modern CSS). Fonts fall back to
// whatever's actually installed rather than the site's Inter/IBM Plex Mono.
// Colors match the site's palette (styles.css :root) by value, since email
// clients won't read CSS custom properties.
export const welcomeEmailHtml = (unsubUrl) => `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>You're subscribed to FlugVel updates</title>
</head>
<body style="margin:0; padding:0; background-color:#c7cfb6;">
  <div style="display:none; max-height:0; overflow:hidden; opacity:0;">You'll get an email when there's new FlugVel firmware or a new feature.</div>
  <table role="presentation" width="100%" cellpadding="0" cellspacing="0" style="background-color:#c7cfb6;">
    <tr>
      <td align="center" style="padding:32px 16px;">
        <table role="presentation" width="100%" cellpadding="0" cellspacing="0" style="max-width:520px; background-color:#faf9f1; border-radius:8px;">
          <tr>
            <td style="padding:28px 32px 0 32px; text-align:center;">
              <span style="font-family:'Courier New', Courier, monospace; font-size:13px; letter-spacing:4px; font-weight:700; color:#23271d;">[&nbsp;FLUGVEL&nbsp;]</span>
            </td>
          </tr>
          <tr>
            <td style="padding:22px 32px 0 32px; text-align:center;">
              <h1 style="margin:0; font-family:Georgia, 'Times New Roman', serif; font-weight:normal; font-size:24px; line-height:1.3; color:#23271d;">You're subscribed</h1>
            </td>
          </tr>
          <tr>
            <td style="padding:18px 32px 0 32px;">
              <p style="margin:0 0 14px 0; font-family:Arial, Helvetica, sans-serif; font-size:15px; line-height:1.6; color:#494e3a;">Thanks for subscribing to FlugVel updates. You'll get an email when there's new firmware or a new feature. There's no fixed schedule, only real releases.</p>
              <p style="margin:0; font-family:Arial, Helvetica, sans-serif; font-size:15px; line-height:1.6; color:#494e3a;">In the meantime, the manual shows what the device can do.</p>
            </td>
          </tr>
          <tr>
            <td style="padding:24px 32px 0 32px; text-align:center;">
              <a href="https://flugvel.com/manual" style="display:inline-block; background-color:#c05a1e; color:#ffffff; font-family:Arial, Helvetica, sans-serif; font-size:14px; font-weight:700; text-decoration:none; padding:12px 28px; border-radius:5px;">Read the manual</a>
            </td>
          </tr>
          <tr>
            <td style="padding:24px 32px 0 32px;">
              <p style="margin:0; font-family:Arial, Helvetica, sans-serif; font-size:15px; line-height:1.6; color:#494e3a;">&ndash; Kamal, FlugVel</p>
            </td>
          </tr>
          <tr>
            <td style="padding:20px 32px 26px 32px;">
              <p style="margin:0; padding-top:16px; border-top:1px solid #dcdccf; font-family:Arial, Helvetica, sans-serif; font-size:12px; line-height:1.6; color:#6b7058; text-align:center;">You're receiving this because this address was subscribed at <a href="https://flugvel.com" style="color:#6b7058;">flugvel.com</a>. <a href="${unsubUrl}" style="color:#6b7058;">Unsubscribe</a> at any time.</p>
            </td>
          </tr>
        </table>
      </td>
    </tr>
  </table>
</body>
</html>
`;
