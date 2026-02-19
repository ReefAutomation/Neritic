export function LEDSettings({ config, setConfig }) {
  return (
    <section className="card">
      <h2>LED Settings</h2>
      <div className="config-grid">
        <div className="config-item">
          <label htmlFor="led-pin">LED Pin (GPIO)</label>
          <input
            id="led-pin"
            type="number"
            min="0"
            max="39"
            className="text-input"
            value={config?.led?.pin}
            onInput={(e) =>
              setConfig((c) => ({
                ...c,
                led: { ...c.led, pin: Number(e.target.value) },
              }))
            }
          />
        </div>
        <div className="config-item">
          <label htmlFor="led-count">LED Count</label>
          <input
            id="led-count"
            type="number"
            min="1"
            max="512"
            className="text-input"
            value={config?.led?.count}
            onInput={(e) =>
              setConfig((c) => ({
                ...c,
                led: { ...c.led, count: Number(e.target.value) },
              }))
            }
          />
        </div>
        <div className="config-item">
          <label htmlFor="led-type">LED Type</label>
          <select
            id="led-type"
            className="select-input"
            value={config?.led?.type}
            onInput={(e) =>
              setConfig((c) => ({
                ...c,
                led: { ...c.led, type: e.target.value },
              }))
            }
          >
            <option value="WS2812B">WS2812B</option>
            <option value="SK6812">SK6812 (GRB)</option>
          </select>
        </div>
        <div className="config-item">
          <label htmlFor="led-color-order">Color Order</label>
          <select
            id="led-color-order"
            className="select-input"
            value={config?.led?.colorOrder}
            onInput={(e) =>
              setConfig((c) => ({
                ...c,
                led: { ...c.led, colorOrder: e.target.value },
              }))
            }
          >
            <option value="GRB">GRB</option>
            <option value="RGB">RGB</option>
            <option value="RGBW">RGBW</option>
            <option value="GRBW">GRBW</option>
          </select>
        </div>
      </div>
    </section>
  );
}
