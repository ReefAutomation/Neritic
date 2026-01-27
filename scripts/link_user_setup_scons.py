#!/usr/bin/env python3
# PlatformIO SCons extra_script for User_Setup.h generation
import os
import logging
from SCons.Script import Import
Import("env")
logging.basicConfig(level=logging.INFO, format='[link_user_setup_scons] %(message)s')

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
    logging.info(f"Overwrote {path} with template header")

# SCons hook

def pre_build_action(source, target, env):
    project_root = env['PROJECT_DIR']
    display_h_path = os.path.join(project_root, 'src', 'display.h')
    vals = parse_display_h(display_h_path)
    libdeps_dir = os.path.join(project_root, '.pio', 'libdeps')
    with open(os.path.join(project_root, 'link_user_setup_scons.log'), 'a') as logf:
        logf.write('link_user_setup_scons.py executed\n')
    if not os.path.exists(libdeps_dir):
        logging.info(f"libdeps directory not found: {libdeps_dir}")
        return
    for env_name in os.listdir(libdeps_dir):
        tft_dir = os.path.join(libdeps_dir, env_name, 'TFT_eSPI')
        if os.path.isdir(tft_dir):
            user_setup_dst = os.path.join(tft_dir, 'User_Setup.h')
            try:
                generate_user_setup_h(user_setup_dst, vals)
                logging.info(f"Generated {user_setup_dst}")
            except Exception as e:
                logging.info(f"Failed to generate {user_setup_dst}: {e}")

env.AddPreAction("buildprog", pre_build_action)
