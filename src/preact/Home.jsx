import { Graph } from "./Graph.jsx";
import { Controls } from "./Controls.jsx";
import { PresetsCard } from "./PresetsCard.jsx";
import { LedBar } from "./LedBar.jsx";
import { LiveClock } from "./LiveClock.jsx";
import { getBaseUrl } from "./baseUrl.js";

function ScheduleTable({ timers = [], presets = [], state }) {
  let nowMinutes = 0;
  if (state?.time && /^\d{2}:\d{2}:\d{2}$/.test(state.time)) {
    const [h, m] = state.time.split(":").map(Number);
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
          let timeStr = "--:--";
          if (
            typeof timer.hour === "number" &&
            typeof timer.minute === "number"
          ) {
            timeStr = `${timer.hour.toString().padStart(2, "0")}:${timer.minute.toString().padStart(2, "0")}`;
          } else if (timer.time) {
            const d = new Date(timer.time);
            if (!Number.isNaN(d.getTime())) {
              timeStr = d.toLocaleTimeString([], {
                hour: "2-digit",
                minute: "2-digit",
              });
            }
          }
          let presetStr = "--";
          if (
            typeof timer.presetId === "number" &&
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
            typeof timer.brightness === "number"
              ? `${timer.brightness}%`
              : "--";
          return (
            <tr
              key={
                timer.id ?? `${timer.hour}-${timer.minute}-${timer.presetId}`
              }
              className={idx === lastTimerIdx ? "active-timer-row" : ""}
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
  wsError,
  wsReady,
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
      fetch(getBaseUrl() + "/api/preset", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ id: presetId, apply: true }),
      }).then(() => setActivePreset?.(presetId));
    }
  // Sort timers by time (hour, minute) for display in Home page
  const sortedTimers = Array.isArray(timers)
    ? [...timers].sort((a, b) => {
        // Sunrise and Sunset types (1,2) always at the top
        if (a.type === 1 || a.type === 2) return -1;
        if (b.type === 1 || b.type === 2) return 1;
        // Otherwise, sort by hour then minute
        return (a.hour ?? 0) * 60 + (a.minute ?? 0) - ((b.hour ?? 0) * 60 + (b.minute ?? 0));
      })
    : [];
  return (
    <>
      <div className="container">
        {wsError && <div className="toast toast-error">{wsError}</div>}
        {!wsReady && !wsError && (
          <div className="toast toast-warn">Connecting to device...</div>
        )}
        {/* Floating LED Bar */}
        <LedBar ref={ledBarRef} />
        {/* Header */}
        <header className="header">
          <h1>🐠 Aquarium Control</h1>
          <div className="status-bar">
            <span className="status-indicator" id="statusIndicator">
              ●
            </span>
            <LiveClock />
            <button
              type="button"
              className="status-config-link"
              title="Configuration"
              aria-label="Configuration"
              onClick={() => {
                setTab?.("config");
              }}
              style={{
                background: "none",
                border: "none",
                padding: 0,
                cursor: "pointer",
              }}
            >
              <span className="status-config-icon" aria-label="Configuration">
                <svg
                  className="gear-svg icon-glow-hover"
                  xmlns="http://www.w3.org/2000/svg"
                  viewBox="0 0 640 640"
                  width="24"
                  height="24"
                >
                  <path
                    className="gear-path"
                    fill="#66ccff"
                    d="M259.1 73.5C262.1 58.7 275.2 48 290.4 48L350.2 48C365.4 48 378.5 58.7 381.5 73.5L396 143.5C410.1 149.5 423.3 157.2 435.3 166.3L503.1 143.8C517.5 139 533.3 145 540.9 158.2L570.8 210C578.4 223.2 575.7 239.8 564.3 249.9L511 297.3C511.9 304.7 512.3 312.3 512.3 320C512.3 327.7 511.8 335.3 511 342.7L564.4 390.2C575.8 400.3 578.4 417 570.9 430.1L541 481.9C533.4 495 517.6 501.1 503.2 496.3L435.4 473.8C423.3 482.9 410.1 490.5 396.1 496.6L381.7 566.5C378.6 581.4 365.5 592 350.4 592L290.6 592C275.4 592 262.3 581.3 259.3 566.5L244.9 496.6C230.8 490.6 217.7 482.9 205.6 473.8L137.5 496.3C123.1 501.1 107.3 495.1 99.7 481.9L69.8 430.1C62.2 416.9 64.9 400.3 76.3 390.2L129.7 342.7C128.8 335.3 128.4 327.7 128.4 320C128.4 312.3 128.9 304.7 129.7 297.3L76.3 249.8C64.9 239.7 62.3 223 69.8 209.9L99.7 158.1C107.3 144.9 123.1 138.9 137.5 143.7L205.3 166.2C217.4 157.1 230.6 149.5 244.6 143.4L259.1 73.5zM320.3 400C364.5 399.8 400.2 363.9 400 319.7C399.8 275.5 363.9 239.8 319.7 240C275.5 240.2 239.8 276.1 240 320.3C240.2 364.5 276.1 400.2 320.3 400z"
                  />
                </svg>
              </span>
            </button>
          </div>
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
            <ScheduleTable timers={sortedTimers} presets={presets} state={state} />
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
          textAlign: "center",
          marginTop: "24px",
          color: "#888",
          fontSize: "0.95em",
        }}
      >
        <span id="versionString">Version: {state.version}</span>
      </footer>
      <div style={{ height: "60px" }}></div>
    </>
  );
}
