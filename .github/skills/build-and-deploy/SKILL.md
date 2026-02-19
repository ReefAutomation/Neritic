---
name: build-and-deploy
description: Instructions for building, testing, and deploying the firmware and web interface for an IoT LED project. Use this skill to automate common development tasks.
license: MIT
metadata:
  author: GitHub Copilot
  version: "1.0"
---

## Build and Deployment Automation

This skill outlines the common tasks for building and deploying the project.

### Python Environment Prerequisite

*   In this repository, `platformio` is installed in `.venv`.
*   Prefix firmware and Python-script commands with:
  ```bash
  source .venv/bin/activate && <command>
  ```

### Building the Firmware

*   **Command:** `source .venv/bin/activate && platformio run -e <environment_name>`
*   **Supported ESP-IDF environments:** `esp32d`, `esp32c6`, `esp32`, `esp32c3`, `esp32s3` and their `_debug` variants.
*   **Example:** To build for the ESP32-C6 debug environment, run:
  ```bash
  source .venv/bin/activate && platformio run -e esp32c6_debug
  ```
*   **Example:** To build for the ESP32-C3 debug environment, run:
  ```bash
  source .venv/bin/activate && platformio run -e esp32c3_debug
  ```
*   Always activate the Python virtual environment before building.
*   You can also use the board-specific build tasks available in the workspace.

### Building the Web Interface

*   **Command:** `npm run build`
*   **Action:** This command, defined in `package.json`, uses Vite to compile the React application into static assets located in the `web/dist` directory.

### Embedding Web Assets into Firmware

*   **Action:** After building the web interface, the static files must be converted into C++ headers to be included in the firmware.
*   **Command:** `source .venv/bin/activate && python scripts/embed_assets.py`
*   **Full Workflow:**
    1.  `npm run build` (in the `web` directory)
  2.  `source .venv/bin/activate && python scripts/embed_assets.py` (from the project root)

### Cleaning the Build Environment

*   **Command:** `source .venv/bin/activate && platformio run --target clean`
*   **Action:** Removes compiled object files and firmware binaries, which is useful for forcing a complete rebuild.

### Uploading Firmware to Device

*   **Command:** `source .venv/bin/activate && platformio run -e <environment_name> --target upload`
*   **Action:** Builds the firmware and uploads it to the connected device.
