import { getBaseUrl } from "../baseUrl.js";
import { useState } from "preact/hooks";
import { Modal } from "../Modal.jsx";

export function DeviceActions({ showToast }) {
  const [modal, setModal] = useState(null); // null | 'reboot' | 'reset'
  const [pending, setPending] = useState(false);
  let actionLabel = "";
  if (pending) actionLabel = "Please wait...";
  else if (modal === "reboot") actionLabel = "Reboot";
  else actionLabel = "Factory Reset";

  const handleAction = async (type) => {
    setPending(true);
    try {
      if (type === "reboot") {
        await fetch(getBaseUrl() + "/api/command", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ command: "reboot" }),
        });
        showToast("Device rebooting...", { type: "info" });
      } else if (type === "reset") {
        await fetch(getBaseUrl() + "/api/factory_reset", { method: "POST" });
        showToast("Factory reset initiated. Device will reboot.", {
          type: "info",
        });
      }
    } catch (e) {
      // Log the error for debugging
      console.error("Device action failed:", e);
      showToast(
        type === "reboot" ? "Reboot failed!" : "Factory reset failed!",
        { type: "error" },
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
      <Modal
        open={!!modal}
        title={modal === "reboot" ? "Reboot Device" : "Factory Reset"}
        description={modal === "reboot"
          ? "Are you sure you want to reboot the device?"
          : "Factory reset will erase all settings. Continue?"}
        onClose={() => !pending && setModal(null)}
        actions={[
          {
            label: "Cancel",
            onClick: () => setModal(null),
            className: "btn btn-secondary",
            disabled: pending,
          },
          {
            label: actionLabel,
            onClick: () => handleAction(modal),
            className: modal === "reboot" ? "btn btn-warning" : "btn btn-danger",
            disabled: pending,
          },
        ]}
      />
    </section>
  );
}
