---
name: web-ui-development
description: A protocol for the agent to follow when modifying the React-based web interface. It covers the full development workflow from editing components to embedding assets.
license: MIT
metadata:
  author: GitHub Copilot
  version: "2.0"
---

## Agentic Web UI Development Protocol

As an AI assistant, your task is to modify the React-based web UI located in the `web/` directory. You must follow this entire protocol to ensure changes are correctly implemented and deployed.

### Environment Prerequisite

For this repository, commands that use project Python tooling (`python`, `platformio`) must run with `.venv` activated:

```bash
source .venv/bin/activate && <command>
```

### Step 1: Understand the Request & The Backend Contract

1.  **Clarify the Goal:** What specific changes are needed for the web UI?
    *   Is it a visual change (e.g., styling, layout)?
    *   Is it a functional change (e.g., adding a new button, a new slider)?
2.  **Check the Firmware Contract:** If the change involves new functionality, you **must** first understand the backend implementation.
    *   **Read `state.h` and `config.h`:** Check if the firmware's state and configuration structs support the new feature.
    *   **Read `webserver.cpp`:** Examine the `onWsEvent` function to see what WebSocket messages the firmware expects to receive and what data it sends to the client.
    *   **If the backend does not support the feature, you must first use the `firmware-development` skill to add it.** Do not proceed with UI changes until the backend is ready.

### Step 2: Modify the React Components

1.  Identify the relevant React component(s) in the `web/` directory that need to be modified.
2.  Apply the necessary changes to the JSX, logic, and SCSS styles.
3.  Follow existing code patterns and conventions. For example, use the `send` function from `websocket.js` to communicate with the firmware.

### Step 3: Build the Web UI

1.  After modifying the files, you must rebuild the web application.
2.  Execute the following command from the project root:
    ```bash
    npm run build --prefix web
    ```
3.  This command compiles the React app and places the static assets into the `web/dist` directory. Verify the command completes successfully.

### Step 4: Embed the New Assets into the Firmware

1.  The newly built web assets must be converted into C++ headers to be included in the firmware binary.
2.  Execute the following Python script from the project root:
    ```bash
    source .venv/bin/activate && python scripts/embed_assets.py
    ```
3.  This script will update the `.inc` files in the `src/inc/` directory. Verify the script runs without errors.

### Step 5: Inform the User of Completion

1.  Once the assets are embedded, your task for modifying the web UI is complete.
2.  Inform the user that the UI changes have been built and embedded.
3.  **Crucially, advise the user that they must now rebuild and upload the firmware** for the changes to take effect on their device. You can suggest the command:
    `source .venv/bin/activate && platformio run -e <environment_name> --target upload`
    where `<environment_name>` is one of `esp32d_debug`, `esp32c6_debug`, `esp32_debug`, `esp32c3_debug`, or `esp32s3_debug`.
