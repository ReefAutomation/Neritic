# Wiring Guide

## Safety First! ⚠️

**IMPORTANT**: Always follow electrical safety practices:
- Disconnect power before wiring
- Never work on live circuits
- Use appropriate gauge wire for current
- Ensure proper insulation
- Keep electronics away from water
- Use GFCI outlets near aquariums

---

## Basic Components

### Required
1. **ESP32 or ESP8266 board**
2. **Addressable LED strip** (WS2812B, SK6812, APA102, etc.)
3. **5V power supply** (sufficient amperage for LEDs)
4. **Jumper wires**

### Recommended
1. **Level shifter** (74HCT245 or similar)
2. **470Ω resistor** (data line protection)
3. **1000µF capacitor** (power smoothing)
4. **Breadboard** (for testing)
5. **Terminal blocks** (permanent installations)

---

## Power Requirements

### LED Power Consumption

**Per LED at full brightness (white)**:
- WS2812B: ~60mA
- SK6812: ~60mA
- APA102: ~60mA

**Calculate total power**:
```
Total Current = Number of LEDs × 60mA
Power Supply Rating = Total Current × 1.5 (safety margin)
```

**Examples**:
- 30 LEDs: 1.8A minimum, 3A recommended
- 60 LEDs: 3.6A minimum, 5A recommended
- 150 LEDs: 9A minimum, 15A recommended

### Microcontroller Power

- **ESP8266**: ~80-170mA (WiFi active)
- **ESP32**: ~120-240mA (WiFi active)

**Note**: Can be powered from 5V supply via VIN pin or USB.

---

## Wiring Diagrams

### ESP8266 (NodeMCU) - Basic Setup

```
                    ┌─────────────┐
                    │  ESP8266    │
                    │  (NodeMCU)  │
                    │             │
    ┌───────────────┤ D4 (GPIO2)  │
    │               │             │
    │           ┌───┤ GND         │
    │           │   │             │
    │           │   │ VIN         ├───────┐
    │           │   └─────────────┘       │
    │           │                         │
    │           │   ┌──────────────┐      │
    │           │   │   5V Power   │      │
    │           │   │   Supply     │      │
    │           │   │              │      │
    │           │   │ +5V      GND ├──────┼─────┐
    │           │   └──────┬───────┘      │     │
    │           │          │              │     │
    │           │          │              │     │
    └───470Ω───┴──────────┼──────────────┘     │
                           │                    │
                      ┌────▼────────────────────▼────┐
                      │   LED Strip (WS2812B)        │
                      │                              │
                      │  DIN    5V          GND      │
                      └──────────────────────────────┘
```

### ESP32 - Basic Setup

```
                    ┌─────────────┐
                    │   ESP32     │
                    │  (DevKit)   │
                    │             │
    ┌───────────────┤ GPIO2       │
    │               │             │
    │           ┌───┤ GND         │
    │           │   │             │
    │           │   │ VIN         ├───────┐
    │           │   └─────────────┘       │
    │           │                         │
    │           │   ┌──────────────┐      │
    │           │   │   5V Power   │      │
    │           │   │   Supply     │      │
    │           │   │              │      │
    │           │   │ +5V      GND ├──────┼─────┐
    │           │   └──────┬───────┘      │     │
    │           │          │              │     │
    │           │          │              │     │
    └───470Ω───┴──────────┼──────────────┘     │
                           │                    │
                      ┌────▼────────────────────▼────┐
                      │   LED Strip (WS2812B)        │
                      │                              │
                      │  DIN    5V          GND      │
                      └──────────────────────────────┘
```

### Professional Setup (with Level Shifter)

```
┌─────────────┐
│  ESP8266/   │
│   ESP32     │
│             │
│ GPIO2   ────┼─────┬─────────┐
│             │     │         │
│ 3.3V    ────┼─────┼────┐    │
│             │     │    │    │
│ GND     ────┼─────┼────┼────┼──────┐
└─────────────┘     │    │    │      │
                    │    │    │      │
           ┌────────▼────┴────▼──┐   │
           │   74HCT245 or       │   │
           │   Level Shifter     │   │
           │                     │   │
           │  LV  HV   GND   OE  │   │
           └───┬───┬────┬────┬───┘   │
               │   │    │    │       │
       ┌───────┘   │    │    └───────┤
       │       ┌───┘    │            │
       │       │        │            │
   470Ω│       │        │            │
       │       │        │            │
┌──────▼───────▼────────▼────────────▼──────┐
│                                            │
│          5V Power Supply                   │
│                                            │
│  +5V                               GND     │
└────┬────────────────────────────────┬──────┘
     │                                │
     │         1000µF                 │
     ├────────[===]───────────────────┤
     │                                │
     │                                │
┌────▼────────────────────────────────▼──────┐
│        LED Strip (WS2812B)                 │
│                                            │
│  DIN            +5V                 GND    │
└────────────────────────────────────────────┘
```

---

## Step-by-Step Instructions

### 1. Prepare Components

**Cut LED strip to length**:
- Cut only at designated cut marks (usually between LEDs)
- Note the data direction arrow
- Solder wires if needed

**Prepare wires**:
- Red: +5V
- Black: GND
- White/Green/Yellow: Data

### 2. Connect Power Supply

**Important**: Always connect power first!

1. Connect LED strip **+5V** to power supply **+5V**
2. Connect LED strip **GND** to power supply **GND**
3. Add 1000µF capacitor across power supply (optional but recommended)
   - Long leg (+) to +5V
   - Short leg (-) to GND

### 3. Connect Data Line

**Without level shifter** (works but less reliable):
1. Connect ESP GPIO2 → 470Ω resistor → LED strip DIN
2. Connect ESP GND to LED strip GND (common ground!)

**With level shifter** (recommended):
1. Connect ESP 3.3V to level shifter LV
2. Connect power supply 5V to level shifter HV
3. Connect all GNDs together
4. Connect ESP GPIO2 to level shifter input
5. Connect level shifter output → 470Ω resistor → LED strip DIN

### 4. Power the ESP

**Option A**: Separate USB power
- Plug USB cable into ESP
- Most convenient for development

**Option B**: Power from 5V supply
- Connect +5V to ESP VIN pin
- Connect GND to ESP GND
- **Warning**: Ensure power supply is exactly 5V!

**Option C**: Buck converter
- Use buck converter (5V → 3.3V)
- More efficient for permanent installations
- Connect buck output to ESP 3.3V pin

### 5. Verify Connections

**Checklist**:
- [ ] All GNDs connected together (common ground)
- [ ] LED strip powered by adequate supply
- [ ] Data line has 470Ω resistor
- [ ] No short circuits
- [ ] Correct polarity (+/-)
- [ ] ESP powered separately or via VIN

---

## Advanced Setups

### Multiple LED Strips

**Parallel Data (Recommended)**:
```
ESP GPIO2 ──┬─── Strip 1 DIN
            ├─── Strip 2 DIN
            └─── Strip 3 DIN
```

Each strip gets own power connection. Limit: ~3 strips before signal degrades.

**Serial Data (Longer runs)**:
```
ESP GPIO2 ──► Strip 1 DOUT ──► Strip 2 DIN ──► Strip 3 DIN
```

Data passes through each strip. More reliable but adds latency.

### Long Distance Runs

**Problem**: Voltage drop over long wires

**Solutions**:
1. **Inject power every 2-3 meters**:
   ```
   +5V ────┬────────┬────────┬────── LED Strip
   GND ────┴────────┴────────┴────
   ```

2. **Use thicker wire gauge**:
   - Under 3ft: 22 AWG
   - 3-6ft: 20 AWG
   - 6-10ft: 18 AWG
   - Over 10ft: 16 AWG

3. **Boost voltage** at source to compensate

### Weatherproofing (Outdoor Use)

1. **Enclosure**: Use IP65+ rated enclosure for ESP
2. **LED strips**: IP67+ rating for outdoor/underwater
3. **Connectors**: Waterproof JST connectors
4. **Sealant**: Silicone sealant around connections
5. **Cable glands**: For wires entering enclosure

---

## Common Mistakes ❌

### 1. Forgetting Common Ground
**Problem**: LEDs flicker or don't work
**Solution**: Connect ALL grounds together (ESP, LEDs, power supply)

### 2. Insufficient Power
**Problem**: LEDs dim at high brightness or wrong colors
**Solution**: Use adequate power supply, add power injection

### 3. Wrong Data Direction
**Problem**: LEDs don't light up
**Solution**: Check data arrow on strip, connect to DIN not DOUT

### 4. No Resistor on Data Line
**Problem**: Occasional glitches or first LED wrong color
**Solution**: Add 470Ω resistor between ESP and LED DIN

### 5. Voltage Levels
**Problem**: Unreliable operation
**Solution**: Use level shifter for 3.3V → 5V logic conversion

### 6. Loose Connections
**Problem**: Intermittent failures
**Solution**: Solder connections or use quality connectors

---

## Testing

### 1. Power Test
1. Connect only power (no data)
2. LED strip should remain dark
3. Measure voltage at strip (should be ~5V)

### 2. Data Test
1. Upload firmware
2. Serial monitor should show "LEDs initialized"
3. Check web interface accessible
4. Try turning on via web UI

### 3. Full Test
1. Set brightness to 50%
2. Test all effects
3. Check for flickering
4. Verify smooth transitions
5. Leave running for 1 hour

---

## Troubleshooting

### LEDs don't light up
- [ ] Check power supply voltage (5V?)
- [ ] Verify GND connections
- [ ] Check data direction (DIN not DOUT)
- [ ] Test with known working strip
- [ ] Check GPIO pin in code matches wiring

### First LED wrong color, rest work
- [ ] Add 470Ω resistor on data line
- [ ] Use level shifter for 3.3V → 5V
- [ ] Shorten wire between ESP and strip
- [ ] Check for loose connections

### LEDs flicker
- [ ] Check all GND connections
- [ ] Add capacitor to power supply
- [ ] Reduce brightness
- [ ] Check for insufficient power
- [ ] Use power injection

### Colors wrong
- [ ] Check LED type in config (WS2812B vs SK6812)
- [ ] Try different color order (GRB vs RGB)
- [ ] Verify power supply voltage
- [ ] Check for voltage drop

### Random glitches
- [ ] Add capacitor to power
- [ ] Use twisted pair for data line
- [ ] Keep data wire short
- [ ] Use level shifter
- [ ] Check for EMI sources

---

## Tools Needed

### Essential
- Soldering iron & solder
- Wire strippers
- Multimeter
- Screwdriver set

### Helpful
- Heat shrink tubing
- Helping hands
- Flush cutters
- Wire crimpers
- Label maker

---

## Safety Checklist

- [ ] Power supply properly rated
- [ ] No exposed wiring
- [ ] Proper wire gauge used
- [ ] Connections secure (soldered or crimped)
- [ ] Heat shrink or electrical tape on connections
- [ ] Enclosure if needed
- [ ] GFCI outlet for aquarium area
- [ ] Power supply away from water
- [ ] Test with low brightness first
- [ ] Monitor temperature during extended run

---

## Need Help?

- Check troubleshooting section
- Read the FAQ
- Post photos on GitHub Discussions
- Include serial monitor output
- Describe symptoms clearly

**Remember**: Take your time, double-check connections, and prioritize safety! 🔧
