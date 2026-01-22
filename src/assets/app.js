// Aquarium LED Controller - Web Interface JavaScript

let ws = null;
let reconnectInterval = null;
let currentState = {};
let cachedPreset = undefined;
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

// Global effect names list
let effectNames = [];

// Shared slider logic for both config.js and app.js
window.steppedTransitionValue = function(val) {
    val = Number(val);
    if (val <= 59) return val; // 0–59s
    if (val <= 119) return (val - 59) * 60; // 1–60m
    if (val <= 127) return (val - 119) * 3600; // 1–8h
    return 28800;
};

window.formatTransitionTime = function(val) {
    val = Number(val);
    if (val === 0) return '0s';
    if (val < 60) return val + 's';
    if (val < 3600) return Math.round(val / 60) + 'm';
    return Math.round(val / 3600) + 'h';
};

// Initialize on page load
document.addEventListener('DOMContentLoaded', () => {
    // Hide Quick Controls until first WebSocket message
    const quickControls = document.querySelector('.card');
    if (quickControls && quickControls.style) quickControls.style.display = 'none';
    // Only connect WebSocket if not on config.html
    if (!window.location.pathname.endsWith('config.html')) {
        initializeWebSocket();
    }
    setupEventListeners();

    // Only load effects, presets, and timers on the home page (index.html)
    if (window.location.pathname.endsWith('index.html') || window.location.pathname === '/' || window.location.pathname === '/index.html') {
        loadEffects().then(() => {
            if (typeof loadPresets === 'function') loadPresets();
            if (typeof loadTimers === 'function') loadTimers();
        });
        // Fetch and display version
        fetch(BASE_URL + '/api/version')
            .then(r => r.json())
            .then(data => {
                if (data && data.version) {
                    const vEl = document.getElementById('versionString');
                    if (vEl) vEl.textContent = 'Version: ' + data.version;
                }
            })
            .catch(() => {
                const vEl = document.getElementById('versionString');
                if (vEl) vEl.textContent = 'Version: (unavailable)';
            });
    }

// Load all available effects from backend
function loadEffects() {
    return fetch(BASE_URL + '/api/effects')
        .then(async response => {
            const text = await response.text();
            if (!text) return [];
            try { return JSON.parse(text); } catch { return []; }
        })
        .then(data => {
            let effects = (data && data.effects) ? data.effects : [];
            effectNames = effects.map(e => e.name);
            // Populate effectSelect dropdown
            const effectSelect = document.getElementById('effectSelect');
            if (effectSelect) {
                effectSelect.innerHTML = '';
                effects.forEach(e => {
                    const opt = document.createElement('option');
                    opt.value = e.id;
                    opt.textContent = e.name;
                    effectSelect.appendChild(opt);
                });
            }
        })
        .catch(error => console.error('Error loading effects:', error));
}

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
    // EXACTLY match Min Transition Time mapping from config.js
    val = Number(val);
    if (val <= 59) return val; // 0–59s
    if (val <= 119) return (val - 59) * 60; // 1–60m
    if (val <= 127) return (val - 119) * 3600; // 1–8h
    return 28800;
}

// Format transition time for display
function formatTransitionTime(val) {
    val = Number(val);
    if (val === 0) return '0s';
    if (val < 60) return val + 's';
    if (val < 3600) return Math.round(val / 60) + 'm';
    return Math.round(val / 3600) + 'h';
}

// Update UI with state from server
function updateState(state) {
    const prevPreset = currentState.preset;
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
        const percent = state.brightness;
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
        if (transitionSlider) {
            let transitionSeconds = Math.round(Number(state.transitionTime) / 1000);
            let sliderVal = 0;
            if (transitionSeconds <= 59) sliderVal = transitionSeconds;
            else if (transitionSeconds < 3600) sliderVal = 59 + Math.round(transitionSeconds / 60);
            else sliderVal = 119 + Math.round(transitionSeconds / 3600);
            transitionSlider.value = sliderVal;
            const transitionValue = document.getElementById('transitionValue');
            if (transitionValue) transitionValue.textContent = formatTransitionTime(transitionSeconds);
        }
    }

    if (state.params) {
        const speedSlider = document.getElementById('speedSlider');
        const speedValue = document.getElementById('speedValue');
        if (speedSlider) speedSlider.value = state.params.speed;
        if (speedValue && speedSlider) speedValue.textContent = state.params.speed + '%';

        const intensitySlider = document.getElementById('intensitySlider');
        const intensityValue = document.getElementById('intensityValue');
        if (intensitySlider) intensitySlider.value = state.params.intensity;
        if (intensityValue) intensityValue.textContent = state.params.intensity;

        // Dynamically create color pickers
        const colorPickersRow = document.getElementById('colorPickersRow');
        if (colorPickersRow) {
            colorPickersRow.innerHTML = '';
            if (state.params.colors && Array.isArray(state.params.colors)) {
                state.params.colors.forEach((color, idx) => {
                    const colorDiv = document.createElement('div');
                    colorDiv.className = 'control-item';
                    const label = document.createElement('label');
                    label.textContent = (idx === 0 ? 'Primary' : idx === 1 ? 'Secondary' : idx === 2 ? 'Tertiary' : `Color ${idx+1}`) + ' Color';
                    const input = document.createElement('input');
                    input.type = 'color';
                    input.className = 'color-input';
                    input.value = color;
                    input.id = `colorPicker${idx}`;
                    input.addEventListener('change', (e) => {
                        let colors = [...state.params.colors];
                        colors[idx] = e.target.value;
                        sendState({
                            params: {
                                ...state.params,
                                colors
                            }
                        });
                    });
                    label.appendChild(input);
                    colorDiv.appendChild(label);
                    colorPickersRow.appendChild(colorDiv);
                });
            }
        }
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
    if (state.preset !== undefined) {
        document.querySelectorAll('.preset-card').forEach((card, index) => {
            if (index === state.preset) {
                card.classList.add('active');
            } else {
                card.classList.remove('active');
            }
        });
    }

    // Only redraw preset cards if preset changed
    if (state.preset !== undefined && state.preset !== cachedPreset) {
        displayPresets();
    } else {
        // Just update active class
        document.querySelectorAll('.preset-card').forEach((card, index) => {
            if (index === state.preset) {
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
    // Transition time slider (stepped)
    const transitionSlider = document.getElementById('transitionSlider');
    const transitionValue = document.getElementById('transitionValue');
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
            const brightnessValue = document.getElementById('brightnessValue');
            if (brightnessValue) brightnessValue.textContent = percent + '%';
        });
        brightnessSlider.addEventListener('change', (e) => {
            const percent = parseInt(e.target.value);
            sendState({ brightness: percent });
        });
    }
    if (transitionSlider && transitionValue) {
        transitionSlider.min = 0;
        transitionSlider.max = 127;
        transitionSlider.step = 1;
        // Set initial value
        let initial = transitionSlider.value;
        let seconds = steppedTransitionValue(initial);
        transitionSlider.value = initial;
        transitionValue.textContent = formatTransitionTime(seconds);
        transitionSlider.addEventListener('input', e => {
            let seconds = steppedTransitionValue(e.target.value);
            transitionSlider.value = e.target.value;
            transitionValue.textContent = formatTransitionTime(seconds);
        });
        transitionSlider.addEventListener('change', e => {
            let seconds = steppedTransitionValue(e.target.value);
            transitionSlider.value = e.target.value;
            transitionValue.textContent = formatTransitionTime(seconds);
            sendState({ transitionTime: seconds * 1000 });
        });
        // On load, set slider to match state (EXACTLY as in config.js)
        if (typeof currentState.transitionTime !== 'undefined') {
            const ms = Number(currentState.transitionTime);
            let sec = Math.round(ms / 1000);
            let sliderVal = 0;
            if (sec <= 59) sliderVal = sec;
            else if (sec < 3600) sliderVal = 59 + Math.round(sec / 60);
            else sliderVal = 119 + Math.round(sec / 3600);
            transitionSlider.value = sliderVal;
            transitionValue.textContent = formatTransitionTime(sec);
        }
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
            const percent = parseInt(e.target.value);
            if (speedValue) speedValue.textContent = percent + '%';
            clearTimeout(speedTimeout);
            speedTimeout = setTimeout(() => {
                sendState({ 
                    params: {
                        ...currentState.params,
                        speed: percent
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
    const color2Picker = document.getElementById('color2Picker');
    const color3Picker = document.getElementById('color3Picker');
    if (color1Picker) {
        color1Picker.addEventListener('change', (e) => {
            const color = e.target.value;
            let colors = (currentState.params && currentState.params.colors) ? [...currentState.params.colors] : ["#FFFFFF", "#FFFFFF", "#FFA000"];
            colors[0] = color;
            sendState({
                params: {
                    ...currentState.params,
                    colors
                }
            });
        });
    }
    if (color2Picker) {
        color2Picker.addEventListener('change', (e) => {
            const color = e.target.value;
            let colors = (currentState.params && currentState.params.colors) ? [...currentState.params.colors] : ["#FFFFFF", "#FFFFFF", "#FFA000"];
            colors[1] = color;
            sendState({
                params: {
                    ...currentState.params,
                    colors
                }
            });
        });
    }
    if (color3Picker) {
        color3Picker.addEventListener('change', (e) => {
            const color = e.target.value;
            let colors = (currentState.params && currentState.params.colors) ? [...currentState.params.colors] : ["#FFFFFF", "#FFFFFF", "#FFA000"];
            colors[2] = color;
            sendState({
                params: {
                    ...currentState.params,
                    colors
                }
            });
        });
    }
    // Hide secondary/tertiary color picker if not present
    function updateColorPickersVisibility() {
        if (color2Picker) {
            if (currentState.params && currentState.params.colors && currentState.params.colors.length > 1) {
                color2Picker.parentElement.style.display = '';
            } else {
                color2Picker.parentElement.style.display = 'none';
            }
        }
        if (color3Picker) {
            if (currentState.params && currentState.params.colors && currentState.params.colors.length > 2) {
                color3Picker.parentElement.style.display = '';
            } else {
                color3Picker.parentElement.style.display = 'none';
            }
        }
    }
    // Call on load and whenever state updates
    updateColorPickersVisibility();
    // Also patch updateState to call this after updating currentState
    const origUpdateState = window.updateState;
    window.updateState = function(state) {
        origUpdateState(state);
        updateColorPickersVisibility();
    };
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
// Home page (index.html) only: loadPresets and loadTimers for independent schedule/preset display
function loadPresets() {
    fetch(BASE_URL + '/api/presets')
        .then(async response => {
            const text = await response.text();
            if (!text) return {};
            try { return JSON.parse(text); } catch { return {}; }
        })
        .then(data => {
            presets = Array.isArray(data) ? data : (data.presets || []);
            if (typeof displayPresets === 'function') displayPresets();
            if (typeof renderBrightnessGraph === 'function') renderBrightnessGraph();
            // If timers are already loaded, update timer table
            if (Array.isArray(timers) && timers.length > 0 && typeof displayTimers === 'function') displayTimers();
        })
        .catch(error => console.error('Error loading presets:', error));
}

function loadTimers() {
    fetch(BASE_URL + '/api/timers')
        .then(async response => {
            const text = await response.text();
            if (!text) return {};
            try { return JSON.parse(text); } catch { return {}; }
        })
        .then(data => {
            timers = Array.isArray(data) ? data : (data.timers || []);
            // Only display timers if presets are loaded
            if (Array.isArray(presets) && presets.length > 0 && typeof displayTimers === 'function') displayTimers();
            if (typeof renderBrightnessGraph === 'function') renderBrightnessGraph();
        })
        .catch(error => console.error('Error loading timers:', error));
}

function displayPresets() {
    const grid = document.getElementById('presetGrid');
    if (!grid) return;
    grid.innerHTML = '';
    presets.forEach((preset) => {
        if (!preset.enabled && preset.id > 0) return;
        const card = document.createElement('div');
        card.className = 'preset-card';
        // Highlight if this preset is active (by id)
        if (currentState.preset !== undefined && presets.find(p => p.id === currentState.preset)?.id === preset.id) {
            card.classList.add('active');
        }
        const effectName = effectNames[preset.effect] || `Effect #${preset.effect}`;
        // Use only the colors array for preview
        let colorA = (preset.params && Array.isArray(preset.params.colors) && preset.params.colors[0]) ? preset.params.colors[0] : '#000000';
        let colorB = (preset.params && Array.isArray(preset.params.colors) && preset.params.colors[1]) ? preset.params.colors[1] : null;
        let previewStyle = colorB ? `background: linear-gradient(135deg, ${colorA}, ${colorB})` : `background: ${colorA}`;
        card.innerHTML = `
            <div class="preset-name">${preset.name}</div>
            <div class="preset-info">Effect: ${effectName}</div>
            <div class="preset-color-preview" style="${previewStyle}"></div>
        `;
        card.addEventListener('click', () => applyPreset(preset.id));
        grid.appendChild(card);
    });
    cachedPreset = currentState.preset;
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
            // Optionally update currentState.preset to presetId for immediate UI feedback
            currentState.preset = presetId;
            displayPresets();
            console.log('Preset applied:', presetId);
        }
    })
    .catch(error => console.error('Error applying preset:', error));
}

// Load and display timers
// Removed loadTimers (timers are part of config API)
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

    // For each 10-min interval, determine active timer and its brightness, wrapping to previous day if needed
    let currentEventIdx = 0;
    for (let i = 0; i < totalPoints; i++) {
        const minutes = i * 10;
        // Advance to next event if time passed
        while (currentEventIdx < events.length - 1 && minutes >= events[currentEventIdx + 1].time) {
            currentEventIdx++;
        }
        // If before the first event, use the last event's brightness (cycle from previous day)
        let brightness;
        if (minutes < events[0].time) {
            brightness = events[events.length - 1].brightness;
        } else {
            brightness = events[currentEventIdx].brightness;
        }
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
            // Only find by id property (robust)
            let found = presets.find(p => p.id === timer.presetId || p.presetId === timer.presetId);
            if (found && found.name) {
                presetStr = found.name;
            } else {
                presetStr = `Preset ${timer.presetId}`;
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
        // Highlight only the timer row that matches the current time (lastTimerIdx)
        if (index === lastTimerIdx) {
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
