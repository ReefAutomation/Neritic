import { useEffect, useRef } from 'preact/hooks';
import { rgbwHexToPreview } from './util.js';

// Helper to find the active timer for a given minute
function findActiveTimer(minute, timers) {
  if (!Array.isArray(timers) || timers.length === 0) return null;
  let activeTimer = null;
  for (const t of timers) {
    if (
      !t.enabled ||
      typeof t.hour !== 'number' ||
      typeof t.minute !== 'number'
    )
      continue;
    let tMin = t.hour * 60 + t.minute;
    if (tMin <= minute) activeTimer = t;
  }
  return activeTimer || timers[0];
}

// Helper to find the preset for a timer
function findPresetForTimer(timer, presets) {
  if (!presets || !timer) return null;
  for (const p of presets) {
    if (p.id === timer.presetId) return p;
  }
  return presets.length > 0 ? presets[0] : null;
}

// Helper to calculate ramp/step points for the chart
function getRampStepPoints(events, transitionDuration, minutesPerDay) {
  const n = events.length;
  let points = [];
  for (let i = 0; i < n; i++) {
    const event = events[i];
    const prevIdx = (i - 1 + n) % n;
    const prevEvent = events[prevIdx];
    const nextIdx = (i + 1) % n;
    const nextEvent = events[nextIdx];
    const rampEnd = calcRampEnd(
      event.time,
      transitionDuration,
      minutesPerDay,
      nextEvent.time
    );
    const startBrightness = calcStartBrightness(
      event,
      prevEvent,
      events,
      prevIdx,
      minutesPerDay,
      transitionDuration
    );
    if (!points.length || points.at(-1).time !== event.time) {
      points.push({ time: event.time, brightness: startBrightness });
    }
    if (!points.length || points.at(-1).time !== rampEnd) {
      points.push({ time: rampEnd, brightness: event.brightness });
    }
  }
  return points;
}

function calcRampEnd(tCurr, transitionDuration, minutesPerDay, tNext) {
  let rampEnd = tCurr + transitionDuration;
  if (rampEnd > minutesPerDay) rampEnd -= minutesPerDay;
  const tCurrToNext =
    tNext > tCurr ? tNext - tCurr : minutesPerDay - tCurr + tNext;
  if (transitionDuration > tCurrToNext) rampEnd = tNext;
  return rampEnd;
}

function calcStartBrightness(
  event,
  prevEvent,
  events,
  prevIdx,
  minutesPerDay,
  transitionDuration
) {
  const tCurr = event.time;
  const tPrev = prevEvent.time;
  const prevIntendedEnd =
    tPrev + transitionDuration > minutesPerDay
      ? tPrev + transitionDuration - minutesPerDay
      : tPrev + transitionDuration;
  const prevTargetIdx = (prevIdx - 1 + events.length) % events.length;
  const bPrevTarget = events[prevTargetIdx].brightness;
  const prevRampDuration =
    prevIntendedEnd > tPrev
      ? prevIntendedEnd - tPrev
      : minutesPerDay - tPrev + prevIntendedEnd;
  const timeFromPrevToCurr =
    tCurr > tPrev ? tCurr - tPrev : minutesPerDay - tPrev + tCurr;
  let startBrightness = prevEvent.brightness;
  if (timeFromPrevToCurr < prevRampDuration && prevRampDuration > 0) {
    const frac = timeFromPrevToCurr / prevRampDuration;
    startBrightness =
      prevEvent.brightness + (bPrevTarget - prevEvent.brightness) * (1 - frac);
  }
  return startBrightness;
}

// Helper to calculate background colors for chart blocks
function getBgColors(bgBlocks, timers, presets, rgbwHexToPreview) {
  return bgBlocks.map((minute) => {
    const activeTimer = findActiveTimer(minute, timers);
    const preset = findPresetForTimer(activeTimer, presets);
    const primaryColor = preset?.params?.colors?.[0] ?? null;
    return primaryColor ? rgbwHexToPreview(primaryColor) : 'rgb(0,116,217)';
  });
}

function buildFlatChartData(state) {
  const hours = Array.from({ length: 25 }, (_, h) => h);
  const flat = hours.map(() => state.brightness || 0);
  return {
    labels: hours.map((h) => `${h.toString().padStart(2, '0')}:00`),
    datasets: [
      {
        label: 'Brightness (%)',
        data: flat,
        borderColor: '#66ccff',
        backgroundColor: 'rgba(102,204,255,0.2)',
        fill: true,
        tension: 0,
        stepped: true,
      },
    ],
  };
}

function buildTimerChartData(
  events,
  config,
  minutesPerDay,
  timers,
  presets,
  rgbwHexToPreview
) {
  let transitionDuration = config.transitionTimes.schedule / 1000 / 60;
  transitionDuration = Math.max(0.01, transitionDuration);
  transitionDuration = Math.round(transitionDuration * 100) / 100;
  let points = getRampStepPoints(events, transitionDuration, minutesPerDay);
  const n = events.length;
  const firstEvent = events[0];
  const lastEvent = events[n - 1];
  const prevEvent = events[(n - 2 + n) % n];
  const t1 = lastEvent.time;
  const bStart = prevEvent.brightness;
  const bEnd = lastEvent.brightness;
  let midnightBrightness = bEnd;
  if (t1 + transitionDuration > minutesPerDay && t1 !== firstEvent.time) {
    const frac = (minutesPerDay - t1) / transitionDuration;
    midnightBrightness = bStart + (bEnd - bStart) * frac;
  }
  if (!points.some((p) => p.time === 0))
    points.push({ time: 0, brightness: midnightBrightness });
  if (!points.some((p) => p.time === minutesPerDay))
    points.push({ time: minutesPerDay, brightness: midnightBrightness });
  points.sort((a, b) => a.time - b.time);
  let uniquePoints = [];
  let seen = new Set();
  for (let i = points.length - 1; i >= 0; i--) {
    if (!seen.has(points[i].time)) {
      uniquePoints.unshift(points[i]);
      seen.add(points[i].time);
    }
  }
  const pointsForChart = uniquePoints.map((p) => ({
    x: p.time,
    y: Math.min(100, p.brightness),
  }));
  const minutesArray = uniquePoints.map((p) => p.time);
  const bgBlocks = minutesArray;
  const bgColors = getBgColors(bgBlocks, timers, presets, rgbwHexToPreview);
  return { pointsForChart, minutesArray, bgColors };
}

// Parses the device time string and returns the current time as fractional
// minutes since midnight, or null if the time is unknown / not yet synced.
// Falls back to the browser clock when the device reports '--:--'.
function parseActualMinutes(time) {
  if (time && time !== '--:--' && /^\d{2}:\d{2}(:\d{2})?$/.test(time)) {
    const parts = time.split(':').map(Number);
    return parts[0] * 60 + parts[1] + (parts[2] || 0) / 60;
  }
  if (time !== '--:--') {
    const now = new Date();
    return now.getHours() * 60 + now.getMinutes() + now.getSeconds() / 60;
  }
  return null;
}

export function Graph({ state, timers, presets, config }) {
  const brightnessGraphRef = useRef(null);
  useEffect(() => {
    if (!brightnessGraphRef.current) return;
    if (typeof config?.transitionTimes?.schedule !== 'number') return;

    function renderChart() {
      // --- Legacy math: build points for ramp transitions ---
      const minutesPerDay = 24 * 60;
      let events = Array.isArray(timers)
        ? timers
            .filter(
              (t) =>
                t.enabled &&
                typeof t.hour === 'number' &&
                typeof t.minute === 'number' &&
                typeof t.brightness === 'number'
            )
            .map((t, idx) => ({
              time: t.hour * 60 + t.minute,
              brightness: t.brightness,
              index: idx,
            }))
            .sort((a, b) => a.time - b.time)
        : [];

      // Calculate actual time in minutes from device clock (or browser fallback)
      const actualMinutes = parseActualMinutes(state?.time);

      // Chart.js plugin for vertical red line at actual time
      const actualTimeLinePlugin = {
        id: 'actualTimeLine',
        afterDraw: (chart) => {
          if (actualMinutes === null) return;
          const { ctx, chartArea, scales } = chart;
          if (!chartArea) return;
          const x = scales.x.getPixelForValue(actualMinutes);
          ctx.save();
          ctx.beginPath();
          ctx.setLineDash([6, 6]); // Dashed line: 6px dash, 6px gap
          ctx.moveTo(x, chartArea.top);
          ctx.lineTo(x, chartArea.bottom);
          ctx.lineWidth = 2;
          ctx.strokeStyle = 'red';
          ctx.globalAlpha = 0.9;
          ctx.stroke();
          ctx.setLineDash([]); // Reset to solid for other drawing
          ctx.restore();
        },
      };

      // Build chart data
      let chartData, chartOptions, chartPlugins;
      if (events.length === 0) {
        chartData = buildFlatChartData(state);
        chartOptions = {
          responsive: true,
          maintainAspectRatio: false,
          plugins: {
            legend: { display: false },
            actualTimeLine: actualTimeLinePlugin,
          },
          scales: {
            x: {
              display: true,
              title: { display: true, text: 'Time (24h)' },
              min: 0,
              max: 24,
            },
            y: {
              display: true,
              title: { display: true, text: 'Brightness (%)' },
              min: 0,
              max: 100,
            },
          },
        };
        chartPlugins = [actualTimeLinePlugin];
      } else {
        const { pointsForChart, minutesArray, bgColors } = buildTimerChartData(
          events,
          config,
          minutesPerDay,
          timers,
          presets,
          rgbwHexToPreview
        );

        const timeToLabel = (minute) => {
          const h = Math.floor(minute / 60);
          return `${h.toString().padStart(2, '0')}` + 'h';
        };
        const bgBlocks = minutesArray;

        const colorBgPlugin = {
          id: 'colorBgByBlock',
          beforeDatasetsDraw: (chart) => {
            const { ctx, chartArea, scales } = chart;
            if (!chartArea) return;
            const dataset = chart.data.datasets[0];
            const points = dataset.data;
            for (let i = 0; i < bgBlocks.length - 1; i++) {
              // Use the true X value (minutes) for linear scale
              const x0 = scales.x.getPixelForValue(points[i].x);
              const x1 = scales.x.getPixelForValue(points[i + 1].x);
              const y0 = scales.y.getPixelForValue(points[i].y);
              const y1 = scales.y.getPixelForValue(points[i + 1].y);
              ctx.save();
              ctx.beginPath();
              ctx.moveTo(x0, chartArea.bottom);
              ctx.lineTo(x0, y0);
              ctx.lineTo(x1, y1);
              ctx.lineTo(x1, chartArea.bottom);
              ctx.closePath();
              const grad = ctx.createLinearGradient(
                x0,
                chartArea.bottom,
                x1,
                chartArea.bottom
              );
              grad.addColorStop(0, bgColors[i - 1] || bgColors[i]);
              grad.addColorStop(1, bgColors[i]);
              ctx.fillStyle = grad;
              ctx.globalAlpha = 0.6;
              ctx.fill();
              ctx.restore();
            }
          },
        };

        chartData = {
          datasets: [
            {
              label: 'Brightness (%)',
              data: pointsForChart,
              borderColor: '#66ccff',
              backgroundColor: 'rgba(102,204,255,0.2)',
              fill: true,
              tension: 0,
              parsing: false, // Use x/y objects
            },
          ],
        };
        chartOptions = {
          responsive: true,
          maintainAspectRatio: false,
          plugins: {
            legend: { display: false },
            colorBgByBlock: colorBgPlugin,
          },
          elements: {
            line: { showLine: true },
            point: { radius: 4, pointStyle: 'circle' },
          },
          scales: {
            x: {
              type: 'linear',
              display: true,
              title: { display: true, text: 'Time (24h)' },
              min: 0,
              max: 1440,
              ticks: {
                stepSize: 60,
                callback(value, index, ticks) {
                  // Show label at each event/ramp point and on the hour
                  if (minutesArray.includes(value)) return timeToLabel(value);
                  if (value % 60 === 0) return timeToLabel(value);
                  return '';
                },
              },
            },
            y: {
              display: true,
              min: 0,
              max: 100,
              title: {
                display: true,
                text: 'Brightness (%)',
              },
            },
          },
        };
        chartPlugins = [colorBgPlugin, actualTimeLinePlugin];
      }

      // Destroy previous Chart.js instance if it exists
      if (brightnessGraphRef.current._chartInstance) {
        brightnessGraphRef.current._chartInstance.destroy();
        brightnessGraphRef.current._chartInstance = null;
      }
      // Create new Chart.js instance
      const ctx = brightnessGraphRef.current.getContext('2d');
      brightnessGraphRef.current._chartInstance = new globalThis.Chart(ctx, {
        type: 'line',
        data: chartData,
        options: chartOptions,
        plugins: chartPlugins,
      });
    }

    // If Chart.js is not loaded, load from CDN
    if (globalThis.Chart === undefined) {
      const script = document.createElement('script');
      script.src = 'https://cdn.jsdelivr.net/npm/chart.js';
      script.async = true;
      script.onload = renderChart;
      document.body.appendChild(script);
    } else {
      renderChart();
    }

    // Add window focus event to update red timeline
    function handleWindowFocus() {
      renderChart();
    }
    globalThis.addEventListener('focus', handleWindowFocus);

    // Resize chart on window resize to fix tall/short bug
    function handleResize() {
      brightnessGraphRef.current?._chartInstance?.resize();
    }
    globalThis.addEventListener('resize', handleResize);

    // Cleanup on unmount
    return () => {
      globalThis.removeEventListener('resize', handleResize);
      globalThis.removeEventListener('focus', handleWindowFocus);
      if (brightnessGraphRef.current?._chartInstance) {
        brightnessGraphRef.current._chartInstance.destroy();
        brightnessGraphRef.current._chartInstance = null;
      }
    };
  }, [timers, state, presets, config]);

  return (
    <div
      style={{
        width: '100%',
        maxWidth: '100%',
        aspectRatio: '5 / 1',
        minHeight: '180px',
        marginTop: '16px',
      }}
    >
      <canvas
        ref={brightnessGraphRef}
        id="brightnessGraph"
        tabIndex={0}
        style={{
          width: '100%',
          height: '100%',
          outline: 'none',
        }}
      />
    </div>
  );
}
