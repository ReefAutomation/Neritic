import { useState, useEffect, useRef } from "preact/hooks";
import { getBaseUrl } from "./baseUrl.js";
import { initializeWebSocket } from "./websocket.js";
import { Toast, useToast } from "./Toast.jsx";
import { Home } from "./Home.jsx";
import { Config } from "./Config.jsx";

function sendState(updates) {
  fetch(getBaseUrl() + "/api/state", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(updates),
  });
}

export function App() {
  // OTA progress state (shared for Config)
  const [otaProgress, setOtaProgress] = useState(-1); // -1: hidden, 0-100: progress
  // Home state (move up so it's available for all hooks)
  const [state, setState] = useState({});
  // Toast state
  const [toast, setToast] = useToast();

  // Toast helper (App-level, always use this)
  function showToast(message, type = "info", duration = 4000) {
    // Defensive: If message is a toast-like object, flatten
    if (
      typeof message === "object" &&
      message !== null &&
      Object.hasOwn(message, "message") &&
      Object.hasOwn(message, "type") &&
      Object.hasOwn(message, "visible")
    ) {
      message = message.message || JSON.stringify(message);
    }
    let msg = message;
    if (typeof msg === "object" && msg !== null) {
      msg = msg.message || JSON.stringify(msg);
    }
    if (!msg || String(msg).trim() === "") return;
    setToast({ message: msg, type, visible: true });
    setTimeout(
      () => setToast((t) => ({ ...t, visible: false, message: "" })),
      duration,
    );
  }
  // Tab state from URL hash, fallback to localStorage, then home
  const getInitialTab = () => {
    if (globalThis.location.hash === "#config") return "config";
    if (globalThis.location.hash === "#home") return "home";
    let saved;
    try {
      saved = localStorage.getItem("deepglow_tab");
    } catch (e) {
      console.error("Error accessing localStorage for deepglow_tab:", e);
    }
    if (saved === "config" || saved === "home") return saved;
    return "home";
  };
  const [tab, setTab] = useState(getInitialTab);
  // Update tab when hash changes
  useEffect(() => {
    const onHashChange = () => {
      if (globalThis.location.hash === "#config") setTab("config");
      else setTab("home");
    };
    globalThis.addEventListener("hashchange", onHashChange);
    return () => globalThis.removeEventListener("hashchange", onHashChange);
  }, []);
  // Persist tab to localStorage and URL hash on change
  useEffect(() => {
    try {
      if (tab === "config") {
        localStorage.setItem("deepglow_tab", "config");
        globalThis.location.hash = "#config";
      } else {
        localStorage.setItem("deepglow_tab", "home");
        globalThis.location.hash = "#home";
      }
    } catch {}
  }, [tab]);
  // Shared WebSocket connection
  const wsRef = useRef(null);
  const [wsReady, setWsReady] = useState(false);
  const [wsError, setWsError] = useState(null);

  // Create the WebSocket connection only once
  useEffect(() => {
    wsRef.current = initializeWebSocket({
      handshake: { type: tab === "config" ? "ota_client" : "state" },
      onMessage: (data) => {
        setState((prev) => ({ ...prev, ...data }));
        if (data.preset !== undefined) setActivePreset(data.preset);
        if ("sunrise" in data) {
          setSunTimes((st) => ({
            sunrise: data.sunrise,
            sunset: st.sunset,
          }));
        }
        if (data.type === "ota_status") {
          if (typeof data.progress === "number") {
            setOtaProgress(data.progress);
            if (data.progress >= 100) {
              setTimeout(() => setOtaProgress(-1), 2000);
            }
          }
          if (data.status === "success") {
            showToast("OTA update successful! Device will reboot.", "success");
            setTimeout(() => globalThis.location.reload(), 7000);
          } else if (data.status === "error") {
            showToast("OTA update failed: " + data.message, "error");
          }
        }
      },
      onBinary: (buffer) => {
        if (
          ledBarRef.current &&
          typeof ledBarRef.current.updateBuffer === "function"
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
  }, []);

  // Only send handshake/subscription message on tab change
  useEffect(() => {
    wsRef.current?.readyState === 1 &&
      wsRef.current.send(
        JSON.stringify({ type: tab === "config" ? "ota_client" : "state" }),
      );
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
  const [sunTimes, setSunTimes] = useState({ sunrise: "", sunset: "" });
  const [loaded, setLoaded] = useState({
    timezones: false,
    presets: false,
    config: false,
  });

  // Toast helper
  // Duplicate showToast removed (already defined above)

  // Fetch shared data (presets, timers, effects, version, config, timezones) in parallel
  useEffect(() => {
    Promise.all([
      fetch(getBaseUrl() + "/api/presets")
        .then((r) => r.json())
        .then((data) => (Array.isArray(data) ? data : data.presets))
        .catch(() => []),
      fetch(getBaseUrl() + "/api/effects")
        .then((r) => r.json())
        .then((data) => data.effects)
        .catch(() => []),
      fetch(getBaseUrl() + "/api/version")
        .then((r) => r.json())
        .catch(() => {}),
      fetch(getBaseUrl() + "/api/timezones")
        .then((resp) => resp.json())
        .catch(() => []),
      fetch(getBaseUrl() + "/api/config")
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
        const vEl = document.getElementById("versionString");
        if (vEl) vEl.textContent = "Version: " + version.version;
      }
    });
  }, []);

  // Tab UI
  // ...existing code...

  // Defensive normalization: if toast.message is an object (unexpected),
  // convert to a display string and prefer any nested `type`.
  const [_displayMessage, _displayType] = (() => {
    const msg = toast.message;
    if (typeof msg === "object" && msg !== null) {
      let messageStr;
      if (typeof msg.message === "string") {
        messageStr = msg.message;
      } else {
        try {
          messageStr = JSON.stringify(msg);
        } catch (e) {
          console.error("Error stringifying toast message:", e);
          messageStr = String(msg);
        }
      }
      const typeStr = typeof msg.type === "string" ? msg.type : toast.type;
      return [messageStr, typeStr];
    }
    return [msg, toast.type];
  })();

  return (
    <div>
      <Toast
        message={_displayMessage}
        type={_displayType}
        visible={toast.visible}
      />
      {tab === "home" ? (
        <Home
          wsError={wsError}
          wsReady={wsReady}
          state={state}
          presets={presets}
          timers={timers}
          effects={effects}
          activePreset={activePreset}
          ledBarRef={ledBarRef}
          sendState={sendState}
          setTab={setTab}
          config={config}
          setActivePreset={setActivePreset}
        />
      ) : (
        <Config
          config={config}
          timezones={timezones}
          sunTimes={sunTimes}
          showToast={showToast}
          loaded={loaded}
          setTab={setTab}
          presets={presets || []}
          setConfig={setConfig}
          otaProgress={otaProgress}
        />
      )}
    </div>
  );
}
