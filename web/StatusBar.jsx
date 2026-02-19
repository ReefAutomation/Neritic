import { LiveClock } from './LiveClock.jsx';
import { BUTTONS, useCurrentTab } from './Tabs.jsx';

export function StatusBar({
  setTab,
  indicatorColor = 'var(--success)',
  showIndicator = true,
  children,
  time,
  power,
  onTogglePower,
}) {
  const tab = useCurrentTab();
  const { label, svgPath, nextTab } = BUTTONS[tab] || {};
  const handleButtonClick = () => {
    if (nextTab && typeof setTab === 'function') {
      setTab(nextTab);
    }
  };
  // Accept state/time as prop if passed via children or context
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
      <LiveClock time={time} />
      {children}
      {typeof onTogglePower === 'function' && (
        <button
          type="button"
          className={`icon-header-btn status-power-btn${power ? ' is-on' : ''}`}
          title={power ? 'Turn power off' : 'Turn power on'}
          aria-label={power ? 'Turn power off' : 'Turn power on'}
          onClick={onTogglePower}
        >
          <svg
            className="icon-glow-hover"
            aria-hidden="true"
            xmlns="http://www.w3.org/2000/svg"
            viewBox="0 0 512 512"
            width={24}
            height={24}
          >
            <path
              fill={power ? '#66ccff' : '#8aa0b3'}
              d="M288 0c0-17.7-14.3-32-32-32S224-17.7 224 0l0 256c0 17.7 14.3 32 32 32s32-14.3 32-32L288 0zM146.3 98.4c14.5-10.1 18-30.1 7.9-44.6s-30.1-18-44.6-7.9C43.4 92.1 0 169 0 256 0 397.4 114.6 512 256 512S512 397.4 512 256c0-87-43.4-163.9-109.7-210.1-14.5-10.1-34.4-6.6-44.6 7.9s-6.6 34.4 7.9 44.6c49.8 34.8 82.3 92.4 82.3 157.6 0 106-86 192-192 192S64 362 64 256c0-65.2 32.5-122.9 82.3-157.6z"
            />
          </svg>
        </button>
      )}
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
