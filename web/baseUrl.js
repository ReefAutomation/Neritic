// Shared BASE_URL logic
export function getBaseUrl() {
  let BASE_URL = '';
  if (
    globalThis.window !== undefined &&
    (globalThis.location.protocol === 'file:' ||
      globalThis.location.hostname === 'localhost' ||
      globalThis.location.hostname === '127.0.0.1')
  ) {
    BASE_URL = localStorage.getItem('BASE_URL') || '';
    if (!BASE_URL) {
      BASE_URL = globalThis.window.prompt(
        'Please enter the BASE_URL for local development (e.g. http://192.168.1.100):'
      );
      if (BASE_URL) {
        localStorage.setItem('BASE_URL', BASE_URL);
      }
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
