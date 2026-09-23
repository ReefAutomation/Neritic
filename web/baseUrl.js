// Shared BASE_URL logic
export function getBaseUrl() {
  let BASE_URL = '';
  const isLocalHost =
    globalThis.window !== undefined &&
    (globalThis.location.protocol === 'file:' ||
      globalThis.location.hostname === 'localhost' ||
      globalThis.location.hostname === '127.0.0.1');

  // Prefer explicit Vite dev flag when available so local dev shells work.
  const isDev = typeof import.meta !== 'undefined' && import.meta.env?.DEV;

  if (isLocalHost || isDev) {
    BASE_URL = localStorage.getItem('BASE_URL') || '';
    if (!BASE_URL) {
      // Use the same non-blocking DOM popup flow across all browsers by
      // dispatching an event that the app listens for and shows the UI.
      try {
        // Avoid showing the popup more than once per session.
        const alreadyShown = sessionStorage.getItem('neritic:base-prompt-shown');
        if (!alreadyShown) {
          globalThis.window?.dispatchEvent(new CustomEvent('neritic:request-base-url'));
          sessionStorage.setItem('neritic:base-prompt-shown', '1');
        }
      } catch (err) {
        /* ignore */
      }
      // Re-read after any external setter may have run.
      BASE_URL = localStorage.getItem('BASE_URL') || '';
    }
  } else if (globalThis.window?.BASE_URL) {
    BASE_URL = globalThis.window.BASE_URL;
  }

  return BASE_URL;
}

// Convenience: build a full URL for a given path (e.g. '/api/state').
export function apiUrl(path) {
  return `${getBaseUrl()}${path}`;
}

/**
 * Allow external embedders (e.g. VS Code webview) to set the BASE_URL
 * programmatically after handling the `neritic:request-base-url` event.
 */
export function setBaseUrl(url) {
  if (!url) return;
  try {
    localStorage.setItem('BASE_URL', url);
    // Notify any embedders that a BASE_URL was set.
    try {
      globalThis.window?.dispatchEvent(new CustomEvent('neritic:base-url-set', { detail: { url } }));
    } catch (e) {
      /* ignore */
    }
  } catch (e) {
    /* ignore storage errors */
  }
}
