import { useState } from "preact/hooks";

export function Toast({ message, type, visible }) {
  if (!visible || !message) return null;
  return (
    <div
      class={`toast show ${type}`}
      style={{
        position: "fixed",
        bottom: "2em",
        left: "50%",
        transform: "translateX(-50%)",
        zIndex: 9999,
      }}
    >
      {message}
    </div>
  );
}

export function useToast() {
  const [toast, setToast] = useState({
    message: "",
    type: "info",
    visible: false,
  });
  return [toast, setToast];
}
