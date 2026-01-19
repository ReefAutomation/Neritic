// config.js
// Handles configuration page logic for config.html

// Ensure global config variable
window.config = window.config || {};

// Load timezones from API and populate combo
let TIMEZONES = [];
    let _loaded = { timezones: false, presets: false, config: false };

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
            const minTransSeconds = Math.floor(Number(window.config.safety.minTransitionTime) / 1000);
            document.getElementById('minTransition').value = minTransSeconds;
            document.getElementById('minTransitionValue').textContent = minTransSeconds;
        }
    }
    // Transition Times
    if (window.config.transitionTimes) {
        document.getElementById('ttPowerOn').value = Math.floor((window.config.transitionTimes.powerOn || 0) / 1000);
        document.getElementById('ttSchedule').value = Math.floor((window.config.transitionTimes.schedule || 0) / 1000);
        document.getElementById('ttManual').value = Math.floor((window.config.transitionTimes.manual || 0) / 1000);
        document.getElementById('ttEffect').value = Math.floor((window.config.transitionTimes.effect || 0) / 1000);
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
        // --- GPS Button Handler ---
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
                        }
                    }, false);
                    window._gpsListenerSet = true;
                }
                window.open('https://locate.wled.me', '_blank');
            };
            gpsBtn._handlerSet = true;
        }
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
    minTransitionInput.addEventListener('input', e => {
        document.getElementById('minTransitionValue').textContent = e.target.value;
    });
}


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
        const minTransitionTime = Math.max(2, parseInt(document.getElementById('minTransition').value)) * 1000;
        if (!orig.safety || minTransitionTime !== orig.safety.minTransitionTime) safetyUpdate.minTransitionTime = minTransitionTime;
        if (Object.keys(safetyUpdate).length > 0) update.safety = safetyUpdate;
    }
    // Transition Times
    if (window.config.transitionTimes) {
        const ttUpdate = {};
        const powerOn = parseInt(document.getElementById('ttPowerOn').value) * 1000;
        const schedule = parseInt(document.getElementById('ttSchedule').value) * 1000;
        const manual = parseInt(document.getElementById('ttManual').value) * 1000;
        const effect = parseInt(document.getElementById('ttEffect').value) * 1000;
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
        const statusSpan = document.getElementById('systemStatus');
        if (statusSpan) statusSpan.textContent = '';
        try {
            const result = await sendCommandWithStatus('reboot');
            if (result && result.success) {
                if (statusSpan) statusSpan.textContent = 'Rebooting device...';
                rebootBtn.textContent = 'Rebooting...';
                setTimeout(() => {
                    rebootBtn.textContent = 'Reboot Device';
                    rebootBtn.disabled = false;
                    if (statusSpan) statusSpan.textContent = '';
                }, 8000);
            } else {
                if (statusSpan) statusSpan.textContent = 'Reboot failed!';
                rebootBtn.textContent = 'Reboot Device';
                rebootBtn.disabled = false;
            }
        } catch (e) {
            if (statusSpan) statusSpan.textContent = 'Reboot error!';
            rebootBtn.textContent = 'Reboot Device';
            rebootBtn.disabled = false;
        }
    };
    rebootBtn._handlerSet = true;
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
