# Quick Start Guide

Get your aquarium LED controller running in 15 minutes! 🚀

## What You Need

- ESP8266 or ESP32 board
- WS2812B LED strip (30-60 LEDs for testing)
- 5V power supply (2-3A for 60 LEDs)
- USB cable (for programming)
- Computer with VS Code or PlatformIO CLI

---

## Step 1: Install Software (5 minutes)

### Option A: VS Code (Recommended)

1. **Download VS Code**: https://code.visualstudio.com/
2. **Install PlatformIO Extension**:
   - Open VS Code
   - Click Extensions (or Ctrl+Shift+X)
   - Search "PlatformIO IDE"
   - Click Install
   - Restart VS Code

### Option B: Command Line

```bash
# Install Python (if not installed)
# Then install PlatformIO
pip install platformio
```

---

## Step 2: Get the Code (2 minutes)

### Download Project

```bash
# Clone the repository
git clone https://github.com/yourusername/DeepGlow.git
cd DeepGlow
```

Or download ZIP from GitHub and extract.

### Open in VS Code

1. Open VS Code
2. File → Open Folder
3. Select DeepGlow folder
4. Wait for PlatformIO to initialize

---

## Step 3: Configure for Your Board (1 minute)

Edit `platformio.ini` default environment:

**For ESP8266**:
```ini
[platformio]
default_envs = esp8266
```

**For ESP32**:
```ini
[platformio]
default_envs = esp32
```

**For Athom Controller**:
```ini
[platformio]
default_envs = athom
```

---

## Step 4: Upload Filesystem (3 minutes)

The web interface needs to be uploaded to the board's filesystem.

### VS Code Method:

1. Click PlatformIO icon (alien head) on left sidebar
2. Expand your environment (esp8266/esp32)
3. Click "Upload Filesystem Image"
4. Wait for completion (~2-3 minutes)

### Command Line Method:

```bash
pio run -t uploadfs -e esp8266
```

---

## Step 5: Upload Firmware (2 minutes)

### VS Code Method:

1. Connect board via USB
2. Click PlatformIO icon
3. Click "Upload" under your environment
4. Wait for upload

### Command Line Method:

```bash
pio run -t upload -e esp8266
```

---

## Step 6: Connect Hardware (2 minutes)

**Minimal Setup** (for testing):

```
ESP GPIO2 ──[470Ω]── LED Strip DIN
ESP GND ────────────── LED Strip GND
5V Supply +5V ──────── LED Strip +5V
5V Supply GND ─────┬── LED Strip GND
                   └── ESP GND
```

**Important**:
- Connect ALL grounds together
- Power LEDs from external 5V supply (not ESP)
- Add 470Ω resistor between GPIO2 and LED data

See [WIRING.md](WIRING.md) for detailed diagrams.

---

## Step 7: First Boot

### Monitor Serial Output:

**VS Code**: Click "Monitor" in PlatformIO tasks

**Command Line**:
```bash
pio device monitor
```

**You should see**:
```
=================================
  Aquarium LED Controller v1.0
=================================
Filesystem mounted
Creating default configuration
LEDs initialized: 60 pixels
Starting Access Point mode
AP IP: 192.168.4.1
System ready!
```

---

## Step 8: Connect to Web Interface

### Method 1: Access Point Mode (First Time)

1. **Look for WiFi network**: "AquariumLED"
2. **Password**: `aquarium123`
3. **Open browser** to: `http://192.168.4.1`
4. **You should see**: Aquarium Control interface!

### Method 2: Configure WiFi

1. In web interface, scroll to **Configuration**
2. Enter your WiFi SSID and password
3. Save configuration
4. Reboot device
5. Check serial monitor for IP address
6. Connect to device at `http://[ip-address]`

---

## Step 9: Test Your Setup ✨

### Quick Test:

1. **Power Toggle**: Turn on/off - LEDs should fade smoothly
2. **Brightness**: Move slider - should change gradually
3. **Effect Selector**: Try "Aquarium Ripple" - should see water effect
4. **Presets**: Click "Morning Sun" - should apply preset

### If It Works:
🎉 **Congratulations!** Your aquarium controller is running!

### If Not Working:
See Troubleshooting section below.

---

## Next Steps

### 1. Configure Your Location (for Sunrise/Sunset)

1. Find your coordinates: https://www.latlong.net/
2. Go to Configuration section
3. Enter:
   - Timezone Offset (hours from UTC)
   - Latitude
   - Longitude
4. Save

### 2. Create Schedule

1. Go to Schedule section
2. Click "Add Timer"
3. Set times for:
   - Morning (8:00 AM) → Morning Sun preset
   - Afternoon (12:00 PM) → Daylight preset
   - Evening (6:00 PM) → Evening Glow preset
   - Night (10:00 PM) → Moonlight preset

### 3. Adjust Safety Settings

**For Fish Safety**:
- Max Brightness: 180-200 (70-78%)
- Min Transition: 5-10 seconds

**For Plants**:
- Max Brightness: 200-255 (78-100%)
- Consider photoperiod (8-10 hours)

### 4. Customize Presets

1. Click on a preset
2. Adjust:
   - Effect type
   - Colors
   - Speed/Intensity
   - Brightness
3. Save changes

---

## Troubleshooting

### LEDs Don't Light Up

**Check**:
- [ ] Power supply connected and ON
- [ ] All grounds connected together
- [ ] Correct GPIO pin (GPIO2)
- [ ] LED strip data direction (arrow points away from ESP)
- [ ] Power supply voltage (should be 5V)

**Try**:
```cpp
// In main.cpp, temporarily change LED count for testing
#define DEFAULT_LED_COUNT 10  // Test with just 10 LEDs
```

### Can't Connect to WiFi AP

**Check**:
- [ ] AP name: "AquariumLED"
- [ ] Password: "aquarium123"
- [ ] Device powered on
- [ ] Within range

**Try**:
- Restart device
- Check serial monitor for errors
- Try from different device (phone, laptop)

### Web Interface Won't Load

**Check**:
- [ ] Filesystem uploaded successfully
- [ ] IP address correct
- [ ] Device on same network (if using WiFi mode)

**Try**:
- Re-upload filesystem: `pio run -t uploadfs`
- Check serial monitor for IP address
- Try `http://192.168.4.1` in AP mode

### LEDs Flicker or Wrong Colors

**Fix**:
- Add 470Ω resistor on data line
- Connect grounds properly
- Use level shifter (3.3V → 5V)
- Check LED type in config
- Reduce brightness

### Serial Monitor Shows Errors

**Common Fixes**:
- "Failed to mount filesystem" → Upload filesystem
- "WiFi connection failed" → Check SSID/password
- "NTP sync failed" → Check internet connection

---

## Configuration Files

### Default WiFi Setup

After first boot, edit configuration via web interface or create `config.json`:

```json
{
  "network": {
    "ssid": "YourWiFiName",
    "password": "YourPassword",
    "hostname": "AquariumLED"
  },
  "led": {
    "count": 60,
    "pin": 2,
    "type": "WS2812B"
  },
  "safety": {
    "maxBrightness": 200,
    "minTransitionTime": 5000
  }
}
```

---

## Getting Help

### Resources

- 📖 **Full Documentation**: [README.md](../README.md)
- 🔌 **Wiring Guide**: [docs/WIRING.md](WIRING.md)
- 🔧 **API Documentation**: [docs/API.md](API.md)

### Support

- 🐛 **Bug Reports**: [GitHub Issues](https://github.com/yourusername/DeepGlow/issues)
- 💬 **Questions**: [GitHub Discussions](https://github.com/yourusername/DeepGlow/discussions)
- 📧 **Email**: support@example.com

### Common Questions

**Q: Can I use different LED types?**
A: Yes! WS2812B, SK6812, APA102 supported. Configure in web interface.

**Q: How many LEDs can I control?**
A: ESP8266: ~300 LEDs, ESP32: ~512 LEDs. Limited by memory and power.

**Q: Can I use this underwater?**
A: ESP must stay dry. Use IP67+ rated LED strips for underwater use.

**Q: Will this work with my existing aquarium light?**
A: This replaces your light. Not designed to control non-LED fixtures.

**Q: How do I update firmware?**
A: Re-upload via USB or use OTA: `pio run -t upload --upload-port aquariumled.local`

---

## Success Checklist ✅

- [ ] Firmware uploaded successfully
- [ ] Filesystem uploaded successfully
- [ ] LEDs light up and respond to commands
- [ ] Web interface accessible
- [ ] WiFi connected (or AP mode working)
- [ ] Presets work correctly
- [ ] Smooth transitions enabled
- [ ] Time synchronization working
- [ ] Schedule configured
- [ ] Safety limits set appropriately

---

## What's Next?

### Week 1: Testing
- Monitor fish behavior
- Adjust brightness levels
- Fine-tune transition times
- Test all effects

### Week 2: Optimization
- Calibrate schedule
- Create custom presets
- Set up sunrise/sunset automation
- Test power loss recovery

### Week 3: Integration
- Add to home automation (optional)
- Set up monitoring
- Document your configuration
- Share results with community!

---

**Welcome to the DeepGlow community!** 🐠💙

Share your setup on GitHub Discussions and help others learn from your experience!
