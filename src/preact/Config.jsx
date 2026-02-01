// Config page as Preact component
import { useState, useRef } from "preact/hooks";

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
        <div
          id="loadingIndicator"
          style={{ textAlign: "center", marginTop: "3em", fontSize: "1.3em" }}
        >
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
          <div class="status-bar">
            <span class="status-indicator" id="statusIndicator">
              ●
            </span>
            <span id="currentTime"></span>
            <button
              type="button"
              className="back-to-main-link"
              title="Back to Main"
              aria-label="Back to Main"
              onClick={() => {
                if (typeof setTab === "function") {
                  setTab("home");
                } else {
                  globalThis.location.href = "index.html";
                }
              }}
              style={{
                background: "none",
                border: "none",
                padding: 0,
                cursor: "pointer",
              }}
            >
              <svg
                aria-label="Home"
                focusable="false"
                data-prefix="fas"
                data-icon="home"
                className="svg-inline--fa fa-home fa-w-18 back-to-main-icon icon-glow-hover"
                xmlns="http://www.w3.org/2000/svg"
                viewBox="0 0 576 512"
                width="24"
                height="24"
              >
                <path
                  fill="#66ccff"
                  d="M280.37 148.26L96 300.11V464a16 16 0 0 0 16 16l112-.3a16 16 0 0 0 16-15.7V368a16 16 0 0 1 16-16h64a16 16 0 0 1 16 16v95.7a16 16 0 0 0 16 16l112 .3a16 16 0 0 0 16-16V300L295.67 148.26a12.19 12.19 0 0 0-15.3 0zM573.32 268.35L512 220.69V56a24 24 0 0 0-24-24h-88a24 24 0 0 0-24 24v51.61L318.47 43a48 48 0 0 0-61 0L2.61 268.35a16 16 0 0 0-1.6 22.59l21.41 25.5a16 16 0 0 0 22.59 1.6L64 271.69V464a48 48 0 0 0 48 48h352a48 48 0 0 0 48-48V271.7l19 16.35a16 16 0 0 0 22.59-1.6l21.41-25.5a16 16 0 0 0-1.68-22.6z"
                />
              </svg>
            </button>
          </div>
        </header>
        <div
          style={{
            display: "flex",
            justifyContent: "flex-end",
            marginBottom: "0.5em",
          }}
        >
          {/* Download Config */}
          <button
            class="btn btn-secondary"
            style={{ marginRight: "0.5em" }}
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
                showToast("Config downloaded!", "success");
              }, 100);
            }}
          >
            Download Config
          </button>
          {/* Upload Config */}
          <label
            class="btn btn-secondary"
            style={{ marginRight: "0.5em", cursor: "pointer" }}
            htmlFor="upload-config-input"
          >
            Upload Config
          </label>
          <input
            id="upload-config-input"
            type="file"
            accept=".json,application/json"
            style={{ display: "none" }}
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
                showToast("Config uploaded!", "success");
                setTimeout(() => globalThis.location.reload(), 1200);
              } catch (err) {
                showToast(
                  "Error uploading config: " + (err.message || err),
                  "error",
                );
              }
              e.target.value = "";
            }}
          />
          {/* Save Configuration */}
          <button
            class="btn btn-primary"
            disabled={Object.keys(modifiedConfig).length === 0}
            onClick={async () => {
              try {
                const resp = await fetch(getBaseUrl() + "/api/config", {
                  method: "POST",
                  headers: { "Content-Type": "application/json" },
                  body: JSON.stringify(modifiedConfig),
                });
                if (!resp.ok) throw new Error("Save failed");
                showToast("Configuration saved!", "success");
                resetModifiedConfig();
              } catch (err) {
                showToast("Error saving config: " + err, "error");
              }
            }}
          >
            Save Configuration
          </button>
        </div>
        <div class="card-grid">
          <WiFiSettings config={config} setConfig={wrappedSetConfig} onFieldChange={handleFieldChange} />
          <LEDSettings config={config} setConfig={wrappedSetConfig} onFieldChange={handleFieldChange} />
          <RelaySettings config={config} setConfig={wrappedSetConfig} onFieldChange={handleFieldChange} />
          <SafetySettings config={config} setConfig={wrappedSetConfig} onFieldChange={handleFieldChange} />
          <TransitionSettings config={config} setConfig={wrappedSetConfig} onFieldChange={handleFieldChange} />
          <LocationTimeSettings
            config={config}
            setConfig={wrappedSetConfig}
            timezones={timezones}
            onFieldChange={handleFieldChange}
          />
          <FirmwareUpdate
            config={config}
            showToast={showToast}
            otaProgress={otaProgress}
            loaded={loaded}
            setConfig={wrappedSetConfig}
            otaFileName={otaFileName}
            setOtaFileName={setOtaFileName}
            otaInputRef={otaInputRef}
            localOtaProgress={localOtaProgress}
            setLocalOtaProgress={setLocalOtaProgress}
            onFieldChange={handleFieldChange}
          />
          <DeviceActions
            config={config}
            showToast={showToast}
            loaded={loaded}
            setConfig={wrappedSetConfig}
            onFieldChange={handleFieldChange}
          />
        </div>
        <Schedule
          config={config}
          setConfig={(updater) => {
            wrappedSetConfig((prev) => {
              const next = typeof updater === "function" ? updater(prev) : updater;
              // Use a functional update to avoid stale closure
              if (next.schedule !== prev.schedule) {
                setModifiedConfig((prevMod) => ({ ...prevMod, schedule: next.schedule }));
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
