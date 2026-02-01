// WiFi setup page as Preact component (initial stub)

import { useRef, useState } from "preact/hooks";

export function WifiSetup() {
  const [result, setResult] = useState(null);
  const [error, setError] = useState(null);
  const [loading, setLoading] = useState(false);
  const [ssidList, setSsidList] = useState([]);
  const [scanning, setScanning] = useState(false);
  const [selectedSsid, setSelectedSsid] = useState("");
  const [manualSsid, setManualSsid] = useState("");
  const [showManual, setShowManual] = useState(false);
  const formRef = useRef(null);
  const handleScan = () => {
    setScanning(true);
    setError(null);
    const pollScan = () => {
      fetch("/wifi/scan")
        .then((res) => res.json())
        .then((data) => {
          if (Array.isArray(data)) {
            // backend returns array directly
            setSsidList({ ssids: data });
            setScanning(false);
          } else if (data?.status === "scanning") {
            // Still scanning, poll again
            setTimeout(pollScan, 1000);
          } else if (Array.isArray(data.ssids)) {
            setSsidList(data);
            setScanning(false);
          } else {
            setError("No networks found");
            setScanning(false);
          }
        })
        .catch((e) => {
          setError("Failed to scan networks");
          setScanning(false);
        });
    };
    pollScan();
  };

  const handleSsidChange = (e) => {
    const value = e.target.value;
    setSelectedSsid(value);
    if (value === "__other__") {
      setShowManual(true);
      setManualSsid("");
    } else {
      setShowManual(false);
      setManualSsid("");
    }
  };

  const handleManualSsidChange = (e) => {
    setManualSsid(e.target.value);
  };

  const handleSubmit = (e) => {
    e.preventDefault();
    setError(null);
    setResult(null);
    setLoading(true);
    const form = formRef.current;
    const data = new URLSearchParams();
    const ssidToSend = showManual ? manualSsid : selectedSsid;
    data.append("ssid", ssidToSend);
    data.append("password", form.password.value);
    const xhr = new XMLHttpRequest();
    xhr.open("POST", form.action, true);
    xhr.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
    xhr.onload = function () {
      setLoading(false);
      if (xhr.status === 200) {
        setResult(xhr.responseText);
      } else {
        setError("WiFi setup failed. Please try again.");
      }
    };
    xhr.onerror = function () {
      setLoading(false);
      setError("Network error. Please try again.");
    };
    xhr.send(data.toString());
  };

  if (result) {
    // Show server response as HTML (as in original captive portal)
    return <div dangerouslySetInnerHTML={{ __html: result }} />;
  }

  return (
    <div
      className="container"
      style={{
        minHeight: "100vh",
        display: "flex",
        flexDirection: "column",
        justifyContent: "flex-start",
      }}
    >
      <section
        className="card"
        style={{ maxWidth: 400, margin: "12px auto 0 auto" }}
      >
        <h2 style={{ fontSize: "1.3em", marginBottom: 8, textAlign: "center" }}>
          🐠 Aquarium Control
        </h2>
        <div
          style={{
            fontWeight: 500,
            color: "var(--secondary-color)",
            textAlign: "center",
            marginBottom: 16,
          }}
        >
          WiFi Setup
        </div>
        <div
          className="instructions"
          style={{ color: "var(--text-secondary)", marginBottom: 16 }}
        >
          Enter your WiFi details to connect.
        </div>
        <form
          id="wifiForm"
          method="POST"
          action="/wifi"
          ref={formRef}
          onSubmit={handleSubmit}
        >
          <label htmlFor="ssid">WiFi SSID</label>
          <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
            <select
              className="text-input"
              name="ssid-select"
              id="ssid-select"
              value={selectedSsid}
              onChange={handleSsidChange}
              required={!showManual}
              style={{ flex: 1 }}
            >
              <option value="" disabled>
                {scanning ? "Scanning..." : "Select network"}
              </option>
              {(Array.isArray(ssidList.ssids) ? ssidList.ssids : ssidList).map(
                (ssid) => (
                  <option value={ssid} key={ssid}>
                    {ssid}
                  </option>
                ),
              )}
              <option value="__other__">Other...</option>
            </select>
            <button
              type="button"
              className="btn btn-secondary"
              style={{ minWidth: 90 }}
              onClick={handleScan}
              disabled={scanning}
            >
              {scanning ? "Scanning..." : "Scan Networks"}
            </button>
          </div>
          {showManual && (
            <input
              className="text-input"
              name="ssid"
              id="ssid"
              type="text"
              required
              autoComplete="on"
              autoFocus
              placeholder="Enter SSID manually"
              value={manualSsid}
              onInput={handleManualSsidChange}
              style={{ marginTop: 8 }}
            />
          )}
          <label htmlFor="password">WiFi Password</label>
          <input
            className="text-input"
            name="password"
            id="password"
            type="password"
            autoComplete="on"
          />
          <button
            className="btn btn-primary"
            type="submit"
            disabled={loading}
            style={{ marginTop: 12 }}
          >
            {loading ? "Connecting..." : "Connect"}
          </button>
        </form>
        {loading && (
          <div className="spinner" style={{ margin: "1em auto 0 auto" }} />
        )}
        {error && (
          <div
            style={{ color: "#ff4466", marginTop: "1em", textAlign: "center" }}
          >
            {error}
          </div>
        )}
        <div
          className="footer"
          style={{ marginTop: 18, color: "#aaa", fontSize: "0.95em" }}
        >
          Aquarium LED Controller &copy; 2026
        </div>
      </section>
    </div>
  );
}
