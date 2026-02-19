import { useState, useEffect, useCallback } from 'preact/hooks';

export function ToastContainer({ toasts, onDismiss }) {
  return (
    <div
      style={{
        position: 'fixed',
        bottom: '2em',
        left: '50%',
        transform: 'translateX(-50%)',
        zIndex: 9999,
        display: 'flex',
        flexDirection: 'column',
        gap: '0.5em',
        pointerEvents: 'none',
      }}
      aria-live="polite"
    >
      {toasts.map((toast) => (
        <ToastItem
          key={toast.id}
          {...toast}
          onDismiss={() => onDismiss(toast.id)}
        />
      ))}
    </div>
  );
}

function ToastItem({
  message,
  type,
  persistent = false,
  autoHide = true,
  hideDelay = 3000,
  onDismiss,
}) {
  const [visible, setVisible] = useState(true);
  const [fade, setFade] = useState('init');

  // Animate in on mount
  useEffect(() => {
    // Start with 'init', then after a tick, set to 'in' for animation
    const enter = setTimeout(() => setFade('in'), 10);
    return () => clearTimeout(enter);
  }, []);

  useEffect(() => {
    if (autoHide && !persistent) {
      const timer = setTimeout(() => {
        setFade('out');
        setTimeout(() => {
          setVisible(false);
          if (onDismiss) onDismiss();
        }, 300);
      }, hideDelay);
      return () => clearTimeout(timer);
    }
  }, [autoHide, persistent, hideDelay, onDismiss]);

  const handleDismiss = () => {
    setFade('out');
    setTimeout(() => {
      setVisible(false);
      if (onDismiss) onDismiss();
    }, 300);
  };

  if (!visible) return null;
  // Map type to class for color
  let typeClass = 'toast-info';
  if (type === 'success') typeClass = 'toast-success';
  else if (type === 'warning') typeClass = 'toast-warning';
  else if (type === 'error') typeClass = 'toast-error';
  return (
    <div className={`toast-custom ${typeClass} fade-${fade}`} role="alert">
      <button
        className="toast-dismiss"
        aria-label="Dismiss"
        onClick={handleDismiss}
      >
        <span style={{ fontSize: '0.8em' }}>&#10005;</span>
      </button>
      <span className="toast-message">{message}</span>
    </div>
  );
}

export function useToast() {
  const [toasts, setToasts] = useState([]);
  const showToast = useCallback((message, opts = {}) => {
    const id = Date.now() + Math.random();
    setToasts((prev) => [
      ...prev,
      {
        id,
        message,
        type: opts.type || 'info',
        persistent: opts.persistent || false,
        autoHide: opts.autoHide ?? true,
        hideDelay: opts.hideDelay || 3000,
      },
    ]);
    return id;
  }, []);
  const hideToast = (id) =>
    setToasts((prev) => prev.filter((t) => t.id !== id));
  const clearToasts = () => setToasts([]);
  return [toasts, showToast, hideToast, clearToasts];
}
