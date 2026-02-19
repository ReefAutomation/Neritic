---
name: firmware-development
description: A protocol for the agent to follow when modifying the C++ firmware. It covers analyzing the request, making code changes, and ensuring API consistency with the web UI.
license: MIT
metadata:
  author: GitHub Copilot
  version: "2.0"
---


## Agentic Firmware Development Protocol (ESP-IDF)

As an AI assistant, your task is to modify the C++ firmware in the `src/` directory using the ESP-IDF framework. Follow this protocol to ensure changes are robust, compatible, and maintainable.

### Step 1: Analyze the Request & UI Contract

1. **Clarify the Goal:**
    - What feature, fix, or refactor is required?
    - Does it affect APIs, configuration, or hardware?
2. **Check the Web UI Contract:**
    - If the change impacts the web UI, review the relevant React components in `web/`.
    - Identify WebSocket/HTTP API messages and expected data structures.
    - If breaking changes are needed, plan coordinated updates for both firmware and UI.

### Step 2: Plan and Apply ESP-IDF Code Changes

1. **Identify Relevant Files:**
    - For new effects: `effects.h`, `effects.cpp`
    - For persistent settings: `config.h`, `config.cpp`
    - For runtime state: `state.h`, `state.cpp`
    - For networking: `network.h`, `network.cpp`
    - For web/API: `webserver.cpp`, `webserver.h`
    - For OTA: `ota.cpp`, `ota.h`
2. **Use ESP-IDF APIs:**
    - Replace all Arduino APIs with ESP-IDF equivalents (e.g., `esp_wifi`, `esp_littlefs`, `esp_log`, FreeRTOS, etc).
    - Use event-driven and task-based patterns as required by ESP-IDF.
    - Update CMakeLists.txt and sdkconfig if new components or features are added.
3. **Follow Coding Standards:**
    - Use ESP-IDF logging (`ESP_LOGI`, `ESP_LOGE`, etc).
    - Mind memory and concurrency constraints.

### Step 3: Build, Flash, and Monitor

1. **Build the Firmware:**
    - Run: `idf.py build`
2. **Flash to Device:**
    - Connect the device via USB.
    - Run: `idf.py -p <PORT> flash` (replace `<PORT>` with your serial port, e.g., `/dev/tty.usbserial-xxxx`)
3. **Monitor Serial Output:**
    - Run: `idf.py -p <PORT> monitor`
4. **Fix Build Errors:**
    - If errors occur, review and correct code or configuration.

### Step 4: Summarize and Advise

1. **Summarize Changes:**
    - Briefly describe what was changed in the firmware and why.
2. **Next Steps:**
    - Advise the user to test the firmware on hardware.
    - If the web UI was affected, summarize required UI changes.

---
**Note:**
- Always use ESP-IDF APIs and patterns. Do not use Arduino APIs.
- Keep CMakeLists.txt and sdkconfig in sync with code changes.
- If adding new features, update documentation as needed.
