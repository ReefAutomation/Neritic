import { useState, useEffect, useRef } from 'preact/hooks';

function parseTimeToSeconds(timeStr) {
  const parts = timeStr.split(':').map(Number);
  return parts[0] * 3600 + parts[1] * 60 + (parts[2] || 0);
}

function secondsToTimeString(totalSeconds) {
  const s = ((totalSeconds % 86400) + 86400) % 86400;
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(sec).padStart(2, '0')}`;
}

export function LiveClock({ time }) {
  const [displayedTime, setDisplayedTime] = useState('--:--');
  const intervalRef = useRef(null);

  useEffect(() => {
    if (intervalRef.current) {
      clearInterval(intervalRef.current);
      intervalRef.current = null;
    }

    if (typeof time === 'string' && /^\d{2}:\d{2}(:\d{2})?$/.test(time)) {
      // Seed from NTP time and tick forward locally
      const baseSeconds = parseTimeToSeconds(time);
      const receivedAt = Date.now();
      function updateNtp() {
        const elapsed = Math.floor((Date.now() - receivedAt) / 1000);
        setDisplayedTime(secondsToTimeString(baseSeconds + elapsed));
      }
      updateNtp();
      intervalRef.current = setInterval(updateNtp, 1000);
    } else if (!time || time === '--:--') {
      // Fall back to local time
      function updateLocal() {
        const now = new Date();
        setDisplayedTime(
          now.toLocaleTimeString([], {
            hour: '2-digit',
            minute: '2-digit',
            second: '2-digit',
            hour12: false,
          })
        );
      }
      updateLocal();
      intervalRef.current = setInterval(updateLocal, 1000);
    }

    return () => {
      if (intervalRef.current) {
        clearInterval(intervalRef.current);
        intervalRef.current = null;
      }
    };
  }, [time]);

  return <span id="currentTime">{displayedTime}</span>;
}
