import { Graph } from './Graph.jsx';
import { Controls } from './Controls.jsx';
import { PresetsCard } from './PresetsCard.jsx';
import { LedBar } from './LedBar.jsx';
import { StatusBar } from './StatusBar.jsx';
import { apiUrl } from './baseUrl.js';
import { sortTimers } from './util.js';

function ScheduleTable({ timers = [], presets = [], state }) {
  let nowMinutes = 0;
  if (state?.time === '--:--') {
    nowMinutes = -1; // Use -1 to indicate unknown time
  } else if (state?.time && /^\d{2}:\d{2}:\d{2}$/.test(state.time)) {
    const [h, m] = state.time.split(':').map(Number);
    nowMinutes = h * 60 + m;
  } else {
    const now = new Date();
    nowMinutes = now.getHours() * 60 + now.getMinutes();
  }
  let lastTimerIdx = -1;
  let lastTimerTime = -1;
  timers.forEach((timer, idx) => {
    if (!timer.enabled) return;
    let timerTime = timer.hour * 60 + timer.minute;
    if (timerTime <= nowMinutes && timerTime > lastTimerTime) {
      lastTimerTime = timerTime;
      lastTimerIdx = idx;
    }
  });
  return (
    <table className="schedule-table-inner">
      <thead>
        <tr>
          <th>Time</th>
          <th>Preset</th>
          <th>Brightness</th>
          <th>Status</th>
        </tr>
      </thead>
      <tbody>
        {timers.map((timer, idx) => {
          if (!timer.enabled && !timer.name && !timer.hour && !timer.minute)
            return null;
          let timeStr = '--:--';
          if (
            typeof timer.hour === 'number' &&
            typeof timer.minute === 'number'
          ) {
            timeStr = `${timer.hour.toString().padStart(2, '0')}:${timer.minute.toString().padStart(2, '0')}`;
          } else if (timer.time) {
            const d = new Date(timer.time);
            if (!Number.isNaN(d.getTime())) {
              timeStr = d.toLocaleTimeString([], {
                hour: '2-digit',
                minute: '2-digit',
              });
            }
          }
          let presetStr = '--';
          if (
            typeof timer.presetId === 'number' &&
            Array.isArray(presets) &&
            presets.length > 0
          ) {
            let found = presets.find((p) => p.id === timer.presetId);
            if (found?.name) {
              presetStr = found.name;
            } else {
              presetStr = `Preset ${timer.presetId}`;
            }
          }
          const statusStr = timer.enabled ? (
            <span className="timer-enabled">Enabled</span>
          ) : (
            <span className="timer-disabled">Disabled</span>
          );
          const brightStr =
            typeof timer.brightness === 'number'
              ? `${timer.brightness}%`
              : '--';
          return (
            <tr
              key={
                timer.id ?? `${timer.hour}-${timer.minute}-${timer.presetId}`
              }
              className={idx === lastTimerIdx ? 'active-timer-row' : ''}
            >
              <td>{timeStr}</td>
              <td>{presetStr}</td>
              <td>{brightStr}</td>
              <td>{statusStr}</td>
            </tr>
          );
        })}
      </tbody>
    </table>
  );
}

export function Home({
  state,
  presets,
  timers,
  effects,
  activePreset,
  ledBarRef,
  sendState,
  setTab,
  config,
  setActivePreset,
}) {
  function applyPreset(presetId) {
    fetch(apiUrl('/api/preset'), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ id: presetId, apply: true }),
    }).then(() => setActivePreset?.(presetId));
  }
  // Sort timers by time (hour, minute) for display in Home page
  const sortedTimers = Array.isArray(timers) ? sortTimers(timers) : [];

  return (
    <>
      <div className="container">
        {/* Floating LED Bar */}
        <LedBar ref={ledBarRef} />
        {/* Header */}
        <header className="header">
          <h1>🐠 Aquarium Control</h1>
          <StatusBar
            setTab={setTab}
            time={
              state && typeof state.time === 'string' ? state.time : '--:--'
            }
            power={!!state?.power}
            onTogglePower={() => sendState({ power: !state?.power })}
          />
        </header>
        {/* Quick Controls */}
        <section className="card">
          <h2>Quick Controls</h2>
          <Controls state={state} effects={effects} sendState={sendState} />
        </section>

        {/* Presets */}
        <PresetsCard
          presets={presets}
          effects={effects}
          activePreset={activePreset}
          applyPreset={applyPreset}
        />

        {/* Schedule */}
        <section className="card">
          <h2>Schedule</h2>
          <div className="sun-times">
            <div className="sun-time">
              <span>🌅 Sunrise</span>
              <span id="sunriseTime">{state.sunrise}</span>
            </div>
            <div className="sun-time">
              <span>🌇 Sunset</span>
              <span id="sunsetTime">{state.sunset}</span>
            </div>
          </div>
          <div className="schedule-table" id="scheduleTable">
            <ScheduleTable
              timers={sortedTimers}
              presets={presets}
              state={state}
            />
          </div>
          {/* Brightness graph moved to its own component */}
          <Graph
            state={state}
            timers={sortedTimers}
            presets={presets}
            config={config}
          />
          {/* Add Timer button logic can be implemented here */}
        </section>
      </div>
      <footer
        className="footer"
        style={{
          textAlign: 'center',
          marginTop: '24px',
          color: '#888',
          fontSize: '0.95em',
        }}
      >
        <span id="versionString">Version: {state.version}</span>
      </footer>
      <div style={{ height: '60px' }} />
    </>
  );
}
