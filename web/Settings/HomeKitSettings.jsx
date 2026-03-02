import { useEffect, useMemo, useState } from 'preact/hooks';
import { apiUrl } from '../baseUrl.js';

export function HomeKitSettings({ showToast }) {
  const [homekit, setHomekit] = useState(null);
  const [loading, setLoading] = useState(true);

  const fetchHomeKit = async () => {
    setLoading(true);
    try {
      const response = await fetch(apiUrl('/api/homekit'));
      const data = await response.json();
      setHomekit(data || {});
    } catch {
      setHomekit(null);
      showToast?.('Failed to load HomeKit metadata', { type: 'error' });
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchHomeKit();
  }, []);

  const qrImageUrl = useMemo(() => {
    if (!homekit?.nativePairingSupported || !homekit?.setupUri) return null;
    return `${apiUrl('/api/homekit/qr')}?t=${Date.now()}`;
  }, [homekit?.nativePairingSupported, homekit?.setupUri]);

  return (
    <section className="card">
      <h2>HomeKit QR Pairing</h2>

      {loading ? (
        <p className="preset-info">Loading HomeKit metadata...</p>
      ) : homekit ? (
        <>
          <div className="config-grid">
            <div className="config-item">
              <label>Bridge Mode</label>
              <input
                type="text"
                className="text-input"
                value={homekit.bridgeMode || 'homebridge-http'}
                readOnly
              />
            </div>
            <div className="config-item">
              <label>Accessory Name</label>
              <input
                type="text"
                className="text-input"
                value={homekit.accessoryName || 'DeepGlow'}
                readOnly
              />
            </div>
            <div className="config-item">
              <label>Setup Code</label>
              <input
                type="text"
                className="text-input"
                value={homekit.setupCode || ''}
                readOnly
              />
            </div>
            <div className="config-item">
              <label>Setup ID</label>
              <input
                type="text"
                className="text-input"
                value={homekit.setupId || ''}
                readOnly
              />
            </div>
          </div>

          {homekit.setupUri ? (
            <div className="homekit-qr-wrap">
              <img
                src={qrImageUrl}
                alt="HomeKit pairing QR"
                className="homekit-qr-image"
                loading="lazy"
              />
              <div className="homekit-qr-meta">
                <label>Setup URI</label>
                <input
                  type="text"
                  className="text-input"
                  value={homekit.setupUri}
                  readOnly
                />
              </div>
            </div>
          ) : (
            <p className="preset-info">
              Direct Apple Home scan is not supported in bridge mode. Pair your
              Homebridge/Home Assistant bridge accessory instead.
            </p>
          )}

          <div className="config-action-bar">
            <button className="btn btn-secondary" onClick={fetchHomeKit}>
              Refresh HomeKit Data
            </button>
            {homekit.nativePairingSupported && homekit.setupUri && (
              <button
                className="btn btn-primary"
                onClick={() =>
                  globalThis.open(apiUrl('/api/homekit/qr'), '_blank')
                }
              >
                Open QR Image
              </button>
            )}
          </div>
        </>
      ) : (
        <p className="preset-info">HomeKit metadata not available.</p>
      )}
    </section>
  );
}
