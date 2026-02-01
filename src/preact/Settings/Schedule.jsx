import { useState, useEffect, useRef } from "preact/hooks";

function sortTimers(timers) {
  return [...timers].sort((a, b) => {
    // Sunrise and Sunset types (1,2) always at the top
    if (a.type === 1 || a.type === 2) return -1;
    if (b.type === 1 || b.type === 2) return 1;
    // Otherwise, sort by hour then minute
    return (a.hour ?? 0) * 60 + (a.minute ?? 0) - ((b.hour ?? 0) * 60 + (b.minute ?? 0));
  });
}

export function Schedule({ config, setConfig, presets, sunTimes }) {
  // Always sort timers on first mount and when config.timers changes (e.g. after config upload/reset)
  const [displayTimers, setDisplayTimers] = useState(() =>
    Array.isArray(config?.timers) ? sortTimers(config.timers) : []
  );
  useEffect(() => {
    if (Array.isArray(config?.timers)) {
      // Only update if timers are different (by reference or length)
      if (config.timers !== displayTimers && config.timers.length !== displayTimers.length) {
        setDisplayTimers(sortTimers(config.timers));
      }
    }
  }, [config?.timers]);

  const updateTimers = (timers) => {
    setDisplayTimers(timers);
    setConfig((prev) => ({ ...prev, timers }));
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
              ? displayTimers.map((timer, idx) => (
                  <tr key={timer.id || idx}>
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
                            name={`type_${timer.id || idx}`}
                            value={val}
                            checked={timer.type === val}
                            onInput={(e) => {
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
                    <td>
                      {(() => {
                        if (timer.type === 1)
                          return <span>{sunTimes.sunrise || ''}</span>;
                        if (timer.type === 2)
                          return <span>{sunTimes.sunset || ''}</span>;
                        return (
                          <input
                            type="time"
                            value={`${String(timer.hour).padStart(2, '0')}:${String(timer.minute).padStart(2, '0')}`}
                            onInput={(e) => {
                              const [h, m] = e.target.value
                                .split(":")
                                .map(Number);
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
                      })()}
                    </td>
                    {/* Preset select */}
                    <td>
                      <select
                        value={timer.presetId}
                        onInput={(e) => {
                          const timers = [...displayTimers];
                          timers[idx] = {
                            ...timers[idx],
                            presetId: Number.parseInt(e.target.value),
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
                        onInput={(e) => {
                          const val = Math.max(
                            0,
                            Math.min(100, Number.parseInt(e.target.value) || 0),
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
                ))
              : null}
          </tbody>
        </table>
        <button
          class="btn btn-secondary"
          style={{ marginTop: "1em", marginRight: "1em" }}
          onClick={(e) => {
            e.preventDefault();
            // Just append, do not sort
            const newTimers = [
              ...displayTimers,
              {
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
