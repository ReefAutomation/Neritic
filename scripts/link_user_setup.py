#!/usr/bin/env python3
# PlatformIO SCons extra_script for User_Setup.h generation

import os
import sys
import logging
from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()

logging.basicConfig(
    level=logging.INFO,
    format='[link_user_setup] %(message)s'
)

env_name = env["PIOENV"]

USER_SETUP_TEMPLATE = """
// TEST UNIQUE HEADER: DeepGlow PlatformIO overwrite check
#define ST7735_DRIVER
#define RGB_TFT
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
    logging.info(f"Parsing display header: {display_h_path}")
    values = {}
    with open(display_h_path, 'r') as f:
        for line in f:
            m = re.match(r'#define\s+(TFT_\w+)\s+(\d+)', line)
            if m:
                key, val = m.groups()
                values[key] = val
                logging.info(f"Found {key} = {val}")
            m = re.match(r'#define\s+TFT_DRIVER\s+(\w+)', line)
            if m:
                values['TFT_DRIVER'] = m.group(1)
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



# Only run for build or upload actions, not for clean or other targets
def main():
    if env.IsCleanTarget():
        return
    project_root = os.getcwd()
    display_h_path = os.path.join(project_root, 'src', 'display.h')
    if not os.path.exists(display_h_path):
        logging.error(f"display.h not found at {display_h_path}. User_Setup.h will not be generated!")
    else:
        vals = parse_display_h(display_h_path)
        tft_dir = os.path.join(project_root, '.pio', 'libdeps', env_name, 'TFT_eSPI')
        with open(os.path.join(project_root, 'link_user_setup.log'), 'a') as logf:
            logf.write(f'link_user_setup.py executed for env: {env_name}\n')
        if not os.path.isdir(tft_dir):
            try:
                os.makedirs(tft_dir, exist_ok=True)
                logging.info(f"Created TFT_eSPI directory: {tft_dir}")
            except Exception as e:
                logging.error(f"Failed to create TFT_eSPI directory {tft_dir}: {e}")
                return
        user_setup_dst = os.path.join(tft_dir, 'User_Setup.h')
        try:
            # Always overwrite User_Setup.h to ensure it is up to date
            generate_user_setup_h(user_setup_dst, vals)
            logging.info(f"Generated {user_setup_dst}")
        except Exception as e:
            logging.error(f"Failed to generate {user_setup_dst}: {e}")
            return

main()
