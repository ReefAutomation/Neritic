// config.js
// Handles configuration page logic for config.html

// Ensure global config variable
window.config = window.config || {};

// Load timezones from API and populate combo
let TIMEZONES = [];
let _loaded = { timezones: false, presets: false, config: false };

// Track if transition times were changed
let transitionTimesChanged = false;

// --- Sun times WebSocket handler ---
function handleSunTimesFromWs(event) {
    try {
        const data = JSON.parse(event.data);
        let updated = false;
        if ('sunrise' in data) {
            window.sunriseTime = data.sunrise;
            updated = true;
        }
        if ('sunset' in data) {
            window.sunsetTime = data.sunset;
            updated = true;
        }
        if (updated) {
            window._sunTimesFirstReceived = true;
            if (typeof displayConfig === 'function') displayConfig();
        }
    } catch (e) {
    }
}

// --- Attach sun times listener to ws as early as possible ---
if (!window._sunTimesWsListenerAdded) {
    window._sunTimesWsListenerAdded = true;
    if (window.ws) {
        window.ws.addEventListener('message', handleSunTimesFromWs);
    }
    Object.defineProperty(window, 'ws', {
        set(v) {
            this._ws = v;
            if (v) {
                v.addEventListener('message', handleSunTimesFromWs);
            }
        },
        get() { return this._ws; }
    });
}

function showMainContainerWhenReady() {
    if (_loaded.timezones && _loaded.presets && _loaded.config) {
        document.getElementById('loadingIndicator').style.display = 'none';
        document.getElementById('mainContainer').style.display = '';
        displayConfig();
    }
}

fetch(BASE_URL + '/api/timezones')
    .then(resp => resp.json())
    .then(zones => { TIMEZONES = zones; _loaded.timezones = true; showMainContainerWhenReady(); })
    .catch(() => { TIMEZONES = []; _loaded.timezones = true; showMainContainerWhenReady(); });

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
            const percent = window.config.safety.maxBrightness;
            document.getElementById('maxBrightness').value = percent;
            document.getElementById('maxBrightnessValue').textContent = percent + '%';
        }
        if (window.config.safety.minTransitionTime !== undefined) {
            // Convert ms to slider value using custom mapping
            const ms = Number(window.config.safety.minTransitionTime);
            let sliderVal = 0;
            const sec = Math.round(ms / 1000);
            if (sec <= 59) sliderVal = sec;
            else if (sec < 3600) sliderVal = 59 + Math.round(sec / 60);
            else sliderVal = 119 + Math.round(sec / 3600);
            document.getElementById('minTransition').value = sliderVal;
            document.getElementById('minTransitionValue').textContent = formatTransitionTime(sec);
        }
    }
    // Transition Times
    if (window.config.transitionTimes) {
        const ttFields = [
            { id: 'ttPowerOn', val: window.config.transitionTimes.powerOn },
            { id: 'ttSchedule', val: window.config.transitionTimes.schedule },
            { id: 'ttManual', val: window.config.transitionTimes.manual },
            { id: 'ttEffect', val: window.config.transitionTimes.effect }
        ];
        ttFields.forEach(f => {
            const el = document.getElementById(f.id);
            let sliderVal = 0;
            const sec = Math.round((f.val || 0) / 1000);
            if (sec <= 59) sliderVal = sec;
            else if (sec < 3600) sliderVal = 59 + Math.round(sec / 60);
            else sliderVal = 119 + Math.round(sec / 3600);
            if (el) el.value = sliderVal;
            const valSpan = document.getElementById(f.id + 'Value');
            if (valSpan) valSpan.textContent = formatTransitionTime(sec);
        });
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
        // Always show password as asterisks, even if empty
        const pwElem = document.getElementById('wifiPassword');
        if (pwElem) {
            pwElem.value = '********';
        }
    }
    // Only update schedule table if timers changed
    if (!shallowEqual(window.config.timers, lastTimers)) {
        lastTimers = JSON.parse(JSON.stringify(window.config.timers));
        const table = document.getElementById('scheduleTableConfig');
        console.debug('[SCHEDULE] Rendering schedule table. Timers:', window.config.timers);
        if (table) {
            const tbody = table.querySelector('tbody');
            tbody.innerHTML = '';
            if (!Array.isArray(window.config.timers)) window.config.timers = [];
            // Render timers in their current order (no sorting)
            window.config.timers.forEach((timer, idx) => {
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
                const typeNames = ['Regular', 'Sunrise', 'Sunset'];
                typeNames.forEach((label, val) => {
                    const radio = document.createElement('input');
                    radio.type = 'radio';
                    radio.name = `type_${originalIdx}`;
                    radio.value = val;
                    radio.checked = (timer.type === val);
                    radio.addEventListener('change', () => {
                        if (radio.checked) {
                            // Prevent multiple Sunrise or Sunset
                            if (val === 1 || val === 2) {
                                window.config.timers.forEach((t, i) => {
                                    if (i !== originalIdx && t.type === val) {
                                        t.type = 0; // Set to Regular
                                    }
                                });
                            }
                            window.config.timers[originalIdx].type = val;
                            // Re-render table to update radios and sunrise/sunset label
                            if (typeof displayConfig === 'function') displayConfig();
                            else location.reload();
                        }
                    });
                    const radioLabel = document.createElement('label');
                    radioLabel.style.marginRight = '0.5em';
                    radioLabel.appendChild(radio);
                    radioLabel.appendChild(document.createTextNode(label));
                    typeTd.appendChild(radioLabel);
                });
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
                const sunLabel = document.createElement('span');
                sunLabel.style.display = 'none';
                function updateTimeInput() {
                    const typeVal = window.config.timers[originalIdx].type;
                    if (typeVal === 1) {
                        // Sunrise
                        timeInput.style.display = 'none';
                        sunLabel.style.display = '';
                        if (window.sunriseTime) {
                            sunLabel.textContent = window.sunriseTime;
                        } else {
                            sunLabel.textContent = '';
                            // Only show toast if the label is visible and no value is present after a short delay
                            setTimeout(() => {
                                if (!window.sunriseTime && sunLabel.offsetParent !== null) {
                                    if (typeof showToast === 'function') showToast('Sunrise time not available from device.', 4000);
                                } else if (window.sunriseTime) {
                                    sunLabel.textContent = window.sunriseTime;
                                }
                            }, 500);
                        }
                    } else if (typeVal === 2) {
                        // Sunset
                        timeInput.style.display = 'none';
                        sunLabel.style.display = '';
                        if (window.sunsetTime) {
                            sunLabel.textContent = window.sunsetTime;
                        } else {
                            sunLabel.textContent = '';
                            setTimeout(() => {
                                if (!window.sunsetTime && sunLabel.offsetParent !== null) {
                                    if (typeof showToast === 'function') showToast('Sunset time not available from device.', 4000);
                                } else if (window.sunsetTime) {
                                    sunLabel.textContent = window.sunsetTime;
                                }
                            }, 500);
                        }
                    } else {
                        timeInput.style.display = '';
                        sunLabel.style.display = 'none';
                    }
                }
                timeTd.appendChild(timeInput);
                timeTd.appendChild(sunLabel);
                tr.appendChild(timeTd);
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
            // Store a deep copy of the original config for change detection
            window._originalConfig = JSON.parse(JSON.stringify(data));
            _loaded.config = true;
            showMainContainerWhenReady();
        })
        .catch(error => { _loaded.config = true; showMainContainerWhenReady(); console.error('Error loading config:', error); });
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
            _loaded.presets = true;
            loadConfig();
        })
        .catch(() => {
            window.presets = [];
            _loaded.presets = true;
            loadConfig();
        });
}

// Only use loadPresetsAndConfig for DOMContentLoaded
window.addEventListener('DOMContentLoaded', loadPresetsAndConfig);

// Add Timer button handler
window.addEventListener('DOMContentLoaded', function() {
    const addTimerBtn = document.getElementById('addTimerButton');
    if (addTimerBtn && !addTimerBtn._handlerSet) {
        addTimerBtn.onclick = function() {
            if (!Array.isArray(window.config.timers)) window.config.timers = [];
            // Add a default timer object
            window.config.timers.push({
                enabled: true,
                type: 0, // Regular
                hour: 12,
                minute: 0,
                presetId: (window.presets && window.presets.length > 0) ? window.presets[0].id : 0,
                brightness: 80
            });
            displayConfig();
        };
        addTimerBtn._handlerSet = true;
    }
});

// Update display values for sliders
// Show % for maxBrightness slider, plain value for minTransition
const maxBrightnessInput = document.getElementById('maxBrightness');
if (maxBrightnessInput) {
    maxBrightnessInput.addEventListener('input', e => {
        document.getElementById('maxBrightnessValue').textContent = e.target.value + '%';
    });
}


const minTransitionInput = document.getElementById('minTransition');
if (minTransitionInput) {
    minTransitionInput.min = 0;
    minTransitionInput.max = 127;
    minTransitionInput.step = 1;
    minTransitionInput.addEventListener('input', e => {
        let seconds = steppedTransitionValue(e.target.value);
        minTransitionInput.value = e.target.value;
        document.getElementById('minTransitionValue').textContent = formatTransitionTime(seconds);
    });
    // Set initial display with unit
    let initial = minTransitionInput.value;
    let seconds = steppedTransitionValue(initial);
    minTransitionInput.value = initial;
    document.getElementById('minTransitionValue').textContent = formatTransitionTime(seconds);
}

// Transition Times sliders: stepped and show value
['ttPowerOn','ttSchedule','ttManual','ttEffect'].forEach(id => {
    const el = document.getElementById(id);
    const valSpan = document.getElementById(id + 'Value');
    if (el && valSpan) {
        el.min = 0;
        el.max = 127;
        el.step = 1;
        el.addEventListener('input', e => {
            let seconds = steppedTransitionValue(e.target.value);
            el.value = e.target.value;
            valSpan.textContent = formatTransitionTime(seconds);
            transitionTimesChanged = true;
        });
        // Set initial display with unit
        let initial = el.value;
        let seconds = steppedTransitionValue(initial);
        el.value = initial;
        valSpan.textContent = formatTransitionTime(seconds);
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

function deepEqual(a, b) {
    if (a === b) return true;
    if (typeof a !== typeof b) return false;
    if (typeof a !== 'object' || a === null || b === null) return false;
    if (Array.isArray(a) !== Array.isArray(b)) return false;
    if (Array.isArray(a)) {
        if (a.length !== b.length) return false;
        for (let i = 0; i < a.length; i++) {
            if (!deepEqual(a[i], b[i])) return false;
        }
        return true;
    }
    const keysA = Object.keys(a);
    const keysB = Object.keys(b);
    if (keysA.length !== keysB.length) return false;
    for (let k of keysA) {
        if (!deepEqual(a[k], b[k])) return false;
    }
    return true;
}

function saveConfig() {
    // Build a partial update object with only changed fields
    const update = {};
    const orig = window._originalConfig || {};
    // LED
    if (window.config.led) {
        const ledUpdate = {};
        const ledPin = parseInt(document.getElementById('ledPin').value);
        if (!orig.led || ledPin !== orig.led.pin) ledUpdate.pin = ledPin;
        const ledCount = parseInt(document.getElementById('ledCount').value);
        if (!orig.led || ledCount !== orig.led.count) ledUpdate.count = ledCount;
        const ledType = document.getElementById('ledType').value;
        if (!orig.led || ledType !== orig.led.type) ledUpdate.type = ledType;
        const ledColorOrder = document.getElementById('ledColorOrder').value;
        if (!orig.led || ledColorOrder !== orig.led.colorOrder) ledUpdate.colorOrder = ledColorOrder;
        const relayPin = parseInt(document.getElementById('relayPin').value);
        if (!orig.led || relayPin !== orig.led.relayPin) ledUpdate.relayPin = relayPin;
        const relayActiveHigh = document.getElementById('relayActiveHigh').value === 'true';
        if (!orig.led || relayActiveHigh !== orig.led.relayActiveHigh) ledUpdate.relayActiveHigh = relayActiveHigh;
        if (Object.keys(ledUpdate).length > 0) update.led = ledUpdate;
    }
    // Safety
    if (window.config.safety) {
        const safetyUpdate = {};
        const maxBrightness = Math.max(1, Math.min(100, parseInt(document.getElementById('maxBrightness').value)));
        if (!orig.safety || maxBrightness !== orig.safety.maxBrightness) safetyUpdate.maxBrightness = maxBrightness;
        // Convert slider value to seconds using custom mapping
        const sliderVal = parseInt(document.getElementById('minTransition').value);
        let minTransitionTime = 0;
        if (sliderVal <= 59) minTransitionTime = sliderVal * 1000;
        else if (sliderVal <= 119) minTransitionTime = (sliderVal - 59) * 60 * 1000;
        else if (sliderVal <= 127) minTransitionTime = (sliderVal - 119) * 3600 * 1000;
        else minTransitionTime = 28800 * 1000;
        if (!orig.safety || minTransitionTime !== orig.safety.minTransitionTime) safetyUpdate.minTransitionTime = minTransitionTime;
        if (Object.keys(safetyUpdate).length > 0) update.safety = safetyUpdate;
    }
    // Transition Times: only include if changed
    if (transitionTimesChanged && window.config.transitionTimes) {
        const ttUpdate = {};
        function steppedTransitionMs(val) {
            val = parseInt(val);
            if (val <= 59) return val * 1000;
            if (val <= 119) return (val - 59) * 60 * 1000;
            if (val <= 127) return (val - 119) * 3600 * 1000;
            return 28800 * 1000;
        }
        const powerOn = steppedTransitionMs(document.getElementById('ttPowerOn').value);
        const schedule = steppedTransitionMs(document.getElementById('ttSchedule').value);
        const manual = steppedTransitionMs(document.getElementById('ttManual').value);
        const effect = steppedTransitionMs(document.getElementById('ttEffect').value);
        if (window.config.transitionTimes.powerOn !== powerOn) ttUpdate.powerOn = powerOn;
        if (window.config.transitionTimes.schedule !== schedule) ttUpdate.schedule = schedule;
        if (window.config.transitionTimes.manual !== manual) ttUpdate.manual = manual;
        if (window.config.transitionTimes.effect !== effect) ttUpdate.effect = effect;
        if (Object.keys(ttUpdate).length > 0) update.transitionTimes = ttUpdate;
    }
    // Time
    if (window.config.time) {
        const timeUpdate = {};
        const ntpServer = document.getElementById('ntpServer').value;
        if (!orig.time || ntpServer !== orig.time.ntpServer) timeUpdate.ntpServer = ntpServer;
        const timezone = document.getElementById('timezone').value;
        if (!orig.time || timezone !== orig.time.timezone) timeUpdate.timezone = timezone;
        const latitude = parseFloat(document.getElementById('latitude').value);
        if (!orig.time || latitude !== orig.time.latitude) timeUpdate.latitude = latitude;
        const longitude = parseFloat(document.getElementById('longitude').value);
        if (!orig.time || longitude !== orig.time.longitude) timeUpdate.longitude = longitude;
        const dstEnabled = document.getElementById('dstEnabled').checked;
        if (!orig.time || dstEnabled !== orig.time.dstEnabled) timeUpdate.dstEnabled = dstEnabled;
        if (Object.keys(timeUpdate).length > 0) update.time = timeUpdate;
    }
    // Network
    if (window.config.network) {
        const netUpdate = {};
        const ssid = document.getElementById('wifiSsid').value;
        if (!orig.network || ssid !== orig.network.ssid) netUpdate.ssid = ssid;
        const passwordElem = document.getElementById('wifiPassword');
        const password = passwordElem.value;
        // Only send password if user explicitly changed it (not asterisks placeholder)
        if (password && password !== '********' && (!orig.network || password !== orig.network.password)) {
            netUpdate.password = password;
        }
        if (Object.keys(netUpdate).length > 0) update.network = netUpdate;
    }
    // Timers: only send if changed compared to original config
    if (Array.isArray(window.config.timers) && Array.isArray(orig.timers)) {
        if (!deepEqual(window.config.timers, orig.timers)) {
            update.timers = window.config.timers;
        }
    }
    // Send only changed fields to backend
    // Reset transitionTimesChanged after save
    fetch(BASE_URL + '/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(update)
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
        transitionTimesChanged = false;
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

// Attach Reboot button handler
const rebootBtn = document.getElementById('rebootButton');
if (rebootBtn && !rebootBtn._handlerSet) {
    rebootBtn.onclick = async function () {
        rebootBtn.disabled = true;
        rebootBtn.textContent = 'Rebooting...';
        // Use toast for status
        showToast('', 'info');
        try {
            const result = await sendCommandWithStatus('reboot');
            if (result && result.success) {
                showToast('Rebooting device...', 'info');
                rebootBtn.textContent = 'Rebooting...';
                setTimeout(() => {
                    rebootBtn.textContent = 'Reboot Device';
                    rebootBtn.disabled = false;
                    if (statusSpan) statusSpan.textContent = '';
                }, 8000);
            } else {
                showToast('Reboot failed!', 'error');
                rebootBtn.textContent = 'Reboot Device';
                rebootBtn.disabled = false;
            }
        } catch (e) {
            showToast('Reboot error!', 'error');
            rebootBtn.textContent = 'Reboot Device';
            rebootBtn.disabled = false;
        }
    };
    rebootBtn._handlerSet = true;
}

// Attach Update button handler
// Attach GPS button handler
const gpsBtn = document.getElementById('getLocationBtn');
if (gpsBtn && !gpsBtn._handlerSet) {
    window._gpsListenerSet = window._gpsListenerSet || false;
    gpsBtn.onclick = function () {
        if (!window._gpsListenerSet) {
            window.addEventListener('message', function (event) {
                if (event.origin !== 'https://locate.wled.me') return;
                if (event.data && typeof event.data === 'object' && 'lat' in event.data && 'lon' in event.data) {
                    document.getElementById('latitude').value = event.data.lat;
                    document.getElementById('longitude').value = event.data.lon;
                    if (window._gpsPopup && !window._gpsPopup.closed) window._gpsPopup.close();
                }
            }, false);
            window._gpsListenerSet = true;
        }
        // Open as larger popup window
        const w = 500, h = 500;
        const left = window.screenX + (window.outerWidth - w) / 2;
        const top = window.screenY + (window.outerHeight - h) / 2;
        window._gpsPopup = window.open('https://locate.wled.me', 'wled_gps', `width=${w},height=${h},left=${left},top=${top},resizable,scrollbars`);
        if (window._gpsPopup) window._gpsPopup.focus();
    };
    gpsBtn._handlerSet = true;
}

const updateBtn = document.getElementById('updateButton');
if (updateBtn && !updateBtn._handlerSet) {
    const updateSpinner = document.getElementById('updateSpinner');
    updateBtn.onclick = async function () {
        updateBtn.disabled = true;
        if (updateSpinner) updateSpinner.style.display = '';
        updateBtn.childNodes[0].textContent = 'Checking... ';
        showToast('', 'info');
        try {
            // Call backend endpoint to check and install update
            const resp = await fetch(BASE_URL + '/api/update', { method: 'POST' });
            const result = await resp.json();
            if (result && result.success) {
                updateBtn.childNodes[0].textContent = 'Updating... ';
                showToast('Installing update... Device will reboot.', 'info');
                // Spinner will be hidden by WebSocket OTA status handler
            } else {
                updateBtn.childNodes[0].textContent = 'Check for Updates ';
                showToast(result && result.message ? result.message : 'No update found.', 'info');
                if (updateSpinner) updateSpinner.style.display = 'none';
                updateBtn.disabled = false;
            }
        } catch (e) {
            updateBtn.childNodes[0].textContent = 'Check for Updates ';
            showToast('Update check failed!', 'error');
            if (updateSpinner) updateSpinner.style.display = 'none';
            updateBtn.disabled = false;
        }
    };
    updateBtn._handlerSet = true;
}

// --- Upload Config Handler ---
const uploadInput = document.getElementById('uploadConfigInput');
if (uploadInput && !uploadInput._handlerSet) {
    uploadInput.addEventListener('change', function (e) {
        const file = e.target.files && e.target.files[0];
        if (!file) return;
        const reader = new FileReader();
        reader.onload = function (evt) {
            try {
                const json = JSON.parse(evt.target.result);
                // POST the uploaded config to /api/config
                fetch(BASE_URL + '/api/config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(json)
                })
                .then(async response => {
                    if (!response.ok) throw new Error('Upload failed');
                    const text = await response.text();
                    try { JSON.parse(text); } catch {}
                })
                .then(() => {
                    // Feedback: show success on upload button label
                    const label = document.querySelector('label[for="uploadConfigInput"]');
                    if (label) {
                        const original = label.textContent;
                        label.textContent = '✓ Uploaded!';
                        label.style.backgroundColor = '#4CAF50';
                        setTimeout(() => {
                            label.textContent = original;
                            label.style.backgroundColor = '';
                        }, 2000);
                    }
                    loadConfig();
                })
                .catch(err => {
                    const label = document.querySelector('label[for="uploadConfigInput"]');
                    if (label) {
                        const original = label.textContent;
                        label.textContent = '✗ Error';
                        label.style.backgroundColor = '#f44336';
                        setTimeout(() => {
                            label.textContent = original;
                            label.style.backgroundColor = '';
                        }, 2000);
                    }
                    console.error('Error uploading config:', err);
                });
            } catch (err) {
                alert('Invalid config file: ' + err.message);
            }
        };
        reader.readAsText(file);
        // Reset input so same file can be uploaded again if needed
        uploadInput.value = '';
    });
    uploadInput._handlerSet = true;
}

// --- WebSocket OTA Status & Toast ---
function showToast(message, type = 'info', duration = 4000) {
    let toast = document.getElementById('toast');
    if (!toast) {
        toast = document.createElement('div');
        toast.id = 'toast';
        toast.className = 'toast';
        document.body.appendChild(toast);
    }
    toast.textContent = message;
    toast.className = 'toast show ' + type;
    setTimeout(() => {
        toast.className = 'toast';
    }, duration);
}

function addOtaStatusHandlerToWs(ws) {
    if (!ws) return;
    ws.addEventListener('message', (event) => {
        console.debug('[WS] Message:', event.data);
        try {
            const msg = JSON.parse(event.data);
            if (msg.type === 'ota_status') {
                const updateBtn = document.getElementById('updateButton');
                const updateSpinner = document.getElementById('updateSpinner');
                if (msg.status === 'success') {
                    showToast('OTA update successful! Device will reboot.', 'success');
                    if (updateSpinner) updateSpinner.style.display = 'none';
                    if (updateBtn) updateBtn.disabled = false;
                    if (updateBtn) updateBtn.childNodes[0].textContent = 'Check for Updates ';
                } else if (msg.status === 'error') {
                    const errMsg = msg.message || msg.error || 'Unknown error';
                    showToast('OTA update failed: ' + errMsg, 'error');
                    if (updateSpinner) updateSpinner.style.display = 'none';
                    if (updateBtn) updateBtn.disabled = false;
                    if (updateBtn) updateBtn.childNodes[0].textContent = 'Check for Updates ';
                }
            }
        } catch (e) {
            // Not JSON, ignore
        }
    });
}

window.addEventListener('DOMContentLoaded', () => {
    // Use global ws from app.js if available, otherwise create it
    let ws = window.ws;
    if (!ws) {
        let wsUrl;
        if (location.protocol === 'file:' || location.hostname === 'localhost' || location.hostname === '127.0.0.1') {
            // Use BASE_URL from app.js
            wsUrl = BASE_URL.replace(/^http/, 'ws') + '/ws';
        } else {
            wsUrl = (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws';
        }
        ws = new WebSocket(wsUrl);
        window.ws = ws;
    }
    addOtaStatusHandlerToWs(ws);
});
