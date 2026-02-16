// Config page as Preact component
import { useState, useRef } from "preact/hooks";
import { StatusBar } from "./StatusBar.jsx";
import { getBaseUrl } from "./baseUrl.js";
import { WiFiSettings } from "./Settings/WiFiSettings.jsx";
import { LEDSettings } from "./Settings/LEDSettings.jsx";
import { RelaySettings } from "./Settings/RelaySettings.jsx";
import { SafetySettings } from "./Settings/SafetySettings.jsx";
import { TransitionSettings } from "./Settings/TransitionSettings.jsx";
import { LocationTimeSettings } from "./Settings/LocationTimeSettings.jsx";
import { FirmwareUpdate } from "./Settings/FirmwareUpdate.jsx";
import { DeviceActions } from "./Settings/DeviceActions.jsx";
import { Schedule } from "./Settings/Schedule.jsx";

export function Config({
  config,
  timezones,
  sunTimes,
  showToast,
  hideToast,
  loaded,
  setTab,
  presets,
  setConfig,
  otaProgress,
}) {
  const [otaFileName, setOtaFileName] = useState("");
  const otaInputRef = useRef();
  // Local upload progress for manual upload only
  const [localOtaProgress, setLocalOtaProgress] = useState(-1); // -1: hidden, 0-100: progress
  // Track only modified fields
  const [modifiedConfig, setModifiedConfig] = useState({});

  // Helper to update modifiedConfig with a field change
  const handleFieldChange = (key, value) => {
    setModifiedConfig((prev) => ({ ...prev, [key]: value }));
    setConfig((prev) => ({ ...prev, [key]: value }));
  };

  // Patch setConfig so that if called directly (not via handleFieldChange), we still track changes
  const wrappedSetConfig = (updater) => {
    setConfig((prev) => {
      const next = typeof updater === "function" ? updater(prev) : updater;
      // Compare prev and next, add changed keys to modifiedConfig
      const changed = {};
      for (const k in next) {
        if (next[k] !== prev[k]) changed[k] = next[k];
      }
      if (Object.keys(changed).length > 0) {
        setModifiedConfig((prevMod) => ({ ...prevMod, ...changed }));
      }
      return next;
    });
  };

  // Helper to reset modifiedConfig after save
  const resetModifiedConfig = () => setModifiedConfig({});

  return (
    <div>
      {!loaded.timezones && (
        <div id="loadingIndicator" className="loading-indicator">
          Loading configuration...
        </div>
      )}
      <div
        class="container"
        id="mainContainer"
        style={{
          display:
            loaded.timezones && loaded.presets && loaded.config ? "" : "none",
        }}
      >
        <header class="header">
          <h1>Configuration</h1>
          <StatusBar setTab={setTab} />
        </header>
        <div className="config-action-bar">
          {/* Download Config */}
          <button
            className="btn btn-secondary"
            onClick={() => {
              const blob = new Blob([JSON.stringify(config, null, 2)], {
                type: "application/json",
              });
              const url = URL.createObjectURL(blob);
              const a = document.createElement("a");
              a.href = url;
              a.download = "config.json";
              document.body.appendChild(a);
              a.click();
              setTimeout(() => {
                a.remove();
                URL.revokeObjectURL(url);
                showToast("Config downloaded!", { type: "success" });
              }, 100);
            }}
          >
            Download Config
          </button>
          {/* Upload Config */}
          <label
            className="btn btn-secondary"
            htmlFor="upload-config-input"
          >
            Upload Config
          </label>
          <input
            id="upload-config-input"
            type="file"
            accept=".json,application/json"
            className="hidden-input"
            onChange={async (e) => {
              const file = e.target.files?.[0];
              if (!file) return;
              try {
                const text = await file.text();
                const json = JSON.parse(text);
                const response = await fetch(getBaseUrl() + "/api/config", {
                  method: "POST",
                  headers: { "Content-Type": "application/json" },
                  body: JSON.stringify(json),
                });
                if (!response.ok) throw new Error("Upload failed");
                showToast("Config uploaded!", { type: "success" });
                setTimeout(() => globalThis.location.reload(), 1200);
              } catch (err) {
                showToast("Error uploading config: " + (err.message || err), {
                  type: "error",
                });
              }
              e.target.value = "";
            }}
          />
          {/* Save Configuration */}
          <button
            className="btn btn-primary"
            disabled={Object.keys(modifiedConfig).length === 0}
            onClick={async () => {
              try {
                // If timers are being saved, sort them before sending
                let toSave = { ...modifiedConfig };
                let sortedTimers = null;
                if (toSave.timers && Array.isArray(toSave.timers)) {
                  sortedTimers = [...toSave.timers].sort((a, b) => {
                    if (a.type === 1 || a.type === 2) return -1;
                    if (b.type === 1 || b.type === 2) return 1;
                    return (
                      (a.hour ?? 0) * 60 +
                      (a.minute ?? 0) -
                      ((b.hour ?? 0) * 60 + (b.minute ?? 0))
                    );
                  });
                  toSave.timers = sortedTimers;
                }
                const resp = await fetch(getBaseUrl() + "/api/config", {
                  method: "POST",
                  headers: { "Content-Type": "application/json" },
                  body: JSON.stringify(toSave),
                });
                if (!resp.ok) throw new Error("Save failed");
                showToast("Configuration saved!", { type: "success" });
                // Update local config state with sorted timers so UI reflects the change
                if (sortedTimers) {
                  setConfig((prev) => ({ ...prev, timers: sortedTimers }));
                }
                resetModifiedConfig();
              } catch (err) {
                showToast("Error saving config: " + err, { type: "error" });
              }
            }}
          >
            Save Configuration
          </button>
        </div>
        <div class="card-grid">
          <WiFiSettings
            config={config}
            setConfig={wrappedSetConfig}
            onFieldChange={handleFieldChange}
          />
          <LEDSettings
            config={config}
            setConfig={wrappedSetConfig}
            onFieldChange={handleFieldChange}
          />
          <RelaySettings
            config={config}
            setConfig={wrappedSetConfig}
            onFieldChange={handleFieldChange}
          />
          <SafetySettings
            config={config}
            setConfig={wrappedSetConfig}
            onFieldChange={handleFieldChange}
          />
          <TransitionSettings
            config={config}
            setConfig={wrappedSetConfig}
            onFieldChange={handleFieldChange}
          />
          <LocationTimeSettings
            config={config}
            setConfig={wrappedSetConfig}
            timezones={timezones}
            onFieldChange={handleFieldChange}
          />
          <FirmwareUpdate
            showToast={showToast}
            hideToast={hideToast}
            otaProgress={otaProgress}
            otaFileName={otaFileName}
            setOtaFileName={setOtaFileName}
            otaInputRef={otaInputRef}
            localOtaProgress={localOtaProgress}
            setLocalOtaProgress={setLocalOtaProgress}
          />
          <DeviceActions
            showToast={showToast}
          />
        </div>
        <Schedule
          config={config}
          setConfig={(updater) => {
            wrappedSetConfig((prev) => {
              const next =
                typeof updater === "function" ? updater(prev) : updater;
              // Use a functional update to avoid stale closure
              if (next.schedule !== prev.schedule) {
                setModifiedConfig((prevMod) => ({
                  ...prevMod,
                  schedule: next.schedule,
                }));
              }
              return next;
            });
          }}
          presets={presets}
          sunTimes={sunTimes}
          onFieldChange={(key, value) => {
            if (key === "schedule") {
              setModifiedConfig((prevMod) => ({ ...prevMod, schedule: value }));
            }
            handleFieldChange(key, value);
          }}
        />
      </div>
    </div>
  );
}
