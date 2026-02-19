import { getBaseUrl } from './baseUrl.js';

let sharedConnection = null;

export function initializeWebSocket(opts) {
  return createWebSocket(opts);
}

function buildWsUrl() {
  const BASE_URL = getBaseUrl();
  if (BASE_URL) {
    return `${BASE_URL.replace(/^http/, 'ws')}/ws`;
  }
  const wsProtocol = globalThis.location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${wsProtocol}//${globalThis.location.host}/ws`;
}

function createSharedConnection(wsUrl) {
  const listeners = new Set();

  let socket = null;
  let manualClose = false;
  let reconnectTimer = null;
  let heartbeatTimer = null;
  let reconnectAttempt = 0;
  let lastPongMs = 0;

  const MAX_BACKOFF_MS = 8000;
  const HEARTBEAT_INTERVAL_MS = 4000;
  const HEARTBEAT_TIMEOUT_MS = 12000;

  const computeBackoffMs = (attempt) => {
    return Math.min(MAX_BACKOFF_MS, 500 * 2 ** Math.min(attempt, 6));
  };

  const safeInvoke = (cb, ...args) => {
    if (typeof cb !== 'function') return;
    try {
      cb(...args);
    } catch (error) {
      console.error('WebSocket callback error:', error);
    }
  };

  const forEachListener = (selector, ...args) => {
    for (const listener of listeners) {
      safeInvoke(listener[selector], ...args);
    }
  };

  const sendRaw = (data) => {
    if (socket?.readyState === globalThis.WebSocket.OPEN) {
      socket.send(data);
      return true;
    }
    return false;
  };

  const scheduleReconnect = () => {
    if (manualClose || reconnectTimer) return;
    const delay = computeBackoffMs(reconnectAttempt++);
    reconnectTimer = globalThis.setTimeout(() => {
      reconnectTimer = null;
      connect();
    }, delay);
  };

  const stopHeartbeat = () => {
    if (!heartbeatTimer) return;
    globalThis.clearInterval(heartbeatTimer);
    heartbeatTimer = null;
  };

  const forceReconnectFromStaleSocket = () => {
    if (manualClose) return;

    stopHeartbeat();

    const staleSocket = socket;
    socket = null;

    if (staleSocket) {
      staleSocket.onopen = null;
      staleSocket.onmessage = null;
      staleSocket.onerror = null;
      staleSocket.onclose = null;
      try {
        staleSocket.close();
      } catch {
        // noop
      }
    }

    scheduleReconnect();
    forEachListener('onClose');
  };

  const startHeartbeat = () => {
    stopHeartbeat();
    heartbeatTimer = globalThis.setInterval(() => {
      if (manualClose || socket?.readyState !== globalThis.WebSocket.OPEN) {
        return;
      }

      try {
        if (Date.now() - lastPongMs > HEARTBEAT_TIMEOUT_MS) {
          forceReconnectFromStaleSocket();
          return;
        }
        socket.send('{"type":"ping"}');
      } catch {
        forceReconnectFromStaleSocket();
      }
    }, HEARTBEAT_INTERVAL_MS);
  };

  const connect = () => {
    if (manualClose || listeners.size === 0) return;
    socket = new globalThis.WebSocket(wsUrl);
    socket.binaryType = 'arraybuffer';

    socket.onopen = () => {
      reconnectAttempt = 0;
      lastPongMs = Date.now();

      startHeartbeat();
      forEachListener('onOpen');
    };

    socket.onmessage = (event) => {
      if (event.data instanceof ArrayBuffer) {
        forEachListener('onBinary', event.data);
        return;
      }
      try {
        const data = JSON.parse(event.data);
        if (data?.type === 'pong') {
          lastPongMs = Date.now();
          return;
        }
        forEachListener('onMessage', data);
      } catch (e) {
        console.error('WebSocket message JSON parse error:', e);
      }
    };

    socket.onerror = (err) => {
      forEachListener('onError', err);
    };

    socket.onclose = () => {
      stopHeartbeat();
      if (!manualClose) scheduleReconnect();
      forEachListener('onClose');
    };
  };

  const shutdown = (code, reason) => {
    manualClose = true;
    stopHeartbeat();
    if (reconnectTimer) {
      globalThis.clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
    if (socket) {
      try {
        socket.close(code, reason);
      } catch {
        // noop
      }
      socket = null;
    }
  };

  return {
    addListener(listener) {
      listeners.add(listener);
      manualClose = false;
      if (socket?.readyState === globalThis.WebSocket.OPEN) {
        safeInvoke(listener.onOpen);
      } else if (!reconnectTimer) {
        connect();
      }
    },
    removeListener(listener) {
      listeners.delete(listener);
      if (listeners.size === 0) {
        shutdown();
      }
    },
    send: sendRaw,
    close(code, reason) {
      shutdown(code, reason);
    },
    get readyState() {
      return socket ? socket.readyState : globalThis.WebSocket.CLOSED;
    },
  };
}

// Shared WebSocket connection logic
export function createWebSocket({
  onMessage,
  onBinary,
  onOpen,
  onClose,
  onError,
}) {
  const wsUrl = buildWsUrl();
  if (!sharedConnection || sharedConnection.url !== wsUrl) {
    if (sharedConnection?.manager) {
      sharedConnection.manager.close();
    }
    sharedConnection = {
      url: wsUrl,
      manager: createSharedConnection(wsUrl),
    };
  }

  const listener = { onMessage, onBinary, onOpen, onClose, onError };
  sharedConnection.manager.addListener(listener);

  return {
    send(data) {
      sharedConnection?.manager.send(data);
    },
    close() {
      sharedConnection?.manager.removeListener(listener);
    },
    get readyState() {
      return sharedConnection?.manager.readyState ?? globalThis.WebSocket.CLOSED;
    },
  };
}
