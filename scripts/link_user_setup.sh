#!/bin/bash
# PlatformIO pre-build script: Generate User_Setup.h
# Replaces scripts/link_user_setup.py

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DISPLAY_H="${PROJECT_ROOT}/src/display.h"
TFT_LIB_DEPS_DIR=".pio/libdeps/PIOENV/TFT_eSPI"
USER_SETUP_DST="${TFT_LIB_DEPS_DIR}/User_Setup.h"

echo "[link_user_setup] Parsing display.h..."

if [ ! -f "$DISPLAY_H" ]; then
    echo "[link_user_setup] ERROR: display.h not found at ${DISPLAY_H}"
    exit 1
fi

# Extract values from display.h using grep and sed
TFT_WIDTH=$(grep '#define TFT_WIDTH' "$DISPLAY_H" | sed 's/.*"#define[[:space:]]*TFT_WIDTH[[:space:]]*\([0-9]*\).*/\1/' || echo "80")
TFT_HEIGHT=$(grep '#define TFT_HEIGHT' "$DISPLAY_H" | sed 's/.*"#define[[:space:]]*TFT_HEIGHT[[:space:]]*\([0-9]*\).*/\1/' || echo "160")
TFT_SCL=$(grep '#define TFT_SCL' "$DISPLAY_H" | sed 's/.*"#define[[:space:]]*TFT_SCL[[:space:]]*\([0-9]*\).*/\1/' || echo "14")
TFT_SDA=$(grep '#define TFT_SDA' "$DISPLAY_H" | sed 's/.*"#define[[:space:]]*TFT_SDA[[:space:]]*\([0-9]*\).*/\1/' || echo "12")
TFT_RES=$(grep '#define TFT_RES' "$DISPLAY_H" | sed 's/.*"#define[[:space:]]*TFT_RES[[:space:]]*\([0-9]*\).*/\1/' || echo "27")
TFT_DC=$(grep '#define TFT_DC' "$DISPLAY_H" | sed 's/.*"#define[[:space:]]*TFT_DC[[:space:]]*\([0-9]*\).*/\1/' || echo "26")
TFT_CS=$(grep '#define TFT_CS' "$DISPLAY_H" | sed 's/.*"#define[[:space:]]*TFT_CS[[:space:]]*\([0-9]*\).*/\1/' || echo "25")
TFT_BLK=$(grep '#define TFT_BL' "$DISPLAY_H" | sed 's/.*"#define[[:space:]]*TFT_BL[[:space:]]*\([0-9]*\).*/\1/' || echo "33")

# Parse driver from comment or define
TFT_DRIVER=$(grep '#define ST7735_DRIVER' "$DISPLAY_H" && echo "ST7735S" || echo "ILI9341_A")

cat > "${USER_SETUP_DST}" << EOF
// TEST UNIQUE HEADER: DeepGlow PlatformIO overwrite check
#define ST7735_DRIVER
#define RGB_TFT
#define TFT_WIDTH ${TFT_WIDTH}
#define TFT_HEIGHT ${TFT_HEIGHT}
#define TFT_MOSI ${TFT_SDA}   // SDA
#define TFT_SCLK ${TFT_SCL}   // SCL
#define TFT_CS   ${TFT_CS}
#define TFT_DC   ${TFT_DC}
#define TFT_RST  ${TFT_RES}   // RES
#define TFT_BL   ${TFT_BLK}   // BLK
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SPI_FREQUENCY  40000000
#define TFT_RGB_ORDER TFT_RGB
#define ST7735_GREENTAB160x80
EOF

echo "[link_user_setup] Generated ${USER_SETUP_DST}"