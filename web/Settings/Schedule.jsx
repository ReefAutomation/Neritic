import { useState, useEffect, useRef } from 'preact/hooks';
import { sortTimers } from '../util.js';

export function Schedule({ config, setConfig, presets, sunTimes }) {
  const [displayTimers, setDisplayTimers] = useState([]);
  const didInitFromConfigRef = useRef(false);
  const nextUiKeyRef = useRef(1);

  const withUiKeys = (timers) =>
    timers.map((timer) => {
      if (timer.__uiKey) return timer;
      const uiKey = `t_${nextUiKeyRef.current++}`;
      return { ...timer, __uiKey: uiKey };
    });

  useEffect(() => {
    if (!didInitFromConfigRef.current && Array.isArray(config?.timers)) {
      setDisplayTimers(withUiKeys(sortTimers(config.timers)));
      didInitFromConfigRef.current = true;
    }
  }, [config?.timers]);

  const updateTimers = (timers) => {
    setDisplayTimers(timers);
    const timersForConfig = timers.map(({ __uiKey, ...rest }) => rest);
    setConfig((prev) => ({ ...prev, timers: timersForConfig }));
  };

  return (
    <section class="card">
      <h2>Schedule</h2>
      <div class="schedule-table">
        <table>
          <thead>
            <tr>
              <th>Enabled</th>
              <th>Type</th>
              <th>Time</th>
              <th>Preset</th>
              <th>Brightness (%)</th>
              <th>Actions</th>
            </tr>
          </thead>
          <tbody>
            {displayTimers.length > 0
              ? displayTimers.map((timer, idx) => {
                  let timeCell;
                  if (timer.type === 1) {
                    timeCell = <span>{sunTimes.sunrise || ''}</span>;
                  } else if (timer.type === 2) {
                    timeCell = <span>{sunTimes.sunset || ''}</span>;
                  } else {
                    timeCell = (
                      <input
                        type="time"
                        value={`${String(timer.hour).padStart(2, '0')}:${String(timer.minute).padStart(2, '0')}`}
                        onChange={(e) => {
                          const [h, m] = e.target.value.split(':').map(Number);
                          const timers = [...displayTimers];
                          timers[idx] = {
                            ...timers[idx],
                            hour: h || 0,
                            minute: m || 0,
                          };
                          updateTimers(timers);
                        }}
                      />
                    );
                  }

                  return (
                  <tr key={timer.__uiKey}>
                    {/* Enabled checkbox */}
                    <td>
                      <input
                        type="checkbox"
                        checked={!!timer.enabled}
                        onInput={(e) => {
                          const timers = [...displayTimers];
                          timers[idx] = {
                            ...timers[idx],
                            enabled: e.target.checked,
                          };
                          updateTimers(timers);
                        }}
                      />
                    </td>
                    {/* Type radio group */}
                    <td>
                      {['Regular', 'Sunrise', 'Sunset'].map((label, val) => (
                        <label style={{ marginRight: '0.5em' }} key={label}>
                          <input
                            type="radio"
                            name={`type_${timer.__uiKey}`}
                            value={val}
                            checked={timer.type === val}
                            onInput={() => {
                              const timers = [...displayTimers];
                              // Prevent multiple Sunrise/Sunset
                              if (val === 1 || val === 2) {
                                for (let i = 0; i < timers.length; i++) {
                                  if (i !== idx && timers[i].type === val) {
                                    timers[i] = { ...timers[i], type: 0 };
                                  }
                                }
                              }
                              timers[idx] = {
                                ...timers[idx],
                                type: val,
                              };
                              updateTimers(timers);
                            }}
                          />
                          {label}
                        </label>
                      ))}
                    </td>
                    {/* Time input or sunrise/sunset label */}
                    <td>{timeCell}</td>
                    {/* Preset select */}
                    <td>
                      <select
                        value={timer.presetId}
                        onInput={(e) => {
                          const timers = [...displayTimers];
                          timers[idx] = {
                            ...timers[idx],
                            presetId: Number.parseInt(e.target.value, 10),
                          };
                          updateTimers(timers);
                        }}
                      >
                        {presets.length > 0 ? (
                          presets.map((preset) => (
                            <option value={preset.id} key={preset.id}>
                              {preset.name || `Preset ${preset.id}`}
                            </option>
                          ))
                        ) : (
                          <option value={0}>No presets available</option>
                        )}
                      </select>
                    </td>
                    {/* Brightness input */}
                    <td>
                      <input
                        type="number"
                        min="0"
                        max="100"
                        value={timer.brightness}
                        style={{ width: '60px' }}
                        onChange={(e) => {
                          const val = Math.max(
                            0,
                            Math.min(
                              100,
                              Number.parseInt(e.target.value, 10) || 0
                            )
                          );
                          const timers = [...displayTimers];
                          timers[idx] = {
                            ...timers[idx],
                            brightness: val,
                          };
                          updateTimers(timers);
                        }}
                      />
                    </td>
                    {/* Actions (Delete button) */}
                    <td>
                      <button
                        class="btn btn-danger"
                        onClick={(e) => {
                          e.preventDefault();
                          const newTimers = [...displayTimers];
                          newTimers.splice(idx, 1);
                          updateTimers(newTimers);
                        }}
                      >
                        Delete
                      </button>
                    </td>
                  </tr>
                );
                })
              : null}
          </tbody>
        </table>
        <button
          class="btn btn-secondary"
          style={{ marginTop: '1em', marginRight: '1em' }}
          onClick={(e) => {
            e.preventDefault();
            // Just append, do not sort
            const newTimers = [
              ...displayTimers,
              {
                __uiKey: `t_${nextUiKeyRef.current++}`,
                enabled: true,
                type: 0,
                hour: 12,
                minute: 0,
                presetId: presets.length > 0 ? presets[0].id : 0,
                brightness: 80,
              },
            ];
            updateTimers(newTimers);
          }}
        >
          Add Timer
        </button>
      </div>
    </section>
  );
}
