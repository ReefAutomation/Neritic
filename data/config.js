// config.js
// Handles configuration page logic for config.html

// Ensure global config variable
window.config = window.config || {};

// Load timezones from API and populate combo
let TIMEZONES = [];
fetch(BASE_URL + '/api/timezones')
    .then(resp => resp.json())
    .then(zones => { TIMEZONES = zones; })
    .catch(() => { TIMEZONES = []; });

// Helper to compare objects shallowly
function shallowEqual(obj1, obj2) {
    if (obj1 === obj2) return true;
    if (!obj1 || !obj2) return false;
    const keys1 = Object.keys(obj1);
    const keys2 = Object.keys(obj2);
    if (keys1.length !== keys2.length) return false;
    for (let k of keys1) if (obj1[k] !== obj2[k]) return false;
    return true;
}

let lastTimers = [];
let lastTimeSettings = {};

function displayConfig() {
    // LED
    if (window.config.led) {
        if (window.config.led.pin !== undefined) document.getElementById('ledPin').value = window.config.led.pin;
        if (window.config.led.count !== undefined) document.getElementById('ledCount').value = window.config.led.count;
        if (window.config.led.type) document.getElementById('ledType').value = window.config.led.type;
        if (window.config.led.colorOrder) document.getElementById('ledColorOrder').value = window.config.led.colorOrder;
        if (window.config.led.relayPin !== undefined) document.getElementById('relayPin').value = window.config.led.relayPin;
        if (typeof window.config.led.relayActiveHigh !== 'undefined') document.getElementById('relayActiveHigh').value = String(window.config.led.relayActiveHigh);
    }
    // Safety
    if (window.config.safety) {
        if (window.config.safety.maxBrightness !== undefined) {
            const percent = Math.round(((window.config.safety.maxBrightness - 1) / 254) * 100);
            document.getElementById('maxBrightness').value = percent;
            document.getElementById('maxBrightnessValue').textContent = percent + '%';
        }
        if (window.config.safety.minTransitionTime !== undefined) {
            const minTransSeconds = Math.floor(Number(window.config.safety.minTransitionTime) / 1000);
            document.getElementById('minTransition').value = minTransSeconds;
            document.getElementById('minTransitionValue').textContent = minTransSeconds;
        }
    }
    // Time
    if (window.config.time) {
        if (window.config.time.ntpServer !== undefined) document.getElementById('ntpServer').value = window.config.time.ntpServer;
        const tzSelect = document.getElementById('timezone');
        if (tzSelect && TIMEZONES.length > 0) {
            if (tzSelect.options.length !== TIMEZONES.length) {
                tzSelect.innerHTML = '';
                TIMEZONES.forEach(tzName => {
                    const opt = document.createElement('option');
                    opt.value = tzName;
                    opt.textContent = tzName;
                    tzSelect.appendChild(opt);
                });
            }
            if (window.config.time.timezone) tzSelect.value = window.config.time.timezone;
        }
        if (window.config.time.latitude !== undefined) document.getElementById('latitude').value = window.config.time.latitude;
        if (window.config.time.longitude !== undefined) document.getElementById('longitude').value = window.config.time.longitude;
        if (typeof window.config.time.dstEnabled !== 'undefined') document.getElementById('dstEnabled').checked = !!window.config.time.dstEnabled;
    }
    // Network (WiFi)
    if (window.config.network) {
        if (window.config.network.ssid !== undefined) document.getElementById('wifiSsid').value = window.config.network.ssid;
        if (window.config.network.password !== undefined) document.getElementById('wifiPassword').value = window.config.network.password;
    }
    // Only update schedule table if timers changed
    if (!shallowEqual(window.config.timers, lastTimers)) {
        lastTimers = JSON.parse(JSON.stringify(window.config.timers));
        // ...existing table update code...
        const table = document.getElementById('scheduleTableConfig');
        if (table) {
            const tbody = table.querySelector('tbody');
            tbody.innerHTML = '';
            if (!Array.isArray(window.config.timers)) window.config.timers = [];
            const sortedTimers = [...window.config.timers].sort((a, b) => {
                const isSunA = a.type === 1 || a.type === 2;
                const isSunB = b.type === 1 || b.type === 2;
                if (isSunA && !isSunB) return 1;
                if (!isSunA && isSunB) return -1;
                if (isSunA && isSunB) return 0;
                const isZeroA = (a.hour === 0 && a.minute === 0);
                const isZeroB = (b.hour === 0 && b.minute === 0);
                if (isZeroA && !isZeroB) return 1;
                if (!isZeroA && isZeroB) return -1;
                if (isZeroA && isZeroB) return 0;
                const ta = (a.hour || 0) * 60 + (a.minute || 0);
                const tb = (b.hour || 0) * 60 + (b.minute || 0);
                return ta - tb;
            });
            sortedTimers.forEach((timer, idx) => {
                const tr = document.createElement('tr');
                // Store original index to update correct timer
                const originalIdx = window.config.timers.indexOf(timer);
                // Enabled checkbox
                const enabledTd = document.createElement('td');
                const enabledInput = document.createElement('input');
                enabledInput.type = 'checkbox';
                enabledInput.checked = !!timer.enabled;
                enabledInput.addEventListener('change', () => {
                    window.config.timers[originalIdx].enabled = enabledInput.checked;
                });
                enabledTd.appendChild(enabledInput);
                tr.appendChild(enabledTd);
                // Type select
                const typeTd = document.createElement('td');
                const typeSelect = document.createElement('select');
                ['Regular', 'Sunrise', 'Sunset'].forEach((label, val) => {
                    const opt = document.createElement('option');
                    opt.value = val;
                    opt.textContent = label;
                    if (timer.type === val) opt.selected = true;
                    typeSelect.appendChild(opt);
                });
                typeSelect.addEventListener('change', () => {
                    window.config.timers[originalIdx].type = parseInt(typeSelect.value);
                });
                typeTd.appendChild(typeSelect);
                tr.appendChild(typeTd);
                // Time input (HH:MM, disabled for sunrise/sunset)
                const timeTd = document.createElement('td');
                const timeInput = document.createElement('input');
                timeInput.type = 'time';
                timeInput.value = `${String(timer.hour).padStart(2, '0')}:${String(timer.minute).padStart(2, '0')}`;
                timeInput.addEventListener('change', () => {
                    const [h, m] = timeInput.value.split(':').map(Number);
                    window.config.timers[originalIdx].hour = h || 0;
                    window.config.timers[originalIdx].minute = m || 0;
                });
                timeTd.appendChild(timeInput);
                tr.appendChild(timeTd);
                // Disable time input for sunrise/sunset
                function updateTimeInput() {
                    const isSun = typeSelect.value === '1' || typeSelect.value === '2';
                    timeInput.disabled = isSun;
                }
                typeSelect.addEventListener('change', updateTimeInput);
                updateTimeInput();
                // Preset select
                const presetTd = document.createElement('td');
                const presetSelect = document.createElement('select');
                if (window.presets && window.presets.length > 0) {
                    window.presets.forEach((preset) => {
                        const opt = document.createElement('option');
                        opt.value = preset.id;
                        opt.textContent = preset.name || `Preset ${preset.id}`;
                        if (timer.presetId === preset.id) opt.selected = true;
                        presetSelect.appendChild(opt);
                    });
                } else {
                    const opt = document.createElement('option');
                    opt.value = 0;
                    opt.textContent = 'No presets available';
                    presetSelect.appendChild(opt);
                }
                presetSelect.addEventListener('change', () => {
                    window.config.timers[originalIdx].presetId = parseInt(presetSelect.value);
                });
                presetTd.appendChild(presetSelect);
                tr.appendChild(presetTd);
                // Brightness input
                const brightTd = document.createElement('td');
                const brightInput = document.createElement('input');
                brightInput.type = 'number';
                brightInput.min = 0;
                brightInput.max = 100;
                brightInput.value = timer.brightness;
                brightInput.style.width = '60px';
                brightInput.addEventListener('change', () => {
                    const val = Math.max(0, Math.min(100, parseInt(brightInput.value) || 0));
                    window.config.timers[originalIdx].brightness = val;
                    brightInput.value = val;
                });
                brightTd.appendChild(brightInput);
                tr.appendChild(brightTd);
                // Actions (Delete button)
                const actionsTd = document.createElement('td');
                const delBtn = document.createElement('button');
                delBtn.textContent = 'Delete';
                delBtn.className = 'btn btn-danger btn-sm';
                delBtn.onclick = () => {
                    window.config.timers.splice(idx, 1);
                    displayConfig();
                };
                actionsTd.appendChild(delBtn);
                tr.appendChild(actionsTd);
                tbody.appendChild(tr);
            });
            // ...existing Add Timer button logic...
        }
        // Re-evaluate schedule if timers changed
        if (typeof reevaluateSchedule === 'function') reevaluateSchedule();
    }
    // Only re-evaluate schedule if time settings changed
    const currentTimeSettings = window.config.time ? {
        timezone: window.config.time.timezone,
        latitude: window.config.time.latitude,
        longitude: window.config.time.longitude,
        dstEnabled: window.config.time.dstEnabled
    } : {};
    if (!shallowEqual(currentTimeSettings, lastTimeSettings)) {
        lastTimeSettings = { ...currentTimeSettings };
        if (typeof reevaluateSchedule === 'function') reevaluateSchedule();
    }
}

function loadConfig() {
    fetch(BASE_URL + '/api/config')
        .then(async response => {
            const text = await response.text();
            if (!text) return {};
            try { return JSON.parse(text); } catch { return {}; }
        })
        .then(data => {
            window.config = data;
            displayConfig();
        })
        .catch(error => console.error('Error loading config:', error));
}

function loadPresetsAndConfig() {
    fetch(BASE_URL + '/api/presets')
        .then(resp => resp.json())
        .then(presetsData => {
            // If backend returns { presets: [...] }, extract the array
            if (Array.isArray(presetsData)) {
                window.presets = presetsData;
            } else if (presetsData && Array.isArray(presetsData.presets)) {
                window.presets = presetsData.presets;
            } else {
                window.presets = [];
            }
            fetch(BASE_URL + '/api/config')
                .then(async response => {
                    const text = await response.text();
                    if (!text) return {};
                    try { return JSON.parse(text); } catch { return {}; }
                })
                .then(data => {
                    window.config = data;
                    displayConfig();
                })
                .catch(error => console.error('Error loading config:', error));
        })
        .catch(() => {
            window.presets = [];
            fetch(BASE_URL + '/api/config')
                .then(async response => {
                    const text = await response.text();
                    if (!text) return {};
                    try { return JSON.parse(text); } catch { return {}; }
                })
                .then(data => {
                    window.config = data;
                    displayConfig();
                })
                .catch(error => console.error('Error loading config:', error));
        });
}

// Only use loadPresetsAndConfig for DOMContentLoaded
window.addEventListener('DOMContentLoaded', loadPresetsAndConfig);

// Update display values for sliders
['maxBrightness', 'minTransition'].forEach(id => {
    const input = document.getElementById(id);
    if (input) {
        input.addEventListener('input', e => {
            document.getElementById(id + 'Value').textContent = e.target.value;
        });
    }
});


async function sendCommandWithStatus(command) {
    try {
        const response = await fetch(BASE_URL + '/api/command', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ command })
        });
        const text = await response.text();
        if (!text) return { success: false };
        try {
            return JSON.parse(text);
        } catch {
            return { success: false };
        }
    } catch (e) {
        return { success: false, error: e.message };
    }
}

function saveConfig() {
    // Collect latest values from inputs before sending
    // LED
    if (window.config.led) {
        window.config.led.pin = parseInt(document.getElementById('ledPin').value) || window.config.led.pin;
        window.config.led.count = parseInt(document.getElementById('ledCount').value) || window.config.led.count;
        window.config.led.type = document.getElementById('ledType').value || window.config.led.type;
        window.config.led.colorOrder = document.getElementById('ledColorOrder').value || window.config.led.colorOrder;
        window.config.led.relayPin = parseInt(document.getElementById('relayPin').value) || window.config.led.relayPin;
        window.config.led.relayActiveHigh = document.getElementById('relayActiveHigh').value === 'true';
    }
    // Safety
    if (window.config.safety) {
        window.config.safety.maxBrightness = Math.max(1, Math.min(100, parseInt(document.getElementById('maxBrightness').value) || window.config.safety.maxBrightness));
        window.config.safety.minTransitionTime = Math.max(2, parseInt(document.getElementById('minTransition').value) || window.config.safety.minTransitionTime) * 1000;
    }
    // Time
    if (window.config.time) {
        window.config.time.ntpServer = document.getElementById('ntpServer').value || window.config.time.ntpServer;
        window.config.time.timezone = document.getElementById('timezone').value || window.config.time.timezone;
        window.config.time.latitude = parseFloat(document.getElementById('latitude').value) || window.config.time.latitude;
        window.config.time.longitude = parseFloat(document.getElementById('longitude').value) || window.config.time.longitude;
        window.config.time.dstEnabled = document.getElementById('dstEnabled').checked;
    }
    // Network
    if (window.config.network) {
        window.config.network.ssid = document.getElementById('wifiSsid').value || window.config.network.ssid;
        window.config.network.password = document.getElementById('wifiPassword').value || window.config.network.password;
    }
    // Timers: already updated live via event listeners
    // Sort timers: regular by time, 00:00 last, sun types very last
    if (Array.isArray(window.config.timers)) {
        window.config.timers.sort((a, b) => {
            const isSunA = a.type === 1 || a.type === 2;
            const isSunB = b.type === 1 || b.type === 2;
            if (isSunA && !isSunB) return 1;
            if (!isSunA && isSunB) return -1;
            if (isSunA && isSunB) return 0;
            const isZeroA = (a.hour === 0 && a.minute === 0);
            const isZeroB = (b.hour === 0 && b.minute === 0);
            if (isZeroA && !isZeroB) return 1;
            if (!isZeroA && isZeroB) return -1;
            if (isZeroA && isZeroB) return 0;
            const ta = (a.hour || 0) * 60 + (a.minute || 0);
            const tb = (b.hour || 0) * 60 + (b.minute || 0);
            return ta - tb;
        });
    }
    // Send config to backend
    fetch(BASE_URL + '/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(window.config)
    })
    .then(async response => {
        if (!response.ok) throw new Error('Save failed');
        const text = await response.text();
        if (!text) return;
        try {
            JSON.parse(text);
        } catch {
            // Not valid JSON, but save succeeded
        }
    })
    .then(() => {
        const saveBtn = document.getElementById('saveConfigButton');
        if (saveBtn) {
            const originalText = saveBtn.textContent;
            saveBtn.textContent = '✓ Saved!';
            saveBtn.style.backgroundColor = '#4CAF50';
            setTimeout(() => {
                saveBtn.textContent = originalText;
                saveBtn.style.backgroundColor = '';
            }, 2000);
        }
        loadConfig();
    })
    .catch(err => {
        const saveBtn = document.getElementById('saveConfigButton');
        if (saveBtn) {
            const originalText = saveBtn.textContent;
            saveBtn.textContent = '✗ Error';
            saveBtn.style.backgroundColor = '#f44336';
            setTimeout(() => {
                saveBtn.textContent = originalText;
                saveBtn.style.backgroundColor = '';
            }, 2000);
        }
        console.error('Error saving config:', err);
    });
}
// Attach Save button handler if not already
const saveBtn = document.getElementById('saveConfigButton');
if (saveBtn && !saveBtn._handlerSet) {
    saveBtn.onclick = saveConfig;
    saveBtn._handlerSet = true;
}
