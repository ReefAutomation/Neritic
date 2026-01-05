# Troubleshooting Checklist

Quick reference for diagnosing and fixing common issues.

## 🔍 Quick Diagnostics

### Check List Order:
1. Power supply ✓
2. Wiring connections ✓
3. Serial monitor output ✓
4. Network connectivity ✓
5. Filesystem status ✓
6. LED hardware ✓

---

## 💡 LEDs Not Working

### Symptoms: LEDs completely dark or not responding

**Check Power:**
- [ ] Power supply plugged in and switched ON
- [ ] Voltage at LED strip measures 4.8-5.2V
- [ ] Power supply adequate for LED count (60mA per LED)
- [ ] Check barrel connector/screw terminals secure

**Check Wiring:**
- [ ] ESP GPIO2 connected to LED strip DIN (not DOUT)
- [ ] ALL grounds connected together (ESP + LEDs + power)
- [ ] 470Ω resistor between ESP and LED data line
- [ ] No reversed polarity (+/- swapped)
- [ ] Data arrow on strip points away from ESP

**Check Configuration:**
- [ ] LED count correct in web interface
- [ ] GPIO pin set to 2 (default)
- [ ] LED type matches strip (WS2812B default)
- [ ] Color order correct (GRB for WS2812B)

**Try:**
```cpp
// Test with minimal LEDs
#define DEFAULT_LED_COUNT 5
```

**Serial Monitor Should Show:**
```
LEDs initialized: 60 pixels
```

---

## 🌐 Can't Access Web Interface

### Symptoms: Browser can't load webpage

**Check AP Mode (First Boot):**
- [ ] Look for WiFi network "AquariumLED"
- [ ] Password: aquarium123
- [ ] Browse to: http://192.168.4.1
- [ ] Device within WiFi range

**Check Station Mode:**
- [ ] Device connected to your WiFi
- [ ] Check serial monitor for IP address
- [ ] Try: http://aquariumled.local
- [ ] Try: http://[ip-address]
- [ ] Ping device from terminal

**Check Filesystem:**
- [ ] Filesystem uploaded: `pio run -t uploadfs`
- [ ] Serial shows "Filesystem mounted"
- [ ] Re-upload if needed

**Try:**
```bash
# Re-upload filesystem
pio run -t uploadfs -e esp8266

# Check upload log for errors
```

**Serial Monitor Should Show:**
```
Filesystem mounted
Web server started
IP Address: 192.168.1.xxx
```

---

## 📡 WiFi Connection Issues

### Symptoms: Won't connect to WiFi network

**Check Credentials:**
- [ ] SSID spelled correctly (case-sensitive)
- [ ] Password correct (case-sensitive)
- [ ] Network is 2.4GHz (NOT 5GHz)
- [ ] Router not hidden SSID

**Check Router:**
- [ ] MAC filtering disabled or device allowed
- [ ] DHCP enabled
- [ ] Not too many connected devices
- [ ] Router within range

**Check Serial Monitor:**
```
Connecting to WiFi....
```

**If "Connection failed":**
- Try AP mode to reconfigure
- Check router settings
- Try different WiFi network
- Update WiFi library

**Force AP Mode:**
```cpp
// In main.cpp, temporarily:
// WiFi.begin(config.network.ssid.c_str(), config.network.password.c_str());
WiFi.mode(WIFI_AP);
WiFi.softAP("AquariumLED", "aquarium123");
```

---

## ⚡ LEDs Flickering

### Symptoms: Random glitches, flickering, or wrong colors

**Check Power:**
- [ ] Power supply adequate (calculate: LEDs × 60mA × 1.5)
- [ ] Add 1000µF capacitor across power supply
- [ ] Use thicker wire gauge for long runs
- [ ] Power injection every 2-3 meters

**Check Grounds:**
- [ ] ESP ground to LED ground
- [ ] LED ground to power supply ground
- [ ] All grounds at same potential
- [ ] No loose connections

**Check Data Line:**
- [ ] Add 470Ω resistor if missing
- [ ] Use twisted pair for data + ground
- [ ] Keep data wire short (<3 feet ideal)
- [ ] Use level shifter (3.3V → 5V)
- [ ] Check for EMI sources nearby

**Check Settings:**
- [ ] Reduce brightness temporarily
- [ ] Try slower effects
- [ ] Check LED type/color order
- [ ] Update FastLED library

**Try:**
```cpp
// In main.cpp:
FastLED.setMaxPowerInVoltsAndMilliamps(5, 1500); // Limit power
```

---

## 🎨 Wrong Colors

### Symptoms: Colors don't match or look incorrect

**Check LED Type:**
- [ ] WS2812B vs SK6812 (different chipsets)
- [ ] RGB vs RGBW (4-channel)
- [ ] Check datasheet

**Check Color Order:**
Try different orders in config:
- [ ] GRB (WS2812B default)
- [ ] RGB
- [ ] BGR
- [ ] BRG

**Check Power:**
- [ ] Voltage at strip (should be 5V)
- [ ] Voltage drop on long strips
- [ ] Brown-out causing color shift

**Check First LED:**
- [ ] First LED often wrong color (expected)
- [ ] Add 470Ω resistor to fix
- [ ] Use level shifter

**Serial Monitor:**
```
Check LED type: WS2812B
Color order: GRB
```

---

## ⏰ Time Not Syncing

### Symptoms: Clock stuck at 00:00:00 or wrong time

**Check Internet:**
- [ ] Device connected to WiFi with internet
- [ ] Router has internet connection
- [ ] Ping 8.8.8.8 works
- [ ] No firewall blocking NTP (port 123)

**Check NTP Server:**
- [ ] Default: pool.ntp.org
- [ ] Try: time.google.com
- [ ] Try: time.nist.gov
- [ ] Check geographic NTP pools

**Check Timezone:**
- [ ] Timezone offset correct (hours from UTC)
- [ ] Example: EST = -5, PST = -8
- [ ] DST setting if applicable

**Serial Monitor Should Show:**
```
Waiting for time sync...
Time synchronized!
```

**Try:**
```json
{
  "time": {
    "ntpServer": "time.google.com",
    "timezoneOffset": -5
  }
}
```

---

## 📅 Timers Not Triggering

### Symptoms: Scheduled presets don't activate

**Check Time Sync:**
- [ ] Current time showing correctly
- [ ] Wait 5 minutes after boot for NTP sync
- [ ] Check timezone offset

**Check Timer Config:**
- [ ] Timer enabled (checkbox)
- [ ] Correct hour/minute
- [ ] Days of week selected (127 = all days)
- [ ] Preset ID valid (0-15)

**Check Timer Type:**
- [ ] Regular: Specific time
- [ ] Sunrise: Needs location coordinates
- [ ] Sunset: Needs location coordinates

**Check Location (Sun Timers):**
- [ ] Latitude/longitude set
- [ ] Reasonable values (-90 to 90, -180 to 180)
- [ ] Sunrise/sunset times make sense

**Serial Monitor Should Show:**
```
Timer triggered: 2
Applying preset: 3
```

**Debug:**
```cpp
// Check scheduler in main.cpp
debugPrint("Current time: ");
debugPrintln(scheduler.getCurrentTime());
```

---

## 🔥 Device Overheating

### Symptoms: Device hot to touch, resets, or unstable

**Check Power:**
- [ ] Not overloading with too many LEDs
- [ ] Power supply not overheating
- [ ] Voltage regulator on ESP not too hot
- [ ] Adequate ventilation

**Check Enclosure:**
- [ ] Not in direct sunlight
- [ ] Ventilation holes present
- [ ] Heat sink if needed
- [ ] Away from heat sources

**Reduce Load:**
- [ ] Lower brightness
- [ ] Fewer LEDs
- [ ] Slower effects
- [ ] Disable WiFi when not needed

**Add Cooling:**
- [ ] Small heatsink on ESP
- [ ] Fan for airflow
- [ ] Better thermal management

---

## 💾 Filesystem Errors

### Symptoms: "Failed to mount" or corrupted files

**Fix Filesystem:**
```bash
# Erase flash
pio run -t erase

# Re-upload firmware
pio run -t upload -e esp8266

# Re-upload filesystem
pio run -t uploadfs -e esp8266
```

**Check Flash Size:**
- [ ] Board has adequate flash (2MB minimum)
- [ ] Partition table correct in platformio.ini
- [ ] SPIFFS/LittleFS properly configured

**Prevent Corruption:**
- [ ] Don't unplug during writes
- [ ] Stable power supply
- [ ] Use SPIFFS on ESP32, LittleFS on ESP8266

---

## 🔄 Boot Loop / Crashes

### Symptoms: Device constantly resetting

**Check Serial Monitor:**
Look for:
- Exception/crash dumps
- Stack traces
- Last function called
- Memory errors

**Common Causes:**
- [ ] Insufficient power
- [ ] Memory overflow
- [ ] Stack overflow
- [ ] Watchdog timeout

**Try:**
```cpp
// Reduce LED count
#define DEFAULT_LED_COUNT 30

// Disable features temporarily
// Comment out scheduler updates
```

**Reset to Defaults:**
```bash
# Complete erase and reflash
pio run -t erase
pio run -t upload
pio run -t uploadfs
```

---

## 🐌 Slow Performance

### Symptoms: Laggy web interface or low FPS

**Check Network:**
- [ ] WiFi signal strength
- [ ] Router bandwidth
- [ ] Multiple clients connected
- [ ] WebSocket working

**Check CPU Load:**
- [ ] Too many LEDs for ESP8266
- [ ] Complex effects
- [ ] Reduce frame rate
- [ ] Optimize code

**Check Memory:**
- [ ] Not running out of RAM
- [ ] Too many presets
- [ ] Large JSON documents
- [ ] Memory leaks

**Optimize:**
```cpp
// Reduce FPS
#define FRAMES_PER_SECOND 30  // Instead of 60

// Fewer WebSocket updates
// In webserver.cpp: increase interval
```

---

## 🔧 Useful Commands

### PlatformIO
```bash
# Clean build
pio run -t clean

# Verbose upload
pio run -t upload -v

# Monitor serial
pio device monitor

# List serial ports
pio device list

# Update libraries
pio lib update

# Check for errors
pio check
```

### Serial Monitor
```bash
# Linux/Mac
screen /dev/ttyUSB0 115200

# Windows
putty -serial COM3 -serspeed 115200
```

### Network
```bash
# Find device IP
nmap -sn 192.168.1.0/24

# Ping device
ping aquariumled.local

# Check port 80 open
telnet aquariumled.local 80
```

---

## 📊 Diagnostic Checklist

Print this and check off during troubleshooting:

**Power:**
- [ ] LED strip powered (5V measured)
- [ ] ESP powered (USB or 5V)
- [ ] Current adequate for LED count
- [ ] No voltage sag under load

**Wiring:**
- [ ] Data: GPIO2 → resistor → LED DIN
- [ ] Ground: All connected together
- [ ] Power: External 5V to LED strip
- [ ] Level shifter (optional but recommended)

**Software:**
- [ ] Firmware uploaded successfully
- [ ] Filesystem uploaded successfully
- [ ] Serial monitor shows "System ready"
- [ ] No error messages

**Network:**
- [ ] WiFi connected or AP mode active
- [ ] IP address obtained
- [ ] Web interface accessible
- [ ] WebSocket connected

**Configuration:**
- [ ] LED count correct
- [ ] LED type correct
- [ ] Safety settings reasonable
- [ ] Timezone configured
- [ ] Location set (for sun timers)

**Testing:**
- [ ] Power toggle works
- [ ] Brightness changes
- [ ] Effects work
- [ ] Presets apply
- [ ] Transitions smooth

---

## 🆘 Still Having Issues?

### Before Asking for Help:

1. **Check serial monitor** - Copy full output
2. **Try different LED count** - Test with 5-10 LEDs
3. **Test on breadboard** - Eliminate wiring issues
4. **Try minimal config** - Default settings
5. **Update libraries** - Latest versions
6. **Check forums** - Someone may have same issue

### When Posting for Help:

Include:
- ESP8266 or ESP32?
- LED type and count
- Power supply specs
- Wiring diagram/photo
- Serial monitor output
- PlatformIO.ini contents
- What you've tried

### Support Channels:

- GitHub Issues: Bug reports
- GitHub Discussions: Questions
- Serial output: Always helpful!

---

**Remember**: 90% of issues are wiring or power supply related! 🔌

Double-check connections before assuming software issues.
