# locate — public location & timezone helper

A single static page that reads the visitor's coordinates (Geolocation API) and
timezone (`Intl`) in the browser and prints the `time` block to paste into a
Neritic controller. It is the Neritic equivalent of `locate.wled.me`.

Nothing is uploaded: the page's only network request is for its own
`timezones.json`.

```
locate/
├── wrangler.jsonc      Cloudflare Worker (static assets) config
├── README.md           this file — not published
└── public/             the published site
    ├── index.html
    ├── locate.css
    ├── locate.js
    ├── _headers        CSP + permissions hardening
    ├── favicon.svg     copied from web/public/
    ├── neritic.svg     copied from web/public/images/
    └── timezones.json  GENERATED — do not edit
```

## Behaviour worth knowing

- **Geolocation needs a secure origin.** Cloudflare serves the page over HTTPS,
  so this is satisfied; `http://localhost` also works for local checks. On plain
  HTTP the page reports the problem instead of guessing a position.
- **No fallbacks.** If permission is refused, the zone list cannot be loaded, or
  the browser reports no timezone, the page says so. It never synthesises an
  IP-based location or a substitute zone name.
- **The zone list is the firmware's list.** `public/timezones.json` is a plain
  array of the `name` values from `defaults/timezones.json`. A zone the browser
  reports but the device does not know is flagged as unlisted rather than
  silently written to the snippet.

## Regenerating `public/timezones.json`

`defaults/timezones.json` is a build artifact of `scripts/gen_timezones.py`
(regenerated before every `pio run`). Run both steps from the repository root:

```bash
source .venv/bin/activate && python scripts/gen_timezones.py
npm run build:locate
```

The script fails loudly if the source file is missing, malformed, or contains a
duplicate name — it never writes a partial list. Commit the regenerated
`locate/public/timezones.json` alongside any timezone change (the same way the
generated `defaults/timezones.json` is committed).

## Popup contract with the device web UI

`web/Settings/LocationTimeSettings.jsx` opens this page in a window (**Get from
GPS**) and waits for a reply.

| | |
| --- | --- |
| Opener | the `LOCATE_URL` constant in that file |
| Reply | `window.opener.postMessage({ lat, lon }, <opener origin>)` |
| Values | decimal degrees rounded to 6 places; lat in [-90, 90], lon in [-180, 180] |
| Accepted by | a `message` listener that requires `event.origin` to equal the locate page's origin and range-checks both values before writing the config |

The reply is addressed to the origin of the page that actually opened the popup,
resolved from `document.referrer` — never `'*'`, so the coordinates cannot reach
an unrelated opener.

## Layout

One **Coordinates** card holds the readout and every action the flow needs — the
send action deliberately does not get a card of its own:

- **Standalone** — a primary **Detect my location** button, then the timezone and
  snippet cards.
- **Popup** — the same card gains a primary **Send to the device** button and the
detect button becomes a secondary **Try again**; the timezone and snippet cards
  are hidden.
- **Popup with an unidentifiable opener** — no send button, an explicit message,
  and the snippet card stays visible so the JSON can be copied by hand.

There is no separate “copy coordinates” action; the snippet's **Copy JSON** is the
only copy path.

The popup requests the position as soon as it opens — the **Get from GPS** click
is the user gesture, so there is no second button to press before the browser's
permission prompt. `requestLocation()` still backs the **Detect my location**
button, which stays on screen as the retry for browsers that refuse a
permission prompt without a fresh gesture. Auto-request only happens when
`openerOrigin` is known, so a page that cannot reply never asks for a position.

### Do not add Cross-Origin-Opener-Policy

`_headers` pins `Cross-Origin-Opener-Policy: unsafe-none` on purpose. Any other
value — `same-origin`, for instance — drops the popup into its own browsing
context group and sets `window.opener` to `null`, breaking the reply with no
visible error anywhere.

## Deploying to Cloudflare

### Option A — Workers (Wrangler)

From this directory:

```bash
npx wrangler deploy
```

`wrangler.jsonc` deploys the Worker as static assets from `./public`; no
`main` entry point is needed. Rename `name` if `neritic-locate` is taken.

### Option B — Pages (Git integration)

Create a Pages project pointing at this repository and set:

| Setting | Value |
| --- | --- |
| Build command | `npm run build:locate` |
| Build output directory | `locate/public` |

The build command only refreshes `timezones.json`. If you would rather keep the
Pages build trivial, commit the generated file and use a build command of
`none` with the same output directory — but then re-run `npm run build:locate`
by hand whenever the timezone list changes.

### `_headers` is parsed, never served

Cloudflare reads `_headers` and applies its rules to asset responses, but never
publishes the file itself as a static asset. All of the following are normal and
are **not** signs of a failed upload:

- `https://locate.lonecesito.cl/_headers` returns 404
- the file does not show up in the deployment's asset list

Check the rules on a real response instead:

```bash
curl -sI https://locate.lonecesito.cl/ \
  | grep -iE 'content-security-policy|permissions-policy|cross-origin-opener'
```

`wrangler deploy --dry-run` from this directory reports the asset count; it
should read all seven files (`index.html`, `locate.css`, `locate.js`,
`timezones.json`, `_headers`, `favicon.svg`, `neritic.svg`).

Two things that genuinely stop it working:

1. **Wrong directory level.** `_headers` has to sit at the root of the deployed
directory. Deploy the *contents* of `locate/public`, not the `locate` folder —
otherwise the file ends up at `/public/_headers` and is ignored.
2. **Not in the build.** A Git-integration build only sees committed files.

If headers must not depend on `_headers` at all, the alternative is to give the
Worker a `main` script with `assets.run_worker_first: true` and attach the
headers there — note that `_headers` rules are *not* applied to responses
generated by Worker code, so the two approaches are alternatives, not layers.

## Local check

```bash
npx serve locate/public
# or
python3 -m http.server 8000 --directory locate/public
```

Then open the printed URL. `localhost` counts as a secure origin, so the
browser will offer the location permission prompt.

To see popup mode locally, open the page with `window.opener` set — for example
from the console of another tab on the same origin:

```js
window.open('http://localhost:8000/', 'test', 'width=460,height=640');
```

After deploying, check that `LOCATE_URL` in
`web/Settings/LocationTimeSettings.jsx` matches the host you actually deployed
to, then rebuild the web UI (`npm run build`) so the firmware picks it up.
