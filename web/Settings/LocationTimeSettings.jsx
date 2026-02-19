export function LocationTimeSettings({ config, setConfig, timezones }) {
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
              <option key={tz} value={tz}>
                {tz}
              </option>
            ))}
          </select>
        </div>
        <div className="config-item">
          <label>
            <input
              type="checkbox"
              checked={!!config?.time?.dstEnabled}
              onInput={(e) =>
                setConfig((c) => ({
                  ...c,
                  time: { ...c.time, dstEnabled: e.target.checked },
                }))
              }
            />{' '}
            Summer Time (DST)
          </label>
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
        <button
          type="button"
          className="btn btn-info"
          onClick={() => {
            // Open GPS popup and listen for message
            const w = 500,
              h = 500;
            const left = window.screenX + (window.outerWidth - w) / 2;
            const top = window.screenY + (window.outerHeight - h) / 2;
            const popup = window.open(
              'https://locate.wled.me',
              'wled_gps',
              `width=${w},height=${h},left=${left},top=${top},resizable,scrollbars`
            );
            if (popup) popup.focus();
            function handleMessage(event) {
              if (event.origin !== 'https://locate.wled.me') return;
              if (
                event.data &&
                typeof event.data === 'object' &&
                'lat' in event.data &&
                'lon' in event.data
              ) {
                setConfig((c) => ({
                  ...c,
                  time: {
                    ...c.time,
                    latitude: event.data.lat,
                    longitude: event.data.lon,
                  },
                }));
                if (popup && !popup.closed) popup.close();
                window.removeEventListener('message', handleMessage);
              }
            }
            window.addEventListener('message', handleMessage);
          }}
        >
          Get from GPS
        </button>
      </div>
    </section>
  );
}
