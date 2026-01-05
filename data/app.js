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
    document.querySelector('.card').style.display = 'none';
    initializeWebSocket();
    setupEventListeners();
    loadPresets();
    loadTimers();
    loadConfig();

    // OTA upload form handler
    const otaForm = document.getElementById('otaForm');
    if (otaForm) {
        otaForm.addEventListener('submit', async (e) => {
            e.preventDefault();
            const otaFileInput = document.getElementById('otaFile');
            const otaFile = otaFileInput.files[0];
            if (!otaFile) {
                showToast('Please select a firmware file.');
                return;
            }
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
    const otaProgressBar = document.getElementById('otaProgressBar');
    const otaProgressFill = document.getElementById('otaProgressFill');
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
        document.getElementById('statusIndicator').style.color = '#00cc88';
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
        document.getElementById('statusIndicator').style.color = '#ff4466';

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
function updateState(state) {
    currentState = state;
    // Show Quick Controls on first WebSocket message
    const quickControls = document.querySelector('.card');
    if (quickControls && quickControls.style.display === 'none') {
        quickControls.style.display = '';
    }
    
    // Update controls without triggering events
    const powerToggle = document.getElementById('powerToggle');
    if (powerToggle.checked !== state.power) {
        powerToggle.checked = state.power;
    }
    
    const brightnessSlider = document.getElementById('brightnessSlider');
    if (brightnessSlider.value != state.brightness) {
        brightnessSlider.value = state.brightness;
        document.getElementById('brightnessValue').textContent = state.brightness;
    }
    
    const effectSelect = document.getElementById('effectSelect');
    if (effectSelect.value != state.effect) {
        effectSelect.value = state.effect;
    }

    // Update transition slider and label from state
    if (typeof state.transitionTime !== 'undefined') {
        const transitionSlider = document.getElementById('transitionSlider');
        const transitionSeconds = Math.round(Number(state.transitionTime) / 1000);
        if (transitionSlider.value != transitionSeconds) {
            transitionSlider.value = transitionSeconds;
        }
        document.getElementById('transitionValue').textContent = transitionSeconds;
    }

    if (state.params) {
        document.getElementById('speedSlider').value = state.params.speed;
        document.getElementById('speedValue').textContent = state.params.speed;
        
        document.getElementById('intensitySlider').value = state.params.intensity;
        document.getElementById('intensityValue').textContent = state.params.intensity;
        
        document.getElementById('color1Picker').value = '#' + state.params.color1.toString(16).padStart(6, '0');
        document.getElementById('color2Picker').value = '#' + state.params.color2.toString(16).padStart(6, '0');
    }

    // Synchronize clock with backend on first WS message, then advance locally
    if (state.time) {
        if (!clockSynced) {
            document.getElementById('currentTime').textContent = state.time;
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
                document.getElementById('currentTime').textContent = `${hh}:${mm}:${ss}`;
            }, 1000);
            clockSynced = true;
        }
    }

    if (state.sunrise) {
        document.getElementById('sunriseTime').textContent = state.sunrise;
    }

    if (state.sunset) {
        document.getElementById('sunsetTime').textContent = state.sunset;
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
    document.getElementById('powerToggle').addEventListener('change', (e) => {
        sendState({ power: e.target.checked });
    });
    
    // Brightness slider
    document.getElementById('brightnessSlider').addEventListener('input', (e) => {
        document.getElementById('brightnessValue').textContent = e.target.value;
    });
    document.getElementById('brightnessSlider').addEventListener('change', (e) => {
        sendState({ brightness: parseInt(e.target.value) });
    });
    
    // Transition time slider
    // Only update label on input
    document.getElementById('transitionSlider').addEventListener('input', (e) => {
        const seconds = parseInt(e.target.value);
        document.getElementById('transitionValue').textContent = seconds;
    });
    // Only send to backend on release
    document.getElementById('transitionSlider').addEventListener('change', (e) => {
        const seconds = parseInt(e.target.value);
        sendState({ transitionTime: Number(seconds) * 1000 });
    });
    
    // Effect selector
    document.getElementById('effectSelect').addEventListener('change', (e) => {
        sendState({ effect: parseInt(e.target.value) });
    });
    
    // Speed slider
    let speedTimeout;
    document.getElementById('speedSlider').addEventListener('input', (e) => {
        document.getElementById('speedValue').textContent = e.target.value;
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
    
    // Intensity slider
    let intensityTimeout;
    document.getElementById('intensitySlider').addEventListener('input', (e) => {
        document.getElementById('intensityValue').textContent = e.target.value;
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
    
    // Color pickers
    document.getElementById('color1Picker').addEventListener('change', (e) => {
        const color = parseInt(e.target.value.substring(1), 16);
        sendState({ 
            params: {
                ...currentState.params,
                color1: color
            }
        });
    });
    
    document.getElementById('color2Picker').addEventListener('change', (e) => {
        const color = parseInt(e.target.value.substring(1), 16);
        sendState({ 
            params: {
                ...currentState.params,
                color2: color
            }
        });
    });
    
    // Configuration sliders
    document.getElementById('maxBrightness').addEventListener('input', (e) => {
        document.getElementById('maxBrightnessValue').textContent = e.target.value;
    });
    
    document.getElementById('minTransition').addEventListener('input', (e) => {
        document.getElementById('minTransitionValue').textContent = e.target.value;
    });
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
        })
        .catch(error => console.error('Error loading presets:', error));
}

function displayPresets() {
    const grid = document.getElementById('presetGrid');
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
            <div class="preset-info">Brightness: ${preset.brightness}</div>
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
        })
        .catch(error => console.error('Error loading timers:', error));
}

function displayTimers() {
    const container = document.getElementById('scheduleTable');
    
    const timerTypes = ['Regular', 'Sunrise', 'Sunset'];
    const dayNames = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
    
    let html = '<table><thead><tr><th>Time</th><th>Type</th><th>Days</th><th>Preset</th><th>Status</th></tr></thead><tbody>';
    
    timers.forEach(timer => {
        if (!timer.enabled) return;
        
        let timeStr = '';
        if (timer.type === 0) {
            timeStr = `${timer.hour.toString().padStart(2, '0')}:${timer.minute.toString().padStart(2, '0')}`;
        } else if (timer.type === 1) {
            timeStr = `Sunrise ${timer.offset >= 0 ? '+' : ''}${timer.offset}m`;
        } else {
            timeStr = `Sunset ${timer.offset >= 0 ? '+' : ''}${timer.offset}m`;
        }
        
        let daysStr = '';
        for (let i = 0; i < 7; i++) {
            if (timer.days & (1 << i)) {
                daysStr += dayNames[i] + ' ';
            }
        }
        
        const statusClass = timer.enabled ? 'timer-enabled' : 'timer-disabled';
        
        html += `
            <tr class="${statusClass}">
                <td>${timeStr}</td>
                <td>${timerTypes[timer.type]}</td>
                <td>${daysStr}</td>
                <td>${presets[timer.presetId]?.name || 'N/A'}</td>
                <td>${timer.enabled ? '✓ Active' : '✗ Disabled'}</td>
            </tr>
        `;
    });
    
    html += '</tbody></table>';
    container.innerHTML = html;
}

function showTimerEditor() {
    showToast('Timer editor UI - To be implemented in full version');
}

// Load configuration
function loadConfig() {
    fetch(BASE_URL + '/api/config')
        .then(async response => {
            const text = await response.text();
            if (!text) return {};
            try { return JSON.parse(text); } catch { return {}; }
        })
        .then(data => {
            config = data;

            // Update config UI
            if (data.led) {
                document.getElementById('ledCount').value = data.led.count;
                if (data.led.type) {
                    document.getElementById('ledType').value = data.led.type;
                }
                if (typeof data.led.relayPin !== 'undefined') {
                    document.getElementById('relayPin').value = data.led.relayPin;
                }
                if (typeof data.led.relayActiveHigh !== 'undefined') {
                    document.getElementById('relayActiveHigh').value = String(data.led.relayActiveHigh);
                }
            }

            if (data.safety) {
                document.getElementById('maxBrightness').value = data.safety.maxBrightness;
                document.getElementById('maxBrightnessValue').textContent = data.safety.maxBrightness;

                const minTransSeconds = Math.floor(Number(data.safety.minTransitionTime) / 1000);
                document.getElementById('minTransition').value = minTransSeconds;
                document.getElementById('minTransitionValue').textContent = minTransSeconds;
            }

            if (data.time) {
                document.getElementById('timezoneOffset').value = data.time.timezoneOffset;
                document.getElementById('latitude').value = data.time.latitude;
                document.getElementById('longitude').value = data.time.longitude;
            }
        })
        .catch(error => console.error('Error loading config:', error));
}

function saveConfiguration() {
    // Build configUpdate with only changed values
    const configUpdate = {};
    // LED
    const ledUpdate = {};
    const ledCount = parseInt(document.getElementById('ledCount').value);
    if (!config.led || config.led.count !== ledCount) ledUpdate.count = ledCount;
    const ledType = document.getElementById('ledType').value;
    if (!config.led || config.led.type !== ledType) ledUpdate.type = ledType;
    const relayPin = parseInt(document.getElementById('relayPin').value);
    if (!config.led || config.led.relayPin !== relayPin) ledUpdate.relayPin = relayPin;
    const relayActiveHigh = document.getElementById('relayActiveHigh').value === 'true';
    if (!config.led || config.led.relayActiveHigh !== relayActiveHigh) ledUpdate.relayActiveHigh = relayActiveHigh;
    if (Object.keys(ledUpdate).length > 0) configUpdate.led = ledUpdate;
    // Safety
    const safetyUpdate = {};
    const maxBrightness = parseInt(document.getElementById('maxBrightness').value);
    if (!config.safety || config.safety.maxBrightness !== maxBrightness) safetyUpdate.maxBrightness = maxBrightness;
    const minTransitionTime = Number(document.getElementById('minTransition').value) * 1000;
    if (!config.safety || config.safety.minTransitionTime !== minTransitionTime) safetyUpdate.minTransitionTime = minTransitionTime;
    if (Object.keys(safetyUpdate).length > 0) configUpdate.safety = safetyUpdate;
    // Time
    const timeUpdate = {};
    const timezoneOffset = parseInt(document.getElementById('timezoneOffset').value);
    if (!config.time || config.time.timezoneOffset !== timezoneOffset) timeUpdate.timezoneOffset = timezoneOffset;
    const latitude = parseFloat(document.getElementById('latitude').value);
    if (!config.time || config.time.latitude !== latitude) timeUpdate.latitude = latitude;
    const longitude = parseFloat(document.getElementById('longitude').value);
    if (!config.time || config.time.longitude !== longitude) timeUpdate.longitude = longitude;
    if (Object.keys(timeUpdate).length > 0) configUpdate.time = timeUpdate;
    // Only send if something changed
    if (Object.keys(configUpdate).length === 0) {
        showToast('No changes to save.');
        return;
    }
    fetch(BASE_URL + '/api/config', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(configUpdate)
    })
    .then(async response => {
        const statusOk = response.ok;
        let data = {};
        try {
            const text = await response.text();
            if (text) data = JSON.parse(text);
        } catch (e) {
            // Ignore JSON parse error, treat as success if HTTP 200
        }
        return { statusOk, data };
    })
    .then(({ statusOk, data }) => {
        if (statusOk && (data.success === undefined || data.success === true)) {
            showToast('Configuration saved! Changes take effect immediately.');
            sendDebugWS('Config saved successfully');
        } else {
            showToast('Failed to save configuration');
            sendDebugWS('Config save failed');
        }
    })
    .catch(error => {
        console.error('Error:', error);
        showToast('Error saving configuration');
        sendDebugWS('Config save error: ' + error);
    });
}

// Toast notification (moved to top level)
function showToast(message, duration = 3000) {
    const toast = document.getElementById('toast');
    console.log('[TOAST]', message); // DEBUG: log to console
    toast.textContent = message;
    toast.style.display = 'block';
    toast.style.opacity = '0.95';
    if (window._toastTimeout) {
        clearTimeout(window._toastTimeout);
    }
    if (message === 'Uploading...') {
        // Don't auto-hide, will be hidden by next toast
        return;
    }
    window._toastTimeout = setTimeout(() => {
        toast.style.opacity = '0';
        setTimeout(() => { toast.style.display = 'none'; }, 300);
    }, duration);
}

// Send debug message through WebSocket (moved to top level)
function sendDebugWS(msg) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ debug: msg }));
    }
}
