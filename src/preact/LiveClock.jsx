import { useEffect, useRef } from "preact/hooks";

export function LiveClock() {
  const spanRef = useRef(null);

  useEffect(() => {
    function update() {
      if (spanRef.current) {
        const now = new Date();
        spanRef.current.textContent = now.toLocaleTimeString([], {
          hour: "2-digit",
          minute: "2-digit",
          second: "2-digit",
        });
      }
    }
    update();
    const interval = setInterval(update, 1000);
    return () => clearInterval(interval);
  }, []);

  return <span id="currentTime" ref={spanRef}></span>;
}
