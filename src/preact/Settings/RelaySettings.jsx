export function RelaySettings({ config, setConfig }) {
  return (
    <section className="card">
      <h2>Relay Settings</h2>
      <div className="config-grid">
        <div className="config-item">
          <label htmlFor="relay-pin">Relay Pin (GPIO)</label>
          <input
            id="relay-pin"
            type="number"
            min="0"
            max="39"
            className="text-input"
            value={config?.led?.relayPin}
            onInput={(e) =>
              setConfig((c) => ({
                ...c,
                led: { ...c.led, relayPin: Number(e.target.value) },
              }))
            }
          />
        </div>
        <div className="config-item">
          <label htmlFor="relay-active-level">Relay Active Level</label>
          <select
            id="relay-active-level"
            className="select-input"
            value={String(config?.led?.relayActiveHigh)}
            onInput={(e) =>
              setConfig((c) => ({
                ...c,
                led: { ...c.led, relayActiveHigh: e.target.value === "true" },
              }))
            }
          >
            <option value="true">HIGH = ON</option>
            <option value="false">LOW = ON</option>
          </select>
        </div>
      </div>
    </section>
  );
}
