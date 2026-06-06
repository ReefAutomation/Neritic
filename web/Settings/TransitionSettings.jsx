import { steppedTransitionValue, formatTransitionTime } from '../util.js';

const transitionSliderPosition = (millis) => {
  const sec = Math.round(Number(millis) / 1000);
  if (sec <= 59) return sec;
  if (sec < 3600) return 59 + Math.round(sec / 60);
  return 119 + Math.round(sec / 3600);
};

export function TransitionSettings({ config, setConfig }) {
  return (
    <section className="card">
      <h2>Transition Times</h2>
      <div className="config-grid">
        <div className="config-item">
          <label htmlFor="poweron-range">Power On</label>
          <input
            id="poweron-range"
            type="range"
            min="0"
            max="127"
            className="slider-input"
            value={transitionSliderPosition(config?.transitionTimes?.powerOn)}
            onInput={(e) =>
              setConfig((c) => ({
                ...c,
                transitionTimes: {
                  ...c.transitionTimes,
                  powerOn: steppedTransitionValue(e.target.value) * 1000,
                },
              }))
            }
          />
          <span>
            {formatTransitionTime(
              Math.round(Number(config?.transitionTimes?.powerOn) / 1000)
            )}
          </span>
        </div>
        <div className="config-item">
          <label htmlFor="schedule-range">Schedule</label>
          <input
            id="schedule-range"
            type="range"
            min="0"
            max="127"
            className="slider-input"
            value={transitionSliderPosition(config?.transitionTimes?.schedule)}
            onInput={(e) =>
              setConfig((c) => ({
                ...c,
                transitionTimes: {
                  ...c.transitionTimes,
                  schedule: steppedTransitionValue(e.target.value) * 1000,
                },
              }))
            }
          />
          <span>
            {formatTransitionTime(
              Math.round(Number(config?.transitionTimes?.schedule) / 1000)
            )}
          </span>
        </div>
        <div className="config-item">
          <label htmlFor="manual-range">Manual</label>
          <input
            id="manual-range"
            type="range"
            min="0"
            max="127"
            className="slider-input"
            value={transitionSliderPosition(config?.transitionTimes?.manual)}
            onInput={(e) =>
              setConfig((c) => ({
                ...c,
                transitionTimes: {
                  ...c.transitionTimes,
                  manual: steppedTransitionValue(e.target.value) * 1000,
                },
              }))
            }
          />
          <span>
            {formatTransitionTime(
              Math.round(Number(config?.transitionTimes?.manual) / 1000)
            )}
          </span>
        </div>
        <div className="config-item">
          <label htmlFor="effect-range">Effect</label>
          <input
            id="effect-range"
            type="range"
            min="0"
            max="127"
            className="slider-input"
            value={transitionSliderPosition(config?.transitionTimes?.effect)}
            onInput={(e) =>
              setConfig((c) => ({
                ...c,
                transitionTimes: {
                  ...c.transitionTimes,
                  effect: steppedTransitionValue(e.target.value) * 1000,
                },
              }))
            }
          />
          <span>
            {formatTransitionTime(
              Math.round(Number(config?.transitionTimes?.effect) / 1000)
            )}
          </span>
        </div>
      </div>
    </section>
  );
}
