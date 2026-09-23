export function WiFiSettings({ config, setConfig, onFieldChange }) {
  const updateNetwork = (patch) => {
    setConfig((c) => ({ ...c, network: { ...c.network, ...patch } }));
    if (typeof onFieldChange === 'function') onFieldChange('network', patch);
  };

  return (
    <section className="card">
      <h2>WiFi Settings</h2>
      <form autoComplete="on">
        <div className="config-grid">
          <div className="config-item">
            <label htmlFor="wifi-hostname">Device Hostname</label>
            <input
              id="wifi-hostname"
              type="text"
              className="text-input"
              autoComplete="on"
              value={config?.network?.hostname}
              onInput={(e) => updateNetwork({ hostname: e.target.value })}
            />
          </div>
          <div className="config-item">
            <label htmlFor="wifi-ssid">WiFi SSID</label>
            <input
              id="wifi-ssid"
              type="text"
              className="text-input"
              autoComplete="on"
              value={config?.network?.ssid}
              onInput={(e) => updateNetwork({ ssid: e.target.value })}
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
              onInput={(e) => updateNetwork({ password: e.target.value })}
            />
          </div>
        </div>
      </form>
    </section>
  );
}
