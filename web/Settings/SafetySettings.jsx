import { steppedTransitionValue, formatTransitionTime } from '../util.js';

export function SafetySettings({ config, setConfig }) {
  const transitionSliderPosition = (millis) => {
    const sec = Math.round(Number(millis) / 1000);
    if (sec <= 59) return sec;
    if (sec < 3600) return 59 + Math.round(sec / 60);
    return 119 + Math.round(sec / 3600);
  };

  return (
    <section className="card">
      <h2>Safety</h2>
      <div className="config-grid">
        <div className="config-item">
          <label htmlFor="max-brightness">Max Brightness (Fish Safety)</label>
          <input
            id="max-brightness"
            type="range"
            min="0"
            max="100"
            className="slider-input"
            value={config?.safety?.maxBrightness}
            onInput={(e) =>
              setConfig((c) => ({
                ...c,
                safety: { ...c.safety, maxBrightness: Number(e.target.value) },
              }))
            }
          />
          <span>{config?.safety?.maxBrightness}%</span>
        </div>
        <div className="config-item">
          <label htmlFor="min-transition-time">Min Transition Time</label>
          <input
            id="min-transition-time"
            type="range"
            min="0"
            max="127"
            className="slider-input"
            value={transitionSliderPosition(config?.safety?.minTransitionTime)}
            onInput={(e) =>
              setConfig((c) => ({
                ...c,
                safety: {
                  ...c.safety,
                  minTransitionTime:
                    steppedTransitionValue(e.target.value) * 1000,
                },
              }))
            }
          />
          <span>
            {formatTransitionTime(
              Math.round(Number(config?.safety?.minTransitionTime) / 1000)
            )}
          </span>
        </div>
      </div>
    </section>
  );
}
