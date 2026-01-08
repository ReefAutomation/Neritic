// Aquarium LED Controller - Web Interface JavaScript

let ws = null;
let reconnectInterval = null;
let currentState = {};
let clockInterval = null;
let clockSynced = false;
let localClock = null;
let presets = [];
let timers = [];
let config = {};

// Base URL logic for local development and file://
let BASE_URL = '';
if (location.protocol === 'file:' || location.hostname === 'localhost' || location.hostname === '127.0.0.1') {
    BASE_URL = localStorage.getItem('BASE_URL') || '';
    if (!BASE_URL) {
        BASE_URL = prompt('Enter backend base URL (e.g. http://192.168.1.100:80):', '');
        if (BASE_URL) localStorage.setItem('BASE_URL', BASE_URL);
    }
}

// Initialize on page load
document.addEventListener('DOMContentLoaded', () => {
    // Hide Quick Controls until first WebSocket message
    const quickControls = document.querySelector('.card');
    if (quickControls && quickControls.style) quickControls.style.display = 'none';
    initializeWebSocket();
    setupEventListeners();
    loadPresets();
    loadTimers();

    // OTA upload form handler
    const otaForm = document.getElementById('otaForm');
    if (otaForm) {
        otaForm.addEventListener('submit', async (e) => {
            e.preventDefault();
            const otaFileInput = document.getElementById('otaFile');
            const otaFile = otaFileInput ? otaFileInput.files[0] : null;
            if (!otaFile) {
                showToast('Please select a firmware file.');
                return;
            }
            const otaProgressBar = document.getElementById('otaProgressBar');
            const otaProgressFill = document.getElementById('otaProgressFill');
            if (otaProgressBar && otaProgressFill) {
                otaProgressBar.style.display = '';
                otaProgressFill.style.width = '0%';
            }
            try {
                let fileToSend = otaFile;
                // If .gz, decompress in browser using fflate
                if (otaFile.name.endsWith('.gz')) {
                    const arrayBuffer = await otaFile.arrayBuffer();
                    // fflate is loaded globally
                    const decompressed = fflate.gunzipSync(new Uint8Array(arrayBuffer));
                    fileToSend = new Blob([decompressed], { type: 'application/octet-stream' });
                }
                const xhr = new XMLHttpRequest();
                xhr.open('POST', BASE_URL + '/ota', true);
                xhr.setRequestHeader('Accept', 'application/json');
                xhr.upload.onprogress = function(e) {
                    if (e.lengthComputable && otaProgressFill) {
                        const percent = Math.round((e.loaded / e.total) * 100);
                        otaProgressFill.style.width = percent + '%';
                    }
                };
                xhr.onload = function() {
                    if (otaProgressBar && otaProgressFill) {
                        otaProgressFill.style.width = '100%';
                    }
                    if (xhr.status === 200) {
                        showToast('Firmware uploaded! Rebooting...', 4000);
                        setTimeout(function() {
                            location.reload();
                        }, 4500);
                    } else {
                        showToast('OTA failed: ' + (xhr.responseText || xhr.statusText), 6000);
                    }
                };
                xhr.onerror = function() {
                    showToast('OTA upload error.', 6000);
                };
                // Send as raw binary, not FormData
                xhr.send(fileToSend);
            } catch (err) {
                showToast('OTA error: ' + err, 6000);
            }
        });
    }

    // OTA file input filename display and progress bar
    const otaFileInput = document.getElementById('otaFile');
    const otaFileName = document.getElementById('otaFileName');
    if (otaFileInput && otaFileName) {
        otaFileInput.addEventListener('change', function() {
            otaFileName.textContent = this.files && this.files.length > 0 ? this.files[0].name : 'No file chosen';
        });
    }
});

// WebSocket Connection
function initializeWebSocket() {
    let wsUrl;
    if (BASE_URL) {
        // Convert BASE_URL to ws(s)://
        wsUrl = BASE_URL.replace(/^http/, 'ws') + '/ws';
    } else {
        const wsProtocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        wsUrl = `${wsProtocol}//${window.location.host}/ws`;
    }
    ws = new WebSocket(wsUrl);

    ws.onopen = () => {
        console.log('[WS] Connected');
        const statusIndicator = document.getElementById('statusIndicator');
        if (statusIndicator && statusIndicator.style) statusIndicator.style.color = '#00cc88';
        if (reconnectInterval) {
            clearInterval(reconnectInterval);
            reconnectInterval = null;
        }
    };

    ws.onmessage = (event) => {
        console.log('[WS] Message:', event.data);
        try {
            const data = JSON.parse(event.data);
            updateState(data);
        } catch (e) {
            console.error('[WS] Failed to parse message:', e);
        }
    };

    ws.onclose = () => {
        console.log('[WS] Disconnected');
        const statusIndicator = document.getElementById('statusIndicator');
        if (statusIndicator && statusIndicator.style) statusIndicator.style.color = '#ff4466';
        // Attempt to reconnect
        if (!reconnectInterval) {
            reconnectInterval = setInterval(() => {
                console.log('[WS] Attempting to reconnect...');
                initializeWebSocket();
            }, 5000);
        }
    };

    ws.onerror = (error) => {
        console.error('[WS] Error:', error);
    };
}

// Update UI with state from server
// Stepped transform for transition time slider (even-distribution, decisecond/second/minute/hour)
function steppedTransitionValue(val) {
    // 0-9: 0-9s (1s step)
    if (val <= 9) return val;
    // 10-59: 10-59s (5s step)
    if (val <= 59) return Math.round(val / 5) * 5;
    // 60-599: 1-9m59s (10s step)
    if (val <= 599) return Math.round(val / 10) * 10;
    // 600-3599: 10m-59m59s (1m step)
    if (val <= 3599) return Math.round(val / 60) * 60;
    // 3600-28800: 1h-8h (5m step)
    return Math.round(val / 300) * 300;
}

// Format transition time for display
function formatTransitionTime(val) {
    if (val < 60) return val + 's';
    if (val < 3600) return (val / 60).toFixed(val % 60 === 0 ? 0 : 1) + 'm';
    return (val / 3600).toFixed(val % 3600 === 0 ? 0 : 1) + 'h';
}

// Update UI with state from server
function updateState(state) {
    currentState = state;
    // Show Quick Controls on first WebSocket message
    const quickControls = document.querySelector('.card');
    if (quickControls && quickControls.style && quickControls.style.display === 'none') {
        quickControls.style.display = '';
    }

    // Update controls without triggering events
    const powerToggle = document.getElementById('powerToggle');
    if (powerToggle && powerToggle.checked !== state.power) {
        powerToggle.checked = state.power;
    }

    const brightnessSlider = document.getElementById('brightnessSlider');
    if (brightnessSlider) {
        // Convert hardware value (1–255) to percent
        const percent = Math.round(((state.brightness - 1) / 2.54));
        if (brightnessSlider.value != percent) brightnessSlider.value = percent;
        const brightnessValue = document.getElementById('brightnessValue');
        if (brightnessValue) brightnessValue.textContent = percent + '%';
    }

    const effectSelect = document.getElementById('effectSelect');
    if (effectSelect && effectSelect.value != state.effect) {
        effectSelect.value = state.effect;
    }

    // Update transition slider and label from state
    if (typeof state.transitionTime !== 'undefined') {
        const transitionSlider = document.getElementById('transitionSlider');
        const transitionSeconds = Math.round(Number(state.transitionTime) / 1000);
        if (transitionSlider) {
            const sliderVal = (function secondsToSlider(seconds) {
                seconds = parseInt(seconds);
                if (seconds <= 59) return seconds - 1;
                if (seconds <= 59 * 60) return 58 + Math.round((seconds - 60) / 60);
                return 117 + Math.round((seconds - 3600) / 3600);
            })(transitionSeconds);
            if (transitionSlider.value != sliderVal) transitionSlider.value = sliderVal;
            if (typeof updateExpoSlider === 'function') {
                updateExpoSlider(sliderVal);
            } else {
                const transitionValue = document.getElementById('transitionValue');
                if (transitionValue) transitionValue.textContent = formatTransitionTime(transitionSeconds);
            }
        }
    }

    if (state.params) {
        const speedSlider = document.getElementById('speedSlider');
        const speedValue = document.getElementById('speedValue');
        if (speedSlider) speedSlider.value = state.params.speed;
        if (speedValue) speedValue.textContent = state.params.speed;

        const intensitySlider = document.getElementById('intensitySlider');
        const intensityValue = document.getElementById('intensityValue');
        if (intensitySlider) intensitySlider.value = state.params.intensity;
        if (intensityValue) intensityValue.textContent = state.params.intensity;

        const color1Picker = document.getElementById('color1Picker');
        if (color1Picker) color1Picker.value = '#' + state.params.color1.toString(16).padStart(6, '0');
        const color2Picker = document.getElementById('color2Picker');
        if (color2Picker) color2Picker.value = '#' + state.params.color2.toString(16).padStart(6, '0');
    }

    // Synchronize clock with backend on first WS message, then advance locally
    if (state.time) {
        if (!clockSynced) {
            const currentTime = document.getElementById('currentTime');
            if (currentTime) currentTime.textContent = state.time;
            // Parse time as HH:MM:SS
            const [h, m, s] = state.time.split(':').map(Number);
            const now = new Date();
            localClock = new Date(now.getFullYear(), now.getMonth(), now.getDate(), h, m, s);
            if (clockInterval) clearInterval(clockInterval);
            clockInterval = setInterval(() => {
                localClock.setSeconds(localClock.getSeconds() + 1);
                const hh = localClock.getHours().toString().padStart(2, '0');
                const mm = localClock.getMinutes().toString().padStart(2, '0');
                const ss = localClock.getSeconds().toString().padStart(2, '0');
                const currentTime = document.getElementById('currentTime');
                if (currentTime) currentTime.textContent = `${hh}:${mm}:${ss}`;
            }, 1000);
            clockSynced = true;
        }
    }

    if (state.sunrise) {
        const sunriseTime = document.getElementById('sunriseTime');
        if (sunriseTime) sunriseTime.textContent = state.sunrise;
    }

    if (state.sunset) {
        const sunsetTime = document.getElementById('sunsetTime');
        if (sunsetTime) sunsetTime.textContent = state.sunset;
    }

    // Highlight active preset
    if (state.currentPreset !== undefined) {
        document.querySelectorAll('.preset-card').forEach((card, index) => {
            if (index === state.currentPreset) {
                card.classList.add('active');
            } else {
                card.classList.remove('active');
            }
        });
    }
}

// Setup event listeners
function setupEventListeners() {
    // Power toggle
    const powerToggle = document.getElementById('powerToggle');
    // Transition time slider (expo-step)
    const transitionSlider = document.getElementById('transitionSlider');
    if (powerToggle) {
        powerToggle.addEventListener('change', (e) => {
            sendState({ power: e.target.checked });
        });
    }
    // Brightness slider (percent 0–100%)
    const brightnessSlider = document.getElementById('brightnessSlider');
    if (brightnessSlider) {
        brightnessSlider.min = 0;
        brightnessSlider.max = 100;
        brightnessSlider.step = 1;
        brightnessSlider.addEventListener('input', (e) => {
            const percent = parseInt(e.target.value);
            const hwValue = Math.round(percent * 2.54) + 1; // 1–255
            const brightnessValue = document.getElementById('brightnessValue');
            if (brightnessValue) brightnessValue.textContent = percent + '%';
        });
        brightnessSlider.addEventListener('change', (e) => {
            const percent = parseInt(e.target.value);
            const hwValue = Math.round(percent * 2.54) + 1; // 1–255
            sendState({ brightness: hwValue });
        });
    }
        if (transitionSlider) {
            // Even distribution: seconds (1–59), minutes (1–59), hours (1–8)
            // Slider range: 0–(59+59+8-1) = 0–125
            // 0–58: seconds (1–59)
            // 59–117: minutes (1–59)
            // 118–125: hours (1–8)
            function sliderToTime(val) {
                val = parseInt(val);
                if (val <= 58) return val + 1; // 1–59s
                if (val <= 117) return (val - 58); // 1–59m
                return (val - 117); // 1–8h
            }
            function timeToSeconds(time, segment) {
                if (segment === 's') return time;
                if (segment === 'm') return time * 60;
                return time * 3600;
            }
            // Ensure max slider value (125) always maps to 8h (28800s)
            function sliderToSeconds(val) {
                val = parseInt(val);
                if (val <= 58) return val + 1; // 1–59s
                if (val <= 117) return (val - 58) * 60 + 60; // 1–59m
                return (val - 117) * 3600 + 3600; // 1–8h
            }
            function secondsToSlider(seconds) {
                seconds = parseInt(seconds);
                if (seconds <= 59) return seconds - 1;
                if (seconds <= 59 * 60) return 58 + Math.round((seconds - 60) / 60);
                return 117 + Math.round((seconds - 3600) / 3600);
            }
            function formatExpoTime(val) {
                if (val < 60) return val + 's';
                if (val < 3600) return (val / 60) + 'm';
                return (val / 3600) + 'h';
            }
            function updateExpoSlider(val) {
                val = parseInt(val);
                // Clamp value to slider range
                if (val < 0) val = 0;
                if (val > 125) val = 125;
                const seconds = sliderToSeconds(val);

                // Only update value if not already set
                if (transitionSlider.value != val) transitionSlider.value = val;
                const transitionValue = document.getElementById('transitionValue');
                if (transitionValue) transitionValue.textContent = formatExpoTime(seconds);
                return seconds;
            }
            transitionSlider.max = 125;
            transitionSlider.min = 0;
            transitionSlider.step = 1;
            transitionSlider.addEventListener('input', (e) => {
                updateExpoSlider(e.target.value);
            });
            transitionSlider.addEventListener('change', (e) => {
                const seconds = updateExpoSlider(e.target.value);
                sendState({ transitionTime: seconds * 1000 });
            });
            // On load, set slider to match state
            if (typeof currentState.transitionTime !== 'undefined') {
                const seconds = Math.round(Number(currentState.transitionTime) / 1000);
                transitionSlider.value = secondsToSlider(seconds);
                updateExpoSlider(transitionSlider.value);
            }
        // On load, set slider to match state
        if (typeof currentState.transitionTime !== 'undefined') {
            const t = Math.round(Number(currentState.transitionTime) / 1000);
            transitionSlider.value = snapToStep(t);
            updateExpoSlider(transitionSlider.value);
        }
    }

    // Stepped transform for transition time slider (even-distribution, decisecond/second/minute/hour)
    function steppedTransitionValue(val) {
        // 0-9: 0-9s (1s step)
        if (val <= 9) return val;
        // 10-59: 10-59s (5s step)
        if (val <= 59) return Math.round(val / 5) * 5;
        // 60-599: 1-9m59s (10s step)
        if (val <= 599) return Math.round(val / 10) * 10;
        // 600-3599: 10m-59m59s (1m step)
        if (val <= 3599) return Math.round(val / 60) * 60;
        // 3600-28800: 1h-8h (5m step)
        return Math.round(val / 300) * 300;
    }

    // Format transition time for display
    function formatTransitionTime(val) {
        if (val < 60) return val + 's';
        if (val < 3600) return (val / 60).toFixed(val % 60 === 0 ? 0 : 1) + 'm';
        return (val / 3600).toFixed(val % 3600 === 0 ? 0 : 1) + 'h';
    }
    // Effect selector
    const effectSelect = document.getElementById('effectSelect');
    if (effectSelect) {
        effectSelect.addEventListener('change', (e) => {
            sendState({ effect: parseInt(e.target.value) });
        });
    }
    // Speed slider
    const speedSlider = document.getElementById('speedSlider');
    if (speedSlider) {
        let speedTimeout;
        speedSlider.addEventListener('input', (e) => {
            const speedValue = document.getElementById('speedValue');
            if (speedValue) speedValue.textContent = e.target.value;
            clearTimeout(speedTimeout);
            speedTimeout = setTimeout(() => {
                sendState({ 
                    params: {
                        ...currentState.params,
                        speed: parseInt(e.target.value)
                    }
                });
            }, 300);
        });
    }
    // Intensity slider
    const intensitySlider = document.getElementById('intensitySlider');
    if (intensitySlider) {
        let intensityTimeout;
        intensitySlider.addEventListener('input', (e) => {
            const intensityValue = document.getElementById('intensityValue');
            if (intensityValue) intensityValue.textContent = e.target.value;
            clearTimeout(intensityTimeout);
            intensityTimeout = setTimeout(() => {
                sendState({ 
                    params: {
                        ...currentState.params,
                        intensity: parseInt(e.target.value)
                    }
                });
            }, 300);
        });
    }
    // Color pickers
    const color1Picker = document.getElementById('color1Picker');
    if (color1Picker) {
        color1Picker.addEventListener('change', (e) => {
            const color = parseInt(e.target.value.substring(1), 16);
            sendState({ 
                params: {
                    ...currentState.params,
                    color1: color
                }
            });
        });
    }
    const color2Picker = document.getElementById('color2Picker');
    if (color2Picker) {
        color2Picker.addEventListener('change', (e) => {
            const color = parseInt(e.target.value.substring(1), 16);
            sendState({ 
                params: {
                    ...currentState.params,
                    color2: color
                }
            });
        });
    }
}

// Send state update to server
function sendState(updates) {
    fetch(BASE_URL + '/api/state', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(updates)
    })
    .then(async response => {
        const text = await response.text();
        if (!text) return {};
        try { return JSON.parse(text); } catch { return {}; }
    })
    .then(data => {
        // Treat empty response or missing success property as success
        if (data && typeof data.success !== 'undefined' && !data.success) {
            console.error('Failed to update state');
        }
    })
    .catch(error => console.error('Error:', error));
}

// Load and display presets
function loadPresets() {
    fetch(BASE_URL + '/api/presets')
        .then(async response => {
            const text = await response.text();
            if (!text) return {};
            try { return JSON.parse(text); } catch { return {}; }
        })
        .then(data => {
            presets = data.presets || [];
            displayPresets();
            renderBrightnessGraph();
        })
        .catch(error => console.error('Error loading presets:', error));
}

function displayPresets() {
    const grid = document.getElementById('presetGrid');
    if (!grid) return;
    grid.innerHTML = '';
    
    const effectNames = ['Solid', 'Ripple', 'Wave', 'Sunrise', 'Shimmer', 'Deep Ocean', 'Moonlight'];
    
    presets.forEach((preset, index) => {
        if (!preset.enabled && index > 0) return;
        
        const card = document.createElement('div');
        card.className = 'preset-card';
        if (currentState.currentPreset === index) {
            card.classList.add('active');
        }
        
        card.innerHTML = `
            <div class="preset-name">${preset.name}</div>
            <div class="preset-info">Effect: ${effectNames[preset.effect]}</div>
            <div class="preset-color-preview" style="background: linear-gradient(135deg, #${preset.params.color1.toString(16).padStart(6, '0')}, #${preset.params.color2.toString(16).padStart(6, '0')})"></div>
        `;
        
        card.addEventListener('click', () => applyPreset(index));
        grid.appendChild(card);
    });
}

function applyPreset(presetId) {
    fetch(BASE_URL + '/api/preset', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({
            id: presetId,
            apply: true
        })
    })
    .then(async response => {
        const text = await response.text();
        if (!text) return {};
        try { return JSON.parse(text); } catch { return {}; }
    })
    .then(data => {
        if (data.success) {
            console.log('Preset applied:', presetId);
        }
    })
    .catch(error => console.error('Error applying preset:', error));
}

// Load and display timers
function loadTimers() {
    fetch(BASE_URL + '/api/timers')
        .then(async response => {
            const text = await response.text();
            if (!text) return {};
            try { return JSON.parse(text); } catch { return {}; }
        })
        .then(data => {
            timers = data.timers || [];
            displayTimers();
            renderBrightnessGraph();
        })
        .catch(error => console.error('Error loading timers:', error));
}
// Render a 24h brightness graph reflecting scheduled preset brightness
function renderBrightnessGraph() {
    const ctx = document.getElementById('brightnessGraph');
    if (!ctx || !Array.isArray(timers) || !Array.isArray(presets) || presets.length === 0) return;

    // Prepare 24h data, 1 point per 10 minutes (144 points)
    const pointsPerHour = 6;
    const totalPoints = 24 * pointsPerHour;
    const labels = [];
    const data = new Array(totalPoints).fill(null);

    // Build a list of timer events sorted by time
    const events = timers
        .filter(timer => timer.enabled && typeof timer.hour === 'number' && typeof timer.minute === 'number' && typeof timer.brightness === 'number')
        .map((timer, idx) => ({
            time: timer.hour * 60 + timer.minute,
            brightness: timer.brightness,
            index: idx
        }))
        .sort((a, b) => a.time - b.time);

    // If no events, clear chart
    if (events.length === 0) {
        if (window.brightnessChart) {
            window.brightnessChart.destroy();
            window.brightnessChart = null;
        }
        return;
    }

    // For each 10-min interval, determine active timer and its brightness
    let currentEventIdx = 0;
    for (let i = 0; i < totalPoints; i++) {
        const minutes = i * 10;
        // Advance to next event if time passed
        while (currentEventIdx < events.length - 1 && minutes >= events[currentEventIdx + 1].time) {
            currentEventIdx++;
        }
        // Get brightness for current timer
        let brightness = events[currentEventIdx].brightness;
        data[i] = brightness;
        // Label every hour
        labels.push(i % pointsPerHour === 0 ? (i / pointsPerHour).toString().padStart(2, '0') + ':00' : '');
    }

    // Calculate current time index using server time if available
    let minutesNow = 0;
    if (currentState && typeof currentState.time === 'string' && /^\d{2}:\d{2}:\d{2}$/.test(currentState.time)) {
        const [h, m, s] = currentState.time.split(':').map(Number);
        minutesNow = h * 60 + m;
    } else {
        const now = new Date();
        minutesNow = now.getHours() * 60 + now.getMinutes();
    }
    const currentIndex = Math.floor(minutesNow / 10);

    // Draw chart with vertical line for current time
    const annotationPlugin = {
        id: 'currentTimeLine',
        afterDraw: chart => {
            if (typeof currentIndex !== 'number' || currentIndex < 0 || currentIndex >= totalPoints) return;
            const ctx = chart.ctx;
            const xAxis = chart.scales.x;
            const yAxis = chart.scales.y;
            const x = xAxis.getPixelForValue(currentIndex);
            ctx.save();
            ctx.beginPath();
            ctx.moveTo(x, yAxis.top);
            ctx.lineTo(x, yAxis.bottom);
            ctx.lineWidth = 2;
            ctx.strokeStyle = '#FF4136';
            ctx.setLineDash([4, 4]);
            ctx.stroke();
            ctx.setLineDash([]);
            ctx.restore();
        }
    };

    if (window.brightnessChart) {
        window.brightnessChart.data.labels = labels;
        window.brightnessChart.data.datasets[0].data = data;
        window.brightnessChart.options.plugins.currentTimeLine = annotationPlugin;
        window.brightnessChart.update();
    } else {
        window.brightnessChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: labels,
                datasets: [{
                    label: 'Brightness (%)',
                    data: data,
                    borderColor: '#0074D9',
                    backgroundColor: 'rgba(0,116,217,0.1)',
                    fill: true,
                    pointRadius: 0,
                    borderWidth: 2,
                    tension: 0.2
                }]
            },
            options: {
                responsive: true,
                plugins: {
                    legend: { display: false },
                    title: { display: false },
                    currentTimeLine: annotationPlugin
                },
                scales: {
                    x: {
                        title: { display: true, text: 'Time (24h)' },
                        ticks: { autoSkip: false, maxTicksLimit: 25 }
                    },
                    y: {
                        title: { display: true, text: 'Brightness (%)' },
                        min: 0, max: 100
                    }
                }
            },
            plugins: [annotationPlugin]
        });
    }
}

function displayTimers() {
    const tableContainer = document.getElementById('scheduleTable');
    if (!tableContainer) return;
    tableContainer.innerHTML = '';

    // Elegant table markup
    const table = document.createElement('table');
    table.className = 'schedule-table-inner';
    const thead = document.createElement('thead');
    thead.innerHTML = `
        <tr>
            <th>Time</th>
            <th>Preset</th>
            <th>Brightness</th>
            <th>Status</th>
        </tr>
    `;
    table.appendChild(thead);
    const tbody = document.createElement('tbody');

    const effectNames = ['Solid', 'Ripple', 'Wave', 'Sunrise', 'Shimmer', 'Deep Ocean', 'Moonlight'];

    // Use server time for schedule highlight
    let activePresetId = null;
    let nowMinutes = 0;
    if (currentState && typeof currentState.time === 'string' && /^\d{2}:\d{2}:\d{2}$/.test(currentState.time)) {
        const [h, m, s] = currentState.time.split(':').map(Number);
        nowMinutes = h * 60 + m;
    } else {
        let now = new Date();
        nowMinutes = now.getHours() * 60 + now.getMinutes();
    }
    let lastTimerIdx = -1;
    let lastTimerTime = -1;
    timers.forEach((timer, idx) => {
        if (!timer.enabled || typeof timer.hour !== 'number' || typeof timer.minute !== 'number') return;
        let timerTime = timer.hour * 60 + timer.minute;
        if (timerTime <= nowMinutes && timerTime > lastTimerTime) {
            lastTimerTime = timerTime;
            lastTimerIdx = idx;
        }
    });
    if (lastTimerIdx >= 0 && timers[lastTimerIdx] && typeof timers[lastTimerIdx].presetId === 'number') {
        activePresetId = timers[lastTimerIdx].presetId;
    }

    timers.forEach((timer, index) => {
        if (!timer.enabled && !timer.name && (!timer.hour && !timer.minute)) return;
        const name = timer.name || `Timer ${index+1}`;
        let timeStr = '--:--';
        if (typeof timer.hour === 'number' && typeof timer.minute === 'number') {
            timeStr = `${timer.hour.toString().padStart(2, '0')}:${timer.minute.toString().padStart(2, '0')}`;
        } else if (timer.time) {
            const d = new Date(timer.time);
            if (!isNaN(d.getTime())) {
                timeStr = d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
            }
        }
        let presetStr = '--';
        if (typeof timer.presetId === 'number' && Array.isArray(presets) && presets.length > 0) {
            if (presets[timer.presetId] && presets[timer.presetId].name) {
                presetStr = presets[timer.presetId].name;
            } else {
                const found = presets.find(p => p.id === timer.presetId || p.presetId === timer.presetId);
                if (found && found.name) {
                    presetStr = found.name;
                } else {
                    presetStr = `Preset ${timer.presetId}`;
                }
            }
        }
        const statusStr = timer.enabled ? '<span class="timer-enabled">Enabled</span>' : '<span class="timer-disabled">Disabled</span>';
        const brightStr = typeof timer.brightness === 'number' ? `${timer.brightness}%` : '--';
        const row = document.createElement('tr');
        row.innerHTML = `
            <td>${timeStr}</td>
            <td>${presetStr}</td>
            <td>${brightStr}</td>
            <td>${statusStr}</td>
        `;
        // Highlight if this timer is the currently running preset
        if (timer.presetId === activePresetId) {
            row.classList.add('active-timer-row');
        }
        // Timer editing disabled: no editTimer UI present
        tbody.appendChild(row);
    });
    table.appendChild(tbody);
    tableContainer.appendChild(table);
}

function editTimer(timerIndex) {
    const timer = timers[timerIndex];
    if (!timer) return;
    
    document.getElementById('timerName').value = timer.name;
    document.getElementById('timerTime').value = new Date(timer.time).toISOString().substring(11, 16);
    const days = timer.days || [];
    ['sun', 'mon', 'tue', 'wed', 'thu', 'fri', 'sat'].forEach((day, index) => {
        const checkbox = document.getElementById(`day-${day}`);
        if (checkbox) checkbox.checked = days.includes(day);
    });
    
    document.getElementById('saveTimerButton').onclick = () => {
        const name = document.getElementById('timerName').value;
        const time = new Date(`1970-01-01T${document.getElementById('timerTime').value}:00Z`).getTime();
        const days = [];
        ['sun', 'mon', 'tue', 'wed', 'thu', 'fri', 'sat'].forEach((day) => {
            const checkbox = document.getElementById(`day-${day}`);
            if (checkbox && checkbox.checked) {
                days.push(day);
            }
        });
        
        saveTimer({ name, time, days });
    };
}

function saveTimer(timer) {
    fetch(BASE_URL + '/api/timer', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(timer)
    })
    .then(async response => {
        const text = await response.text();
        if (!text) return {};
        try { return JSON.parse(text); } catch { return {}; }
    })
    .then(data => {
        if (data.success) {
            console.log('Timer saved');
            loadTimers();
        } else {
            console.error('Failed to save timer');
        }
    })
    .catch(error => console.error('Error saving timer:', error));
}

// Toast notifications
function showToast(message, duration = 3000) {
    const toast = document.createElement('div');
    toast.className = 'toast';
    toast.textContent = message;
    document.body.appendChild(toast);
    
    setTimeout(() => {
        toast.classList.add('show');
    }, 100);
    
    setTimeout(() => {
        toast.classList.remove('show');
        setTimeout(() => {
            document.body.removeChild(toast);
        }, 300);
    }, duration);
}
