// Preact entry point for SPA routing
import { h, render } from "preact";
import { App } from "./App.jsx";
import { WifiSetup } from "./WifiSetup.jsx";
import './style.scss';

function getPage() {
  const path = globalThis.location.pathname;
  // Always show WifiSetup for /wifi or /wifi.html
  if (path === "/wifi" || path.endsWith("wifi.html")) return WifiSetup;
  return App;
}

const Page = getPage();
render(h(Page, {}), document.body);
