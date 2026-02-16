import { LiveClock } from "./LiveClock.jsx";
import { BUTTONS, useCurrentTab } from "./Tabs.jsx";

export function StatusBar({
  setTab,
  indicatorColor = "var(--success)",
  showIndicator = true,
  children,
}) {
  const tab = useCurrentTab();
  const { label, svgPath, nextTab } = BUTTONS[tab] || {};
  const handleButtonClick = () => {
    if (nextTab && typeof setTab === "function") {
      setTab(nextTab);
    }
  };
  return (
    <div className="status-bar">
      {showIndicator && (
        <span
          className="status-indicator"
          id="statusIndicator"
          style={{ color: indicatorColor }}
        >
          ●
        </span>
      )}
      <LiveClock />
      {children}
      <button
        type="button"
        className="icon-header-btn"
        title={label}
        aria-label={label}
        onClick={handleButtonClick}
      >
        <svg
          className="icon-glow-hover"
          aria-label={label}
          xmlns="http://www.w3.org/2000/svg"
          viewBox="0 0 640 640"
          width={24}
          height={24}
        >
          <path fill="#66ccff" d={svgPath} />
        </svg>
      </button>
    </div>
  );
}
