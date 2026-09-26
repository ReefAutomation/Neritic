// Preact entry point for SPA routing
import { h, render } from 'preact';
import { App } from './App.jsx';
import { WifiSetup } from './WifiSetup.jsx';
import './style.scss';

// Listen for embedders requesting a BASE_URL. When received, show a simple
// prompt dialog (UI) and call setBaseUrl from baseUrl.js so embedded
// environments (like VS Code webview) can supply the value.
import { setBaseUrl } from './baseUrl.js';

window.addEventListener('neritic:request-base-url', () => {
  try {
    // Use a non-blocking DOM prompt so webviews that block window.prompt still work.
    const modal = document.createElement('div');
    modal.id = 'neritic-base-modal';
    modal.style = 'position:fixed;inset:0;display:flex;align-items:center;justify-content:center;background:rgba(0,0,0,0.4);z-index:9999;';
    modal.innerHTML = `
      <div style="background:var(--bg-light);padding:18px;border-radius:8px;max-width:440px;width:90%;box-shadow:0 6px 20px rgba(0,0,0,0.6);">
        <div style="margin-bottom:8px;font-weight:600;color:var(--text-primary)">Enter device BASE_URL</div>
        <input id="neritic-base-input" placeholder="http://192.168.1.100" style="width:100%;padding:8px;border-radius:6px;border:1px solid rgba(255,255,255,0.06);margin-bottom:12px;background:transparent;color:var(--text-primary)" />
        <div style="display:flex;gap:8px;justify-content:flex-end;">
          <button id="neritic-base-cancel" style="padding:8px 12px;border-radius:6px;background:transparent;border:1px solid rgba(255,255,255,0.06);">Cancel</button>
          <button id="neritic-base-ok" style="padding:8px 12px;border-radius:6px;background:linear-gradient(135deg,var(--primary-color),var(--accent-color));border:none;color:#000;font-weight:700">Save</button>
        </div>
      </div>
    `;
    document.body.appendChild(modal);
    const input = modal.querySelector('#neritic-base-input');
    const ok = modal.querySelector('#neritic-base-ok');
    const cancel = modal.querySelector('#neritic-base-cancel');
    input.focus();

    function closeModal() {
      const m = document.getElementById('neritic-base-modal');
      m?.remove();
    }

    ok.addEventListener('click', () => {
      const val = input.value && input.value.trim();
      if (val) setBaseUrl(val);
      closeModal();
    });

    // Allow Enter to save.
    input.addEventListener('keydown', (ev) => {
      if (ev.key === 'Enter') {
        ev.preventDefault();
        const val = input.value && input.value.trim();
        if (val) setBaseUrl(val);
        closeModal();
      } else if (ev.key === 'Escape') {
        closeModal();
      }
    });

    cancel.addEventListener('click', () => closeModal());

    // Close modal if an external embedder sets the base URL
    const onSet = (e) => {
      closeModal();
    };
    window.addEventListener('neritic:base-url-set', onSet, { once: true });
  } catch (e) {
    // Ignore any DOM errors — embedder should implement its own handler.
  }
});

// When a BASE_URL is set (either by the modal or an embedder), reload
// so the app picks up the new URL immediately.
window.addEventListener('neritic:base-url-set', (e) => {
  try {
    // small delay to ensure storage is flushed in some environments
    setTimeout(() => {
      location.reload();
    }, 50);
  } catch (err) {
    /* ignore */
  }
});

function getPage() {
  const path = globalThis.location.pathname;
  // Always show WifiSetup for /wifi or /wifi.html
  if (path === '/wifi' || path.endsWith('wifi.html')) return WifiSetup;
  return App;
}

const Page = getPage();
render(h(Page, {}), document.body);
