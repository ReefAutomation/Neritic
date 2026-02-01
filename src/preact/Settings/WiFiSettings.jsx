export function WiFiSettings({ config, setConfig }) {
  return (
    <section className="card">
      <h2>WiFi Settings</h2>
      <form autoComplete="on">
        <div className="config-grid">
          <div className="config-item">
            <label htmlFor="wifi-ssid">WiFi SSID</label>
            <input
              id="wifi-ssid"
              type="text"
              className="text-input"
              autoComplete="on"
              value={config?.network?.ssid}
              onInput={(e) =>
                setConfig((c) => ({
                  ...c,
                  network: { ...c.network, ssid: e.target.value },
                }))
              }
            />
          </div>
          <div className="config-item">
            <label htmlFor="wifi-password">WiFi Password</label>
            <input
              id="wifi-password"
              type="password"
              className="text-input"
              autoComplete="on"
              value={config?.network?.password}
              onInput={(e) =>
                setConfig((c) => ({
                  ...c,
                  network: { ...c.network, password: e.target.value },
                }))
              }
            />
          </div>
        </div>
      </form>
    </section>
  );
}
