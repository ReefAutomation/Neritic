import { useState, useEffect, useRef } from 'preact/hooks';
import { apiUrl } from './baseUrl.js';
import { initializeWebSocket } from './websocket.js';
import { ToastContainer, useToast } from './Toast.jsx';
import { useTabs, getHandshakeType, Tabs } from './Tabs.jsx';

function sendState(updates) {
  fetch(apiUrl('/api/state'), {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(updates),
  });
}

export function App() {
  // OTA progress state (shared for Config)
  const [otaProgress, setOtaProgress] = useState(-1); // -1: hidden, 0-100: progress
  // Home state (move up so it's available for all hooks)
  const [state, setState] = useState({});
  // Toast state
  const [toasts, showToast, hideToast] = useToast();

  // Toast helper (App-level, always use this)
  function showToastHelper(message, opts = {}) {
    let msg = message;
    if (typeof msg === 'object' && msg !== null) {
      msg = msg.message || JSON.stringify(msg);
    }
    if (!msg || String(msg).trim() === '') return;
    const type = opts.type || 'info';
    const hideDelay = opts.hideDelay || 4000;
    return showToast(msg, { ...opts, type, hideDelay });
  }

  // Tabs logic (migrated)
  const [tab, setTab] = useTabs();

  // Shared WebSocket connection
  const wsRef = useRef(null);
  const [setWsReady] = useState(false);
  const [setWsError] = useState(null);

  // Create the WebSocket connection only once
  useEffect(() => {
    wsRef.current = initializeWebSocket({
      handshake: getHandshakeType(tab),
      onMessage: (data) => {
        setState((prev) => ({ ...prev, ...data }));
        if (data.preset !== undefined) setActivePreset(data.preset);
        if ('sunrise' in data) {
          setSunTimes((st) => ({
            sunrise: data.sunrise,
            sunset: st.sunset,
          }));
        }
        if (data.type === 'ota_status') {
          if (typeof data.progress === 'number') {
            setOtaProgress(data.progress);
            if (data.progress >= 100) {
              setTimeout(() => setOtaProgress(-1), 2000);
            }
          }
          if (data.status === 'success') {
            showToast('OTA update successful! Device will reboot.', {
              type: 'success',
            });
            setTimeout(() => globalThis.location.reload(), 7000);
          } else if (data.status === 'error') {
            showToast(`OTA update failed: ${  data.message}`, { type: 'error' });
            setOtaProgress(-2); // sentinel: hides progress bar + re-triggers dismiss effect in FirmwareUpdate
          }
        }
      },
      onBinary: (buffer) => {
        if (
          ledBarRef.current &&
          typeof ledBarRef.current.updateBuffer === 'function'
        ) {
          ledBarRef.current.updateBuffer(buffer);
        }
      },
      onOpen: () => {
        setWsReady(true);
        setWsError(null);
      },
      onError: (err) => {
        setWsError(err);
        setWsReady(false);
      },
      onClose: () => {
        setWsReady(false);
      },
    });
    return () => {
      if (wsRef.current) wsRef.current.close();
    };
  }, [setWsError, setWsReady, showToast]);

  // Send handshake/subscription message on tab change
  useEffect(() => {
    if (wsRef.current?.readyState === 1) {
      wsRef.current.send(JSON.stringify(getHandshakeType(tab)));
    }
  }, [tab]);
  // Home state
  const [presets, setPresets] = useState([]);
  const [timers, setTimers] = useState([]);
  const [effects, setEffects] = useState([]);
  const [activePreset, setActivePreset] = useState(null);
  const ledBarRef = useRef(null);
  // Config state (moved from Config.jsx)
  const [config, setConfig] = useState(null);
  const [timezones, setTimezones] = useState([]);
  const [sunTimes, setSunTimes] = useState({ sunrise: '', sunset: '' });
  const [loaded, setLoaded] = useState({
    timezones: false,
    presets: false,
    config: false,
  });

  // Keep timers in sync with config.timers
  useEffect(() => {
    if (config && Array.isArray(config.timers)) {
      setTimers(config.timers);
    }
  }, [config]);

  // Toast helper
  // Duplicate showToast removed (already defined above)

  // Fetch shared data (presets, timers, effects, version, config, timezones) in parallel
  useEffect(() => {
    Promise.all([
      fetch(apiUrl('/api/presets'))
        .then((r) => r.json())
        .then((data) => (Array.isArray(data) ? data : data.presets))
        .catch(() => []),
      fetch(apiUrl('/api/effects'))
        .then((r) => r.json())
        .then((data) => data.effects)
        .catch(() => []),
      fetch(apiUrl('/api/version'))
        .then((r) => r.json())
        .catch(() => {}),
      fetch(apiUrl('/api/timezones'))
        .then((resp) => resp.json())
        .catch(() => []),
      fetch(apiUrl('/api/config'))
        .then(async (response) => {
          const text = await response.text();
          if (!text) return {};
          try {
            return JSON.parse(text);
          } catch {
            return {};
          }
        })
        .catch(() => {}),
    ]).then(([presets, effects, version, timezones, config]) => {
      setPresets(presets);
      setEffects(effects);
      setConfig(config);
      setTimezones(timezones);
      setTimers(config?.timers);
      setLoaded({ presets: true, timezones: true, config: true });
      // Set version string if available
      if (version?.version) {
        const vEl = document.getElementById('versionString');
        if (vEl) vEl.textContent = `Version: ${  version.version}`;
      }
    });
  }, []);

  return (
    <div>
      <ToastContainer toasts={toasts} onDismiss={hideToast} />
      <Tabs
        tab={tab}
        setTab={setTab}
        state={state}
        presets={presets}
        timers={timers}
        effects={effects}
        activePreset={activePreset}
        ledBarRef={ledBarRef}
        sendState={sendState}
        config={config}
        setActivePreset={setActivePreset}
        timezones={timezones}
        sunTimes={sunTimes}
        showToast={showToastHelper}
        hideToast={hideToast}
        loaded={loaded}
        setConfig={setConfig}
        otaProgress={otaProgress}
      />
    </div>
  );
}
