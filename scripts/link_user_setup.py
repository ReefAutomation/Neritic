#!/usr/bin/env python3

import os
import sys

USER_SETUP_TEMPLATE = """
// Auto-generated for 80x160 RGB IPS display (ST7735S)
#define ST7735_DRIVER
#define TFT_WIDTH {TFT_WIDTH}
#define TFT_HEIGHT {TFT_HEIGHT}
#define TFT_MOSI {TFT_SDA}   // SDA
#define TFT_SCLK {TFT_SCL}   // SCL
#define TFT_CS   {TFT_CS}
#define TFT_DC   {TFT_DC}
#define TFT_RST  {TFT_RES}   // RES
#define TFT_BL   {TFT_BLK}   // BLK
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
"""


import re

def parse_display_h(display_h_path):
    values = {}
    with open(display_h_path, 'r') as f:
        for line in f:
            m = re.match(r'#define\s+(TFT_\w+)\s+(\d+)', line)
            if m:
                key, val = m.groups()
                values[key] = val
            m = re.match(r'#define\s+TFT_DRIVER\s+(\w+)', line)
            if m:
                values['TFT_DRIVER'] = m.group(1)
    # Map to User_Setup.h names
    return {
        'TFT_WIDTH': values.get('TFT_WIDTH', '80'),
        'TFT_HEIGHT': values.get('TFT_HEIGHT', '160'),
        'TFT_SCL': values.get('TFT_SCL', '14'),
        'TFT_SDA': values.get('TFT_SDA', '12'),
        'TFT_RES': values.get('TFT_RES', '27'),
        'TFT_DC': values.get('TFT_DC', '26'),
        'TFT_CS': values.get('TFT_CS', '25'),
        'TFT_BLK': values.get('TFT_BLK', '33'),
        'TFT_DRIVER': values.get('TFT_DRIVER', 'ST7735S'),
    }

def generate_user_setup_h(path, vals):
    content = USER_SETUP_TEMPLATE.format(
        TFT_WIDTH=vals['TFT_WIDTH'],
        TFT_HEIGHT=vals['TFT_HEIGHT'],
        TFT_SCL=vals['TFT_SCL'],
        TFT_SDA=vals['TFT_SDA'],
        TFT_RES=vals['TFT_RES'],
        TFT_DC=vals['TFT_DC'],
        TFT_CS=vals['TFT_CS'],
        TFT_BLK=vals['TFT_BLK']
    )
    with open(path, 'w') as f:
        f.write(content)
    print(f"Generated {path}")


def main():
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    display_h_path = os.path.join(project_root, 'src', 'display.h')
    vals = parse_display_h(display_h_path)
    libdeps_dir = os.path.join(project_root, '.pio', 'libdeps')
    if not os.path.exists(libdeps_dir):
        print(f"libdeps directory not found: {libdeps_dir}")
        sys.exit(0)
    for env in os.listdir(libdeps_dir):
        tft_dir = os.path.join(libdeps_dir, env, 'TFT_eSPI')
        if os.path.isdir(tft_dir):
            user_setup_dst = os.path.join(tft_dir, 'User_Setup.h')
            try:
                generate_user_setup_h(user_setup_dst, vals)
            except Exception as e:
                print(f"Failed to generate {user_setup_dst}: {e}")

if __name__ == "__main__":
    main()
