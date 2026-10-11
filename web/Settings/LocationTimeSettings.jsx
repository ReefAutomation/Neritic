import { useEffect, useRef } from 'preact/hooks';
import { formatTimezoneOption } from '../util.js';

// The locate helper is a standalone static page (see the locate/ folder in this
// repo) that runs the browser's Geolocation API and posts { lat, lon } back to
// whichever page opened it. Update LOCATE_URL when it is deployed elsewhere.
const LOCATE_URL = 'https://locate.lonecesito.cl/';
const LOCATE_ORIGIN = new URL(LOCATE_URL).origin;

export function LocationTimeSettings({ config, setConfig, timezones, showToast }) {
  const popupRef = useRef(null);

  // One listener for the lifetime of the component. It only accepts a reply from
  // the locate page's own origin, and only writes the config once the values
  // have been checked against the valid latitude/longitude ranges.
  useEffect(() => {
    const onMessage = (event) => {
      if (event.origin !== LOCATE_ORIGIN) return;

      const data = event.data;
      if (!data || typeof data !== 'object') return;

      const { lat, lon } = data;
      if (
        !Number.isFinite(lat) ||
        !Number.isFinite(lon) ||
        lat < -90 ||
        lat > 90 ||
        lon < -180 ||
        lon > 180
      ) {
        showToast(
          'The locate page returned coordinates outside the valid latitude/longitude range.',
          { type: 'error' }
        );
        return;
      }

      setConfig((c) => ({
        ...c,
        time: { ...c.time, latitude: lat, longitude: lon },
      }));
      showToast('Coordinates received from the locate page.', {
        type: 'success',
      });

      if (popupRef.current && !popupRef.current.closed) {
        popupRef.current.close();
      }
      popupRef.current = null;
    };

    window.addEventListener('message', onMessage);
    return () => window.removeEventListener('message', onMessage);
  }, [setConfig, showToast]);

  const handleGetFromGps = () => {
    const width = 460;
    const height = 640;
    const left = window.screenX + (window.outerWidth - width) / 2;
    const top = window.screenY + (window.outerHeight - height) / 2;

    const popup = window.open(
      LOCATE_URL,
      'neritic_locate',
      `width=${width},height=${height},left=${left},top=${top},resizable,scrollbars`
    );

    if (!popup) {
      showToast(
        'The browser blocked the popup window. Allow popups for this page, then try again.',
        { type: 'error' }
      );
      return;
    }

    popupRef.current = popup;
    popup.focus();
  };

  return (
    <section className="card">
      <h2>Location & Time</h2>
      <div className="config-grid">
        <div className="config-item">
          <label htmlFor="ntpServer-input">NTP Server</label>
          <input
            id="ntpServer-input"
            type="text"
            className="text-input"
            value={config?.time?.ntpServer}
            onInput={(e) =>
              setConfig((c) => ({
                ...c,
                time: { ...c.time, ntpServer: e.target.value },
              }))
            }
          />
        </div>
        <div className="config-item">
          <label htmlFor="timezone-select">Timezone</label>
          <select
            id="timezone-select"
            className="select-input"
            value={config?.time?.timezone}
            onInput={(e) =>
              setConfig((c) => ({
                ...c,
                time: { ...c.time, timezone: e.target.value },
              }))
            }
          >
            {timezones.map((tz) => (
              <option key={tz.name} value={tz.name}>
                {formatTimezoneOption(tz)}
              </option>
            ))}
          </select>
        </div>
        <div className="config-item">
          <label htmlFor="latitude-input">Latitude</label>
          <input
            id="latitude-input"
            type="number"
            min="-90"
            max="90"
            step="any"
            className="text-input"
            value={config?.time?.latitude}
            onInput={(e) =>
              setConfig((c) => ({
                ...c,
                time: { ...c.time, latitude: Number(e.target.value) },
              }))
            }
          />
        </div>
        <div className="config-item">
          <label htmlFor="longitude-input">Longitude</label>
          <input
            id="longitude-input"
            type="number"
            min="-180"
            max="180"
            step="any"
            className="text-input"
            value={config?.time?.longitude}
            onInput={(e) =>
              setConfig((c) => ({
                ...c,
                time: { ...c.time, longitude: Number(e.target.value) },
              }))
            }
          />
        </div>
      </div>
      <div
        style={{
          display: 'flex',
          justifyContent: 'flex-end',
          marginTop: '0.5em',
        }}
      >
        <button type="button" className="btn btn-info" onClick={handleGetFromGps}>
          Get from GPS
        </button>
      </div>
    </section>
  );
}
