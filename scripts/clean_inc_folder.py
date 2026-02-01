


import shutil
import os
import logging
from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()
PROJECT_DIR = env['PROJECT_DIR']
INC_DIR = os.path.join(PROJECT_DIR, 'src', 'inc')

# Setup logging
logging.basicConfig(level=logging.INFO, format='[clean_inc_folder] %(message)s')

def clean_inc_folder():
    if os.path.exists(INC_DIR):
        try:
            shutil.rmtree(INC_DIR)
            logging.info(f"Deleted: {INC_DIR}")
        except Exception as e:
            logging.error(f"Failed to delete {INC_DIR}: {e}")
    else:
        logging.info(f"Not found: {INC_DIR}")

if env.IsCleanTarget():
    clean_inc_folder()
