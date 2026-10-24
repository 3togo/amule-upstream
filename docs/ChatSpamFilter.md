# Chat protection in amule and amuled

The advanced spam filter now protects incoming chat in both builds. Configure it
under **Preferences → Filters** in amule, amulegui, or the WebUI. REST clients can
PATCH `/api/v1/preferences` with:

```json
{"message_filter":{"advanced_spam_filter":true,"use_captchas":true}}
```

With the advanced filter enabled, unsolicited links and repeated unanswered
messages from non-friends are blocked. With CAPTCHA enabled as well, a peer
who writes first must solve a challenge before the original message reaches
the transcript. Friends and peers we have already written to bypass this
challenge. A correct answer releases the original message, including a link;
incorrect answers allow at most three challenges. Peers without CAPTCHA support
receive a single explanation and cannot start a conversation.

Pending messages stay hidden. Creating transcript entries for every blocked
sender would allow spam to fill the session list. The existing CAPTCHA-answer
interface continues to display challenges that other peers send to us.

## Defaults and upgrades

Both settings default to **off in amuled** when their configuration keys are
absent. The desktop build keeps its existing defaults of on. Saved values of
`/eMule/AdvancedSpamFilter` and `/eMule/MessageUseCaptchas` are preserved.
Older daemon versions may already have saved these keys as `1`, even though
they did not apply the protection. Such saved values now enable it; review the
Filters preferences when upgrading. Disabling the advanced filter also bypasses
CAPTCHA without changing the saved CAPTCHA sub-option.

The REST field `message_filter.advanced_spam_filter_supported` is read-only.
It is false when connected to an older daemon; writes to the two new options
then return HTTP 409. New desktop clients disable those controls for older
daemons. Older clients saving the message-filter preferences leave both new
settings unchanged.

## Validation

`ChatCaptchaGeneratorTest` checks all 33 glyphs with extreme sizes, rotations and
vertical offsets against the shared BMP validator, using wx base only.
`ChatSpamFilterIntegrationTest` starts an isolated daemon and simulated local
peers, covering default-off behavior, EC preference compatibility and persistence,
challenges, retries, exhaustion, unsupported peers, spam-session removal and
user-initiated conversations. Debug builds also check successful answers using
the existing private diagnostic log. `ChatCaptchaIntegrationTest` covers REST
preference round trips and the existing CAPTCHA-answering path.

The image layout uses generous letter spacing and sparse pixel noise, informed
by the eMuleBB generator. Rendering remains independent of system fonts and
GUI libraries. The generated images use the existing `OP_CHATCAPTCHAREQ` / `OP_CHATCAPTCHARES`
wire format. Tests with actual eMule 0.70b and independent aMule installations
are still required to establish live interoperability.
