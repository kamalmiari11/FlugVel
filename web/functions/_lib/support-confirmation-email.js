// Content for the "we got your message" email sent back to whoever just
// submitted the Support page's contact form - see welcome-email.js for the
// styling conventions this mirrors (inline styles, <table> layout, no web
// fonts) and why.
//
// Deliberately does NOT echo the submitted message or name back. The form
// takes any address, so an echo lets anyone use it to send their own text
// from flugvel.com to a stranger - the classic contact-form spam relay, and
// the fastest way to get the sending domain blocklisted. The recipient
// already knows what they wrote; this only confirms it arrived.

export const SUPPORT_CONFIRMATION_SUBJECT = "FlugVel support: we got your message";

export const supportConfirmationText = () =>
  "Hi,\n\n" +
  "Thanks for contacting FlugVel support. Your message came through and a " +
  "person will read it and reply, usually within a few days.\n\n" +
  "If you think of anything to add, just reply to this email.\n\n" +
  "The manual may already cover your question: https://flugvel.com/manual\n\n" +
  "- Kamal, FlugVel\n\n" +
  "You're receiving this because this address was entered in the contact " +
  "form at https://flugvel.com/support. If that wasn't you, you can ignore " +
  "this email.\n";

export const supportConfirmationHtml = () => `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>We got your message</title>
</head>
<body style="margin:0; padding:0; background-color:#c7cfb6;">
  <div style="display:none; max-height:0; overflow:hidden; opacity:0;">Your message reached FlugVel support. A person will reply, usually within a few days.</div>
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
              <h1 style="margin:0; font-family:Georgia, 'Times New Roman', serif; font-weight:normal; font-size:24px; line-height:1.3; color:#23271d;">We got your message</h1>
            </td>
          </tr>
          <tr>
            <td style="padding:18px 32px 0 32px;">
              <p style="margin:0 0 14px 0; font-family:Arial, Helvetica, sans-serif; font-size:15px; line-height:1.6; color:#494e3a;">Thanks for contacting FlugVel support. Your message came through and a person will read it and reply, usually within a few days.</p>
              <p style="margin:0; font-family:Arial, Helvetica, sans-serif; font-size:15px; line-height:1.6; color:#494e3a;">If you think of anything to add, just reply to this email.</p>
            </td>
          </tr>
          <tr>
            <td style="padding:24px 32px 0 32px; text-align:center;">
              <a href="https://flugvel.com/manual" style="display:inline-block; background-color:#c05a1e; color:#ffffff; font-family:Arial, Helvetica, sans-serif; font-size:14px; font-weight:700; text-decoration:none; padding:12px 28px; border-radius:5px;">Browse the manual</a>
            </td>
          </tr>
          <tr>
            <td style="padding:24px 32px 0 32px;">
              <p style="margin:0; font-family:Arial, Helvetica, sans-serif; font-size:15px; line-height:1.6; color:#494e3a;">&ndash; Kamal, FlugVel</p>
            </td>
          </tr>
          <tr>
            <td style="padding:20px 32px 26px 32px;">
              <p style="margin:0; padding-top:16px; border-top:1px solid #dcdccf; font-family:Arial, Helvetica, sans-serif; font-size:12px; line-height:1.6; color:#6b7058; text-align:center;">You're receiving this because this address was entered in the contact form at <a href="https://flugvel.com/support" style="color:#6b7058;">flugvel.com/support</a>. If that wasn't you, you can ignore this email.</p>
            </td>
          </tr>
        </table>
      </td>
    </tr>
  </table>
</body>
</html>
`;
