import { createPortal } from 'preact/compat';
import { useEffect, useRef, useState } from 'preact/hooks';
import {
  formatTransitionTime,
  steppedTransitionValue,
  rgbwToDisplayColor,
} from './util.js';

function rgbToHsv(r, g, b) {
  const max = Math.max(r, g, b);
  const d = max - Math.min(r, g, b);
  let h = 0;
  if (d) {
    if (max === r) h = ((g - b) / d) % 6;
    else if (max === g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h = (h * 60 + 360) % 360;
  }
  return { h, s: max ? d / max : 0, v: max / 255 };
}

function hsvToRgb({ h, s, v }) {
  const f = (n) => {
    const k = (n + h / 60) % 6;
    return Math.round((v - v * s * Math.max(0, Math.min(k, 4 - k, 1))) * 255);
  };
  return { r: f(5), g: f(3), b: f(1) };
}

function parseRgbw(color) {
  const n = (i) => Number.parseInt(color.slice(i, i + 2), 16) || 0;
  return { r: n(1), g: n(3), b: n(5), w: color.length === 9 ? n(7) : 0 };
}

function toHex({ r, g, b, w }, withWhite) {
  const h = (v) => v.toString(16).padStart(2, '0');
  return `#${h(r)}${h(g)}${h(b)}${withWhite ? h(w) : ''}`;
}

// Custom RGBW picker. Edits stay local; the value is sent only when closed.
function RgbwPicker({ color, label, onCommit }) {
  const [open, setOpen] = useState(false);
  const [hsv, setHsv] = useState(() => {
    const { r, g, b } = parseRgbw(color);
    return rgbToHsv(r, g, b);
  });
  const [white, setWhite] = useState(() => parseRgbw(color).w);
  const draft = { ...hsvToRgb(hsv), w: white };
  const rootRef = useRef(null);
  const popRef = useRef(null);
  const dirtyRef = useRef(false);
  const [hexText, setHexText] = useState(null);
  const [pos, setPos] = useState({ top: 0, left: 0 });
  const draftRef = useRef(draft);
  draftRef.current = draft;
  const withWhite = color.length === 9;

  useEffect(() => {
    if (open) return;
    const { r, g, b, w } = parseRgbw(color);
    setHsv(rgbToHsv(r, g, b));
    setWhite(w);
  }, [color, open]);

  const pickScreenColor = async () => {
    try {
      const { sRGBHex } = await new globalThis.EyeDropper().open();
      const { r, g, b } = parseRgbw(sRGBHex);
      dirtyRef.current = true;
      setHsv(rgbToHsv(r, g, b));
    } catch {
      // cancelled by the user
    }
  };

  const pickSv = (e) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const clamp = (v) => Math.max(0, Math.min(1, v));
    dirtyRef.current = true;
    setHsv((p) => ({
      ...p,
      s: clamp((e.clientX - rect.left) / rect.width),
      v: 1 - clamp((e.clientY - rect.top) / rect.height),
    }));
  };

  const close = () => {
    setOpen(false);
    if (!dirtyRef.current) return;
    dirtyRef.current = false;
    const next = draftRef.current;
    const hex = toHex(next, withWhite || next.w > 0);
    if (hex.toLowerCase() !== color.toLowerCase()) onCommit(hex);
  };
  const closeRef = useRef(close);
  closeRef.current = close;

  useEffect(() => {
    if (!open) return undefined;
    const onDown = (e) => {
      if (
        rootRef.current &&
        !rootRef.current.contains(e.target) &&
        !popRef.current?.contains(e.target)
      ) {
        closeRef.current();
      }
    };
    const onKey = (e) => {
      if (e.key === 'Escape' || e.key === 'Enter') closeRef.current();
    };
    document.addEventListener('pointerdown', onDown);
    document.addEventListener('keydown', onKey);
    return () => {
      document.removeEventListener('pointerdown', onDown);
      document.removeEventListener('keydown', onKey);
    };
  }, [open]);

  const [rr, gg, bb] = rgbwToDisplayColor(draft.r, draft.g, draft.b, draft.w);

  return (
    <div
      ref={rootRef}
      style={{ position: 'relative', width: '148px', height: '40px' }}
    >
      <button
        type="button"
        aria-label={label}
        aria-expanded={open}
        className="color-preview-swatch color-input-lookalike"
        onClick={() => {
          if (open) return close();
          const rect = rootRef.current.getBoundingClientRect();
          const width = 240;
          setPos({
            top: rect.bottom + 6,
            left: Math.max(
              8,
              Math.min(rect.left, globalThis.innerWidth - width - 8)
            ),
          });
          setOpen(true);
        }}
        style={{
          background: `rgb(${rr},${gg},${bb})`,
          width: '100%',
          height: '100%',
          borderRadius: '8px',
          boxShadow: '0 0 6px #222',
          display: 'block',
          cursor: 'pointer',
          border: 'none',
          padding: 0,
        }}
      />
      {open &&
        createPortal(
          <div
            ref={popRef}
            className="rgbw-picker-popover"
            style={{
              position: 'fixed',
              top: `${pos.top}px`,
              left: `${pos.left}px`,
              zIndex: 1000,
              width: '240px',
              padding: '12px',
              borderRadius: '8px',
              background: '#1e2d3d',
              boxShadow: '0 4px 16px rgba(0,0,0,0.5)',
            }}
          >
            <div
              role="presentation"
              onPointerDown={(e) => {
                e.currentTarget.setPointerCapture(e.pointerId);
                pickSv(e);
              }}
              onPointerMove={(e) => {
                if (e.currentTarget.hasPointerCapture(e.pointerId)) pickSv(e);
              }}
              style={{
                position: 'relative',
                height: '140px',
                borderRadius: '6px',
                cursor: 'crosshair',
                touchAction: 'none',
                background: `linear-gradient(to top, #000, transparent), linear-gradient(to right, #fff, hsl(${hsv.h},100%,50%))`,
              }}
            >
              <span
                style={{
                  position: 'absolute',
                  left: `${hsv.s * 100}%`,
                  top: `${(1 - hsv.v) * 100}%`,
                  width: '14px',
                  height: '14px',
                  marginLeft: '-7px',
                  marginTop: '-7px',
                  borderRadius: '50%',
                  border: '2px solid #fff',
                  boxShadow: '0 0 2px #000',
                  pointerEvents: 'none',
                }}
              />
            </div>
            <div
              style={{
                display: 'flex',
                alignItems: 'center',
                gap: '10px',
                margin: '10px 0',
              }}
            >
              {'EyeDropper' in globalThis && (
                <button
                  type="button"
                  title="Pick color from screen"
                  aria-label="Pick color from screen"
                  onClick={pickScreenColor}
                  style={{
                    background: 'none',
                    border: 'none',
                    padding: 0,
                    cursor: 'pointer',
                    color: '#cfd8dc',
                    fontSize: '18px',
                    lineHeight: 1,
                  }}
                >
                  <svg
                    width="18"
                    height="18"
                    viewBox="0 0 24 24"
                    fill="none"
                    stroke="currentColor"
                    strokeWidth="2"
                    strokeLinecap="round"
                    strokeLinejoin="round"
                    aria-hidden="true"
                  >
                    <path d="m2 22 1-1h3l9-9M3 21v-3l9-9" />
                    <path d="m15 6 3.4-3.4a2.1 2.1 0 1 1 3 3L18 9l.4.4a2.1 2.1 0 1 1-3 3l-3.8-3.8a2.1 2.1 0 1 1 3-3l.4.4Z" />
                  </svg>
                </button>
              )}
              <span
                aria-label="Color preview"
                style={{
                  width: '32px',
                  height: '32px',
                  flexShrink: 0,
                  borderRadius: '50%',
                  border: '2px solid #fff',
                  background: `rgb(${rr},${gg},${bb})`,
                }}
              />
              <div
                style={{
                  display: 'flex',
                  alignItems: 'center',
                  width: 'fit-content',
                  marginLeft: 'auto',
                  padding: '4px 8px',
                  borderRadius: '4px',
                  border: '1px solid #3a4a5c',
                  background: '#16222f',
                  color: '#cfd8dc',
                  fontFamily: 'monospace',
                  fontSize: '13px',
                }}
              >
                <span aria-hidden="true">#</span>
                <input
                  type="text"
                  aria-label={`${label} hex`}
                  spellCheck={false}
                  maxLength={8}
                  value={hexText ?? toHex(draft, true).slice(1).toUpperCase()}
                  onInput={(e) => {
                    const text = e.target.value
                      .replace(/[^0-9a-f]/gi, '')
                      .slice(0, 8)
                      .toUpperCase();
                    setHexText(text);
                    if (text.length === 6 || text.length === 8) {
                      const { r, g, b, w } = parseRgbw(`#${text}`);
                      dirtyRef.current = true;
                      setHsv(rgbToHsv(r, g, b));
                      if (text.length === 8) setWhite(w);
                    }
                  }}
                  onBlur={() => setHexText(null)}
                  style={{
                    width: '8ch',
                    textAlign: 'left',
                    border: 'none',
                    outline: 'none',
                    background: 'transparent',
                    color: 'inherit',
                    font: 'inherit',
                    padding: 0,
                  }}
                />
              </div>
            </div>
            <div style={{ margin: '0 0 10px' }}>
              <input
                type="range"
                min="0"
                max="360"
                value={Math.round(hsv.h)}
                aria-label={`${label} hue`}
                className="slider-input"
                style={{
                  width: '100%',
                  margin: 0,
                  background:
                    'linear-gradient(to right,#f00,#ff0,#0f0,#0ff,#00f,#f0f,#f00)',
                }}
                onInput={(e) => {
                  dirtyRef.current = true;
                  setHsv((p) => ({ ...p, h: Number(e.target.value) }));
                }}
              />
            </div>
            <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
              <span
                style={{
                  color: '#cfd8dc',
                  whiteSpace: 'nowrap',
                  minWidth: '84px',
                }}
              >
                White: {Math.round((white / 255) * 100)}%
              </span>
              <input
                type="range"
                min="0"
                max="100"
                value={Math.round((white / 255) * 100)}
                aria-label={`${label} white`}
                className="slider-input"
                style={{
                  flex: 1,
                  minWidth: 0,
                  margin: 0,
                  background: 'linear-gradient(to right, #000, #fff)',
                }}
                onInput={(e) => {
                  dirtyRef.current = true;
                  setWhite(Math.round((Number(e.target.value) / 100) * 255));
                }}
              />
            </div>
          </div>,
          document.body
        )}
    </div>
  );
}

export function ColorPickers({ colors, sendState }) {
  if (!colors) return null;
  return (
    <div className="color-pickers-row">
      {colors.map((color, idx) => {
        return (
          <div className="control-item" key={idx}>
            <div style={{ display: 'flex', alignItems: 'center', gap: '1px' }}>
              <span
                style={{ width: '80px', fontWeight: 500, color: '#cfd8dc' }}
              >{`${['Primary', 'Secondary', 'Tertiary'][idx]} Color`}</span>
              <RgbwPicker
                color={color}
                label={`${['Primary', 'Secondary', 'Tertiary'][idx]} Color`}
                onCommit={(v) => {
                  const newColors = [...colors];
                  newColors[idx] = v;
                  sendState({ params: { colors: newColors } });
                }}
              />
            </div>
          </div>
        );
      })}
    </div>
  );
}

export function Controls({ state, effects, sendState }) {
  const isSolid = state.effect === 0;
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
          disabled={isSolid}
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
          disabled={isSolid}
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
