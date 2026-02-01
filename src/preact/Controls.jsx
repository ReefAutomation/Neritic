import { formatTransitionTime, steppedTransitionValue } from "./util.js";

export function ColorPickers({ colors, sendState }) {
  if (!colors) return null;
  return (
    <div className="color-pickers-row">
      {colors.map((color, idx) => {
        let hex = color.length === 9 ? color.slice(0, 7) : color;
        let w = color.length === 9 ? color.slice(7, 9) : "";
        let swatchColor = hex;
        if (hex.toLowerCase() === "#000000" && w.toLowerCase() === "ff") {
          swatchColor = "#ffffff";
        }
        // Use a more unique key, e.g., color value plus index fallback
        return (
          <div className="control-item" key={color + "-" + idx}>
            <label
              style={{ display: "flex", alignItems: "center", gap: "1px" }}
            >
              <span
                style={{ width: "80px", fontWeight: 500, color: "#cfd8dc" }}
              >{`${["Primary", "Secondary", "Tertiary"][idx]} Color`}</span>
              <div
                style={{ position: "relative", width: "148px", height: "40px" }}
              >
                <span
                  className="color-preview-swatch color-input-lookalike"
                  style={{
                    background: swatchColor,
                    width: "100%",
                    height: "100%",
                    borderRadius: "8px",
                    boxShadow: "0 0 6px #222",
                    display: "block",
                  }}
                ></span>
                <input
                  type="color"
                  aria-label={`${["Primary", "Secondary", "Tertiary"][idx]} Color`}
                  value={swatchColor}
                  onChange={(e) => {
                    let newColors = [...colors];
                    let orig = newColors[idx];
                    let w = orig.length === 9 ? orig.slice(7, 9) : "";
                    newColors[idx] = e.target.value + w;
                    sendState({ params: { colors: newColors } });
                  }}
                  className="color-input color-picker-overlay"
                  style={{
                    opacity: 0,
                    position: "absolute",
                    top: 0,
                    left: 0,
                    width: "100%",
                    height: "100%",
                    cursor: "pointer",
                    margin: 0,
                    padding: 0,
                    border: "none",
                  }}
                />
              </div>
            </label>
          </div>
        );
      })}
    </div>
  );
}

export function Controls({ state, effects, sendState }) {
  // Generic handler to avoid duplication for slider POST on release
  const sliderReleaseHandler = (extractValue, updateObj) => (e) => {
    sendState(
      typeof updateObj === "function"
        ? updateObj(extractValue(e))
        : { [updateObj]: extractValue(e) },
    );
  };

  return (
    <div className="control-grid">
      <div className="control-item">
        <div className="switch-label">
          <label htmlFor="powerToggle">
            <span>Power</span>
          </label>
          <label className="switch" htmlFor="powerToggle">
            <span
              style={{
                position: "absolute",
                width: 1,
                height: 1,
                padding: 0,
                margin: -1,
                overflow: "hidden",
                clip: "rect(0,0,0,0)",
                border: 0,
              }}
            >
              Power toggle
            </span>
            <input
              type="checkbox"
              id="powerToggle"
              checked={!!state.power}
              onChange={(e) => sendState({ power: e.target.checked })}
              aria-label="Power toggle"
            />
            <span className="slider"></span>
          </label>
        </div>
      </div>

      <div className="control-item full-width">
        <label>
          <span>Brightness</span>
          <span id="brightnessValue">{state.brightness}%</span>
        </label>
        <input
          type="range"
          id="brightnessSlider"
          min="0"
          max="100"
          value={state.brightness}
          className="slider-input"
          onInput={(e) => {
            document.getElementById("brightnessValue").textContent =
              e.target.value + "%";
          }}
          onMouseUp={sliderReleaseHandler(
            (e) => Number.parseInt(e.target.value),
            "brightness",
          )}
          onTouchEnd={sliderReleaseHandler(
            (e) => Number.parseInt(e.target.value),
            "brightness",
          )}
        />
      </div>

      <div className="control-item full-width">
        <label>
          <span>Transition Time</span>
          <span id="transitionValue">
            {formatTransitionTime(
              typeof state.transitionTime === "number"
                ? Math.round(state.transitionTime / 1000)
                : 0,
            )}
          </span>
        </label>
        <input
          type="range"
          id="transitionSlider"
          min="0"
          max="127"
          value={(() => {
            const ms = Number(state.transitionTime);
            const sec = Math.round(ms / 1000);
            if (sec <= 59) return sec;
            if (sec < 3600) return 59 + Math.round(sec / 60);
            return 119 + Math.round(sec / 3600);
          })()}
          className="slider-input"
          step="1"
          onInput={(e) => {
            document.getElementById("transitionValue").textContent =
              formatTransitionTime(steppedTransitionValue(e.target.value));
          }}
          onMouseUp={sliderReleaseHandler(
            (e) => steppedTransitionValue(e.target.value) * 1000,
            (v) => ({ transitionTime: v }),
          )}
          onTouchEnd={sliderReleaseHandler(
            (e) => steppedTransitionValue(e.target.value) * 1000,
            (v) => ({ transitionTime: v }),
          )}
        />
      </div>

      <div className="control-item full-width">
        <label htmlFor="effectSelect">Effect</label>
        <select
          id="effectSelect"
          className="select-input"
          value={state.effect}
          onChange={(e) =>
            sendState({ effect: Number.parseInt(e.target.value) })
          }
        >
          {effects.map((effect) => (
            <option key={effect.id} value={effect.id}>
              {effect.name}
            </option>
          ))}
        </select>
      </div>

      <div className="control-item">
        <label>
          <span>Speed</span>
          <span id="speedValue">{state.params?.speed}%</span>
        </label>
        <input
          type="range"
          id="speedSlider"
          min="1"
          max="100"
          value={state.params?.speed}
          className="slider-input"
          onInput={(e) => {
            document.getElementById("speedValue").textContent =
              e.target.value + "%";
          }}
          onMouseUp={sliderReleaseHandler(
            (e) => Number.parseInt(e.target.value),
            (v) => ({ params: { speed: v } }),
          )}
          onTouchEnd={sliderReleaseHandler(
            (e) => Number.parseInt(e.target.value),
            (v) => ({ params: { speed: v } }),
          )}
        />
      </div>
      <div className="control-item">
        <label>
          <span>Intensity</span>
          <span id="intensityValue">{state.params?.intensity}%</span>
        </label>
        <input
          type="range"
          id="intensitySlider"
          min="1"
          max="100"
          value={state.params?.intensity}
          className="slider-input"
          onInput={(e) => {
            document.getElementById("intensityValue").textContent =
              e.target.value + "%";
          }}
          onMouseUp={sliderReleaseHandler(
            (e) => Number.parseInt(e.target.value),
            (v) => ({ params: { intensity: v } }),
          )}
          onTouchEnd={sliderReleaseHandler(
            (e) => Number.parseInt(e.target.value),
            (v) => ({ params: { intensity: v } }),
          )}
        />
      </div>
      {state.params?.colors && <ColorPickers colors={state.params.colors} />}
    </div>
  );
}
