'use strict';

/*
 * Neritic — location & timezone helper.
 *
 * Everything runs locally in this tab: the Geolocation API and Intl supply the
 * values, and the only network request the page makes is for its own copy of
 * the supported timezone list (timezones.json, generated from
 * defaults/timezones.json by scripts/build-locate.cjs).
 *
 * There are deliberately no fallbacks: when a value cannot be obtained the page
 * says so instead of substituting a guess.
 */

const PLACEHOLDER = '—';
const DEGREE_PLACES = 6;
const NONE_OPTION_LABEL = '— select a zone —';

const els = {
  detect: document.getElementById('detect'),
  send: document.getElementById('send'),
  coordsBadge: document.getElementById('coords-badge'),
  coordsHint: document.getElementById('coords-hint'),
  coordsStatus: document.getElementById('coords-status'),
  rdLat: document.getElementById('rd-lat'),
  rdLon: document.getElementById('rd-lon'),
  rdAccuracy: document.getElementById('rd-accuracy'),
  rdAltitude: document.getElementById('rd-altitude'),
  tzCard: document.getElementById('tz-card'),
  tzBadge: document.getElementById('tz-badge'),
  rdDetected: document.getElementById('rd-detected'),
  rdPreview: document.getElementById('rd-preview'),
  tzSelect: document.getElementById('tz-select'),
  tzStatus: document.getElementById('tz-status'),
  outCard: document.getElementById('out-card'),
  snippet: document.getElementById('snippet'),
  copyJson: document.getElementById('copy-json'),
  outStatus: document.getElementById('out-status'),
};

const state = {
  lat: null,
  lon: null,
  accuracy: null,
  altitude: null,
  detectedTz: null, // zone name the browser reports
  tz: null, // zone name that will be written to the config
  zones: null, // supported zone names, null until loaded
  sent: false, // true once a popup has handed its coordinates back
};

/*
 * Popup mode.
 *
 * The device web UI opens this page in a window and waits for a {lat, lon}
 * reply. The reply is addressed to the origin of the page that actually opened
 * us — derived from the referrer — and never to '*', so the coordinates cannot
 * be read by an unrelated opener.
 */
const isPopup = window.opener !== null;

const openerOrigin = (() => {
  if (!isPopup || document.referrer === '') {
    return null;
  }
  try {
    const url = new URL(document.referrer);
    return url.protocol === 'http:' || url.protocol === 'https:' ? url.origin : null;
  } catch {
    return null;
  }
})();

const GEO_ERROR_MESSAGES = {
  1: 'Location permission was denied. Allow location access for this site in your browser settings, then try again.',
  2: 'The browser could not determine a position. Check that location services are enabled for your device and browser.',
  3: 'The location request timed out before a position came back. Try again.',
};

/* ---------- small helpers ---------- */

function setStatus(element, kind, text) {
  element.textContent = text;
  element.className = kind ? `status is-${kind}` : 'status';
}

function clearStatus(element) {
  element.textContent = '';
  element.className = 'status';
}

function setBadge(element, kind, text) {
  element.textContent = text;
  element.className = kind ? `badge is-${kind}` : 'badge';
}

function roundDegrees(value) {
  return Number(value.toFixed(DEGREE_PLACES));
}

/* ---------- coordinates ---------- */

function applyCoordinates(lat, lon, accuracy, altitude) {
  state.lat = roundDegrees(lat);
  state.lon = roundDegrees(lon);
  state.accuracy = Number.isFinite(accuracy) ? accuracy : null;
  state.altitude = Number.isFinite(altitude) ? altitude : null;
  renderCoordinates();
  renderSnippet();
}

function renderCoordinates() {
  const hasPosition = state.lat !== null && state.lon !== null;

  els.rdLat.textContent = hasPosition ? state.lat.toFixed(DEGREE_PLACES) : PLACEHOLDER;
  els.rdLon.textContent = hasPosition ? state.lon.toFixed(DEGREE_PLACES) : PLACEHOLDER;
  els.rdAccuracy.textContent =
    state.accuracy === null ? PLACEHOLDER : `± ${Math.round(state.accuracy)} m`;
  els.rdAltitude.textContent =
    state.altitude === null ? PLACEHOLDER : `${state.altitude.toFixed(1)} m`;

  if (!hasPosition) {
    setBadge(els.coordsBadge, '', 'Not detected');
  } else if (state.sent) {
    setBadge(els.coordsBadge, 'ok', 'Sent');
  } else if (openerOrigin !== null) {
    setBadge(els.coordsBadge, 'ok', 'Ready to send');
  } else {
    setBadge(els.coordsBadge, 'ok', 'From browser');
  }

  if (openerOrigin !== null) {
    els.send.disabled = !hasPosition || state.sent;
  }
}

function onGeolocationSuccess(position) {
  const coords = position.coords;
  els.detect.disabled = false;
  applyCoordinates(coords.latitude, coords.longitude, coords.accuracy, coords.altitude);
  setStatus(els.coordsStatus, 'ok', 'Position acquired from this browser.');
}

function onGeolocationError(error) {
  els.detect.disabled = false;
  const message =
    GEO_ERROR_MESSAGES[error.code] || error.message || 'The location request failed.';
  setStatus(els.coordsStatus, 'error', message);
}

function requestLocation() {
  if (!('geolocation' in navigator)) {
    setStatus(els.coordsStatus, 'error', 'This browser does not expose the Geolocation API.');
    return;
  }
  if (!window.isSecureContext) {
    setStatus(
      els.coordsStatus,
      'error',
      'Browsers only expose location on a secure origin. Open this page over HTTPS (or on localhost) and try again.'
    );
    return;
  }

  els.detect.disabled = true;
  setStatus(els.coordsStatus, 'info', 'Requesting a position from the browser…');
  navigator.geolocation.getCurrentPosition(onGeolocationSuccess, onGeolocationError, {
    enableHighAccuracy: true,
    timeout: 20000,
    maximumAge: 0,
  });
}

/* ---------- timezone ---------- */

function detectTimezone() {
  try {
    const zone = Intl.DateTimeFormat().resolvedOptions().timeZone;
    return typeof zone === 'string' && zone.length > 0 ? zone : null;
  } catch {
    return null;
  }
}

function renderPreview() {
  if (state.tz === null) {
    els.rdPreview.textContent = PLACEHOLDER;
    return;
  }

  try {
    const now = new Date();
    const clock = new Intl.DateTimeFormat(undefined, {
      timeZone: state.tz,
      hour: '2-digit',
      minute: '2-digit',
      hour12: false,
    }).format(now);
    const offsetPart = new Intl.DateTimeFormat('en-US', {
      timeZone: state.tz,
      timeZoneName: 'shortOffset',
    })
      .formatToParts(now)
      .find((part) => part.type === 'timeZoneName');

    els.rdPreview.textContent = offsetPart ? `${clock} (${offsetPart.value})` : clock;
  } catch {
    els.rdPreview.textContent = PLACEHOLDER;
  }
}

function selectZone(zone) {
  state.tz = zone === '' ? null : zone;
  els.tzSelect.value = state.tz === null ? '' : state.tz;
  renderPreview();
  renderSnippet();
}

function populateZoneSelect(zones) {
  const placeholder = document.createElement('option');
  placeholder.value = '';
  placeholder.textContent = NONE_OPTION_LABEL;

  const options = [placeholder];
  for (const zone of zones) {
    const option = document.createElement('option');
    option.value = zone;
    option.textContent = zone;
    options.push(option);
  }

  els.tzSelect.replaceChildren(...options);
  els.tzSelect.disabled = false;
}

function resolveDetectedZone() {
  const detected = state.detectedTz;

  if (detected === null) {
    setBadge(els.tzBadge, 'warn', 'Not reported');
    setStatus(
      els.tzStatus,
      'error',
      'This browser did not report a timezone name. Pick the zone that matches your location from the list above.'
    );
    selectZone('');
    return;
  }

  if (state.zones.includes(detected)) {
    setBadge(els.tzBadge, 'ok', 'In list');
    setStatus(
      els.tzStatus,
      'ok',
      `“${detected}” is part of Neritic's built-in timezone list and is pre-selected below.`
    );
    selectZone(detected);
    return;
  }

  setBadge(els.tzBadge, 'warn', 'Not in list');
  setStatus(
    els.tzStatus,
    'warn',
    `Your browser reports “${detected}”, which is not in Neritic's built-in list. Choose the zone that matches your location from the list — the device only accepts names from that list, so an unlisted name would be rejected.`
  );
  selectZone('');
}

async function loadSupportedZones() {
  els.tzSelect.disabled = true;
  setStatus(els.tzStatus, 'info', 'Loading the supported timezone list…');

  let zones;
  try {
    const response = await fetch('timezones.json', { cache: 'no-cache' });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}${response.statusText ? ` ${response.statusText}` : ''}`);
    }
    const payload = await response.json();
    if (
      !Array.isArray(payload) ||
      payload.length === 0 ||
      payload.some((name) => typeof name !== 'string' || name.length === 0)
    ) {
      throw new Error('timezones.json is not a non-empty array of zone names');
    }
    zones = payload;
  } catch (error) {
    state.zones = null;
    setBadge(els.tzBadge, 'warn', 'Unverified');
    setStatus(
      els.tzStatus,
      'error',
      `The supported timezone list could not be loaded (${error.message}). The name your browser reports is shown above, but it cannot be checked against the firmware's list — run “npm run build:locate” to regenerate it.`
    );
    els.tzSelect.replaceChildren();
    const unavailable = document.createElement('option');
    unavailable.value = '';
    unavailable.textContent = 'zone list unavailable';
    els.tzSelect.append(unavailable);
    els.tzSelect.disabled = true;
    return;
  }

  state.zones = zones;
  populateZoneSelect(zones);
  resolveDetectedZone();
}

/* ---------- configuration snippet ---------- */

function buildSnippet() {
  const time = {};
  if (state.tz !== null) {
    time.timezone = state.tz;
  }
  if (state.lat !== null && state.lon !== null) {
    time.latitude = state.lat;
    time.longitude = state.lon;
  }
  return time;
}

function renderSnippet() {
  const time = buildSnippet();
  const isEmpty = Object.keys(time).length === 0;

  els.snippet.textContent = JSON.stringify({ time }, null, 2);
  els.snippet.classList.toggle('is-empty', isEmpty);
  els.copyJson.disabled = isEmpty;

  if (isEmpty) {
    setStatus(
      els.outStatus,
      'info',
      'Detect your location or choose a timezone to fill this snippet.'
    );
  } else {
    clearStatus(els.outStatus);
  }
}

/* ---------- popup mode ---------- */

function setupPopupMode() {
  if (!isPopup) {
    return;
  }

  if (openerOrigin === null) {
    setStatus(
      els.coordsStatus,
      'error',
      'This window was opened by a page that did not identify itself, so the coordinates cannot be sent back automatically. Use “Copy JSON” below instead.'
    );
    return;
  }

  els.coordsHint.textContent = `Send the position back to ${openerOrigin} once your browser has one.`;

  // In the popup the send is the main action; detecting is only the retry.
  els.send.hidden = false;
  els.send.className = 'btn btn-primary';
  els.detect.className = 'btn btn-ghost';
  els.detect.textContent = 'Try again';

  // The popup exists only to produce coordinates, so the cards that only matter
  // on the standalone page stay out of the way.
  els.tzCard.hidden = true;
  els.outCard.hidden = true;

  // "Get from GPS" on the device page is itself the user gesture, so ask for the
  // position straight away instead of making the user press a second button.
  // Browsers that insist on a gesture for the permission prompt simply leave the
  // "Try again" button in place.
  requestLocation();
}

function sendCoordinatesToOpener() {
  if (openerOrigin === null || state.lat === null || state.lon === null) {
    return;
  }

  window.opener.postMessage({ lat: state.lat, lon: state.lon }, openerOrigin);
  state.sent = true;
  els.send.disabled = true;
  setBadge(els.coordsBadge, 'ok', 'Sent');
  setStatus(els.coordsStatus, 'ok', `Coordinates sent to ${openerOrigin}.`);
}

/* ---------- clipboard ---------- */

async function copyText(text, statusElement, successMessage) {
  if (!navigator.clipboard) {
    setStatus(
      statusElement,
      'error',
      'This browser does not offer the Clipboard API here. Select the text and copy it manually.'
    );
    return;
  }

  try {
    await navigator.clipboard.writeText(text);
    setStatus(statusElement, 'ok', successMessage);
  } catch (error) {
    setStatus(
      statusElement,
      'error',
      `Copying failed (${error.message}). Select the text and copy it manually.`
    );
  }
}

/* ---------- wiring ---------- */

function wireEvents() {
  els.detect.addEventListener('click', requestLocation);

  els.tzSelect.addEventListener('change', () => {
    const zone = els.tzSelect.value;
    selectZone(zone);

    if (state.tz === null) {
      clearStatus(els.tzStatus);
      return;
    }

    const knockedOut = state.detectedTz !== state.tz;
    setStatus(
      els.tzStatus,
      knockedOut ? 'warn' : 'ok',
      knockedOut
        ? `“${state.tz}” differs from the zone your browser reports. Make sure it matches where the light is installed.`
        : `“${state.tz}” will be written to the configuration.`
    );
  });

  els.copyJson.addEventListener('click', () => {
    copyText(els.snippet.textContent, els.outStatus, 'Snippet copied to the clipboard.');
  });

  els.send.addEventListener('click', sendCoordinatesToOpener);
}

function init() {
  state.detectedTz = detectTimezone();
  els.rdDetected.textContent = state.detectedTz === null ? PLACEHOLDER : state.detectedTz;

  wireEvents();
  setupPopupMode();
  renderCoordinates();
  renderPreview();
  renderSnippet();
  loadSupportedZones();
}

init();
