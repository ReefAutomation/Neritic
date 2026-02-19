import {
  formatTransitionTime,
  steppedTransitionValue,
  rgbwToDisplayColor,
} from './util.js';

export function ColorPickers({ colors, sendState }) {
  if (!colors) return null;
  return (
    <div className="color-pickers-row">
      {colors.map((color, idx) => {
        let hex = color.length === 9 ? color.slice(0, 7) : color;
        let swatchColor = hex;
        if (color.length === 9) {
          const r = Number.parseInt(color.slice(1, 3), 16);
          const g = Number.parseInt(color.slice(3, 5), 16);
          const b = Number.parseInt(color.slice(5, 7), 16);
          const wch = Number.parseInt(color.slice(7, 9), 16);
          const [rr, gg, bb] = rgbwToDisplayColor(r, g, b, wch);
          swatchColor = `rgb(${rr},${gg},${bb})`;
        }

        return (
          <div className="control-item" key={`${color}-${idx}`}>
            <label
              style={{ display: 'flex', alignItems: 'center', gap: '1px' }}
            >
              <span
                style={{ width: '80px', fontWeight: 500, color: '#cfd8dc' }}
              >{`${['Primary', 'Secondary', 'Tertiary'][idx]} Color`}</span>
              <div
                style={{ position: 'relative', width: '148px', height: '40px' }}
              >
                <span
                  className="color-preview-swatch color-input-lookalike"
                  style={{
                    background: swatchColor,
                    width: '100%',
                    height: '100%',
                    borderRadius: '8px',
                    boxShadow: '0 0 6px #222',
                    display: 'block',
                  }}
                />
                <input
                  type="color"
                  aria-label={`${['Primary', 'Secondary', 'Tertiary'][idx]} Color`}
                  value={hex}
                  onChange={(e) => {
                    let newColors = [...colors];
                    let orig = newColors[idx];
                    let w = orig.length === 9 ? orig.slice(7, 9) : '';
                    newColors[idx] = e.target.value + w;
                    sendState({ params: { colors: newColors } });
                  }}
                  className="color-input color-picker-overlay"
                  style={{
                    opacity: 0,
                    position: 'absolute',
                    top: 0,
                    left: 0,
                    width: '100%',
                    height: '100%',
                    cursor: 'pointer',
                    margin: 0,
                    padding: 0,
                    border: 'none',
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
  const sliderReleaseHandler = (extractValue, updateObj) => (e) => {
    sendState(
      typeof updateObj === 'function'
        ? updateObj(extractValue(e))
        : { [updateObj]: extractValue(e) }
    );
  };

  return (
    <div className="control-grid">
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
          onInput={() => {}}
          onMouseUp={sliderReleaseHandler(
            (e) => Number.parseInt(e.target.value, 10),
            'brightness'
          )}
          onTouchEnd={sliderReleaseHandler(
            (e) => Number.parseInt(e.target.value, 10),
            'brightness'
          )}
        />
      </div>

      <div className="control-item full-width">
        <label>
          <span>Transition Time</span>
          <span id="transitionValue">
            {formatTransitionTime(
              typeof state.transitionTime === 'number'
                ? Math.round(state.transitionTime / 1000)
                : 0
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
            document.getElementById('transitionValue').textContent =
              formatTransitionTime(steppedTransitionValue(e.target.value));
          }}
          onMouseUp={sliderReleaseHandler(
            (e) => steppedTransitionValue(e.target.value) * 1000,
            (v) => ({ transitionTime: v })
          )}
          onTouchEnd={sliderReleaseHandler(
            (e) => steppedTransitionValue(e.target.value) * 1000,
            (v) => ({ transitionTime: v })
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
            sendState({ effect: Number.parseInt(e.target.value, 10) })
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
            document.getElementById('speedValue').textContent =
              `${e.target.value}%`;
          }}
          onMouseUp={sliderReleaseHandler(
            (e) => Number.parseInt(e.target.value, 10),
            (v) => ({ params: { speed: v } })
          )}
          onTouchEnd={sliderReleaseHandler(
            (e) => Number.parseInt(e.target.value, 10),
            (v) => ({ params: { speed: v } })
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
            document.getElementById('intensityValue').textContent =
              `${e.target.value}%`;
          }}
          onMouseUp={sliderReleaseHandler(
            (e) => Number.parseInt(e.target.value, 10),
            (v) => ({ params: { intensity: v } })
          )}
          onTouchEnd={sliderReleaseHandler(
            (e) => Number.parseInt(e.target.value, 10),
            (v) => ({ params: { intensity: v } })
          )}
        />
      </div>

      {state.params?.colors && (
        <ColorPickers colors={state.params.colors} sendState={sendState} />
      )}
    </div>
  );
}
