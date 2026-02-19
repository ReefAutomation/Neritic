import { useState, useEffect } from 'preact/hooks';
import { Home } from './Home.jsx';
import { Config } from './Config.jsx';

export const TABS = Object.freeze({
  HOME: 'home',
  CONFIG: 'config',
});

// Hook to get current tab from URL hash
export function useCurrentTab() {
  const [tab, setTab] = useState(() => {
    if (globalThis.location.hash === `#${TABS.CONFIG}`) return TABS.CONFIG;
    if (globalThis.location.hash === `#${TABS.HOME}`) return TABS.HOME;
    return TABS.HOME;
  });
  useEffect(() => {
    const onHashChange = () => {
      if (globalThis.location.hash === `#${TABS.CONFIG}`) setTab(TABS.CONFIG);
      else setTab(TABS.HOME);
    };
    globalThis.addEventListener('hashchange', onHashChange);
    return () => globalThis.removeEventListener('hashchange', onHashChange);
  }, []);
  return tab;
}

// Button config for StatusBar navigation
export const BUTTONS = {
  [TABS.HOME]: {
    label: 'Configuration',
    svgPath:
      'M259.1 73.5C262.1 58.7 275.2 48 290.4 48L350.2 48C365.4 48 378.5 58.7 381.5 73.5L396 143.5C410.1 149.5 423.3 157.2 435.3 166.3L503.1 143.8C517.5 139 533.3 145 540.9 158.2L570.8 210C578.4 223.2 575.7 239.8 564.3 249.9L511 297.3C511.9 304.7 512.3 312.3 512.3 320C512.3 327.7 511.8 335.3 511 342.7L564.4 390.2C575.8 400.3 578.4 417 570.9 430.1L541 481.9C533.4 495 517.6 501.1 503.2 496.3L435.4 473.8C423.3 482.9 410.1 490.5 396.1 496.6L381.7 566.5C378.6 581.4 365.5 592 350.4 592L290.6 592C275.4 592 262.3 581.3 259.3 566.5L244.9 496.6C230.8 490.6 217.7 482.9 205.6 473.8L137.5 496.3C123.1 501.1 107.3 495.1 99.7 481.9L69.8 430.1C62.2 416.9 64.9 400.3 76.3 390.2L129.7 342.7C128.8 335.3 128.4 327.7 128.4 320C128.4 312.3 128.9 304.7 129.7 297.3L76.3 249.8C64.9 239.7 62.3 223 69.8 209.9L99.7 158.1C107.3 144.9 123.1 138.9 137.5 143.7L205.3 166.2C217.4 157.1 230.6 149.5 244.6 143.4L259.1 73.5zM320.3 400C364.5 399.8 400.2 363.9 400 319.7C399.8 275.5 363.9 239.8 319.7 240C275.5 240.2 239.8 276.1 240 320.3C240.2 364.5 276.1 400.2 320.3 400z',
    nextTab: TABS.CONFIG,
  },
  [TABS.CONFIG]: {
    label: 'Back to Main',
    svgPath:
      'M341.8 72.6C329.5 61.2 310.5 61.2 298.3 72.6L74.3 280.6C64.7 289.6 61.5 303.5 66.3 315.7C71.1 327.9 82.8 336 96 336L112 336L112 512C112 547.3 140.7 576 176 576L464 576C499.3 576 528 547.3 528 512L528 336L544 336C557.2 336 569 327.9 573.8 315.7C578.6 303.5 575.4 289.5 565.8 280.6L341.8 72.6zM304 384L336 384C362.5 384 384 405.5 384 432L384 528L256 528L256 432C256 405.5 277.5 384 304 384z',
    nextTab: TABS.HOME,
  },
};

export function getHandshakeType(tab) {
  return { type: tab === TABS.CONFIG ? 'ota_client' : 'state' };
}

export function useTabs() {
  // Tab state from URL hash, fallback to localStorage, then home
  const getInitialTab = () => {
    if (globalThis.location.hash === `#${TABS.CONFIG}`) return TABS.CONFIG;
    if (globalThis.location.hash === `#${TABS.HOME}`) return TABS.HOME;
    let saved;
    try {
      saved = localStorage.getItem('deepglow_tab');
    } catch (e) {
      console.error('Error accessing localStorage for deepglow_tab:', e);
    }
    if (saved === TABS.CONFIG || saved === TABS.HOME) return saved;
    return TABS.HOME;
  };
  const [tab, setTab] = useState(getInitialTab);

  // Update tab when hash changes
  useEffect(() => {
    const onHashChange = () => {
      if (globalThis.location.hash === `#${TABS.CONFIG}`) setTab(TABS.CONFIG);
      else setTab(TABS.HOME);
    };
    globalThis.addEventListener('hashchange', onHashChange);
    return () => globalThis.removeEventListener('hashchange', onHashChange);
  }, []);

  // Persist tab to localStorage and URL hash on change
  useEffect(() => {
    try {
      if (tab === TABS.CONFIG) {
        localStorage.setItem('deepglow_tab', TABS.CONFIG);
        globalThis.location.hash = `#${TABS.CONFIG}`;
      } else {
        localStorage.setItem('deepglow_tab', TABS.HOME);
        globalThis.location.hash = `#${TABS.HOME}`;
      }
    } catch {}
  }, [tab]);

  return [tab, setTab];
}

export function Tabs({
  tab,
  setTab,
  state,
  presets,
  timers,
  effects,
  activePreset,
  ledBarRef,
  sendState,
  config,
  setActivePreset,
  timezones,
  sunTimes,
  showToast,
  hideToast,
  loaded,
  setConfig,
  otaProgress,
}) {
  return tab === TABS.HOME ? (
    <Home
      state={state}
      presets={presets}
      timers={timers}
      effects={effects}
      activePreset={activePreset}
      ledBarRef={ledBarRef}
      sendState={sendState}
      setTab={setTab}
      config={config}
      setActivePreset={setActivePreset}
    />
  ) : (
    <Config
      config={config}
      state={state}
      sendState={sendState}
      timezones={timezones}
      sunTimes={sunTimes}
      showToast={showToast}
      hideToast={hideToast}
      loaded={loaded}
      setTab={setTab}
      presets={presets || []}
      setConfig={setConfig}
      otaProgress={otaProgress}
    />
  );
}
