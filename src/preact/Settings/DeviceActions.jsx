import { getBaseUrl } from "../baseUrl.js";
import { useState } from "preact/hooks";

export function DeviceActions({ config, showToast, loaded, setConfig }) {
  const [modal, setModal] = useState(null); // null | 'reboot' | 'reset'
  const [pending, setPending] = useState(false);

  const handleAction = async (type) => {
    setPending(true);
    try {
      if (type === "reboot") {
        await fetch(getBaseUrl() + "/api/command", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ command: "reboot" }),
        });
        showToast("Device rebooting...", "info");
      } else if (type === "reset") {
        await fetch(getBaseUrl() + "/api/factory_reset", { method: "POST" });
        showToast("Factory reset initiated. Device will reboot.", "info");
      }
    } catch (e) {
      // Log the error for debugging
      console.error("Device action failed:", e);
      showToast(
        type === "reboot" ? "Reboot failed!" : "Factory reset failed!",
        "error",
      );
    } finally {
      setPending(false);
      setModal(null);
    }
  };

  return (
    <section class="card system-card">
      <h2>Device Actions</h2>
      <div style={{ display: "flex", gap: 12, flexWrap: "wrap" }}>
        <button class="btn btn-warning" onClick={() => setModal("reboot")}>
          Reboot Device
        </button>
        <button class="btn btn-danger" onClick={() => setModal("reset")}>
          Factory Reset
        </button>
      </div>
      {modal && (
        <div
          style={{
            position: "fixed",
            top: 0,
            left: 0,
            width: "100vw",
            height: "100vh",
            background: "rgba(0,0,0,0.4)",
            zIndex: 1000,
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
          }}
        >
          <div
            style={{
              background: "#fff",
              borderRadius: 8,
              padding: 24,
              minWidth: 320,
              boxShadow: "0 2px 16px rgba(0,0,0,0.2)",
              textAlign: "center",
              color: "#222",
            }}
          >
            <h3 style={{ marginTop: 0 }}>
              {modal === "reboot" ? "Reboot Device" : "Factory Reset"}
            </h3>
            <p>
              {modal === "reboot"
                ? "Are you sure you want to reboot the device?"
                : "Factory reset will erase all settings. Continue?"}
            </p>
            <div
              style={{
                display: "flex",
                gap: 12,
                justifyContent: "center",
                marginTop: 24,
              }}
            >
              <button
                class="btn btn-secondary"
                onClick={() => setModal(null)}
                disabled={pending}
              >
                Cancel
              </button>
              <button
                class={
                  modal === "reboot" ? "btn btn-warning" : "btn btn-danger"
                }
                onClick={() => handleAction(modal)}
                disabled={pending}
              >
                {(() => {
                  if (pending) return "Please wait...";
                  if (modal === "reboot") return "Reboot";
                  return "Factory Reset";
                })()}
              </button>
            </div>
          </div>
        </div>
      )}
    </section>
  );
}
