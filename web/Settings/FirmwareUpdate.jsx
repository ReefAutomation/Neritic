import { useState, useEffect } from 'preact/hooks';
import { Fragment } from 'preact';
import { apiUrl } from '../baseUrl.js';
import { Modal } from '../Modal.jsx';

export function FirmwareUpdate({
  showToast,
  hideToast,
  otaProgress,
  otaFileName,
  setOtaFileName,
  otaInputRef,
  localOtaProgress,
  setLocalOtaProgress,
}) {
  // Helper to reset file input
  const resetFileInput = () => {
    if (otaInputRef.current) {
      otaInputRef.current.value = '';
    }
    setOtaFileName('');
  };

  // Helper to extract version from various data formats
  const extractVersion = (data) => {
    const latestArr = data?.latest;
    if (Array.isArray(latestArr)) {
      const entry = latestArr.find((e) => e.version);
      return entry ? entry.version : null;
    }
    if (Array.isArray(data)) {
      const entry = data.find((e) => e.version);
      return entry ? entry.version : null;
    }
    return data?.version || data?.Version || data?.tag || null;
  };

  // Modal state
  const [showModal, setShowModal] = useState(false);
  const [latestVersion, setLatestVersion] = useState(null);
  const [installing, setInstalling] = useState(false);
  const [currentVersion, setCurrentVersion] = useState(null);
  const [remoteStartPending, setRemoteStartPending] = useState(false);

  const isUpgradeActive =
    installing ||
    remoteStartPending ||
    otaProgress >= 0 ||
    localOtaProgress >= 0;

  // Handler for update check and confirmation
  const handleCheckForUpdates = async () => {
    if (isUpgradeActive) return;
    let toastId = null;
    try {
      toastId = showToast('Checking for latest version...', { type: 'info' });
      const resp = await fetch(apiUrl('/api/update'));
      if (!resp.ok) throw new Error('Could not fetch update info');
      const data = await resp.json();
      if (data?.error && !data?.latest) {
        if (toastId) showToast(null, null, toastId);
        showToast(data.error, { type: 'error' });
        return;
      }
      setCurrentVersion(data?.current || null);
      setLatestVersion(extractVersion(data));
      setShowModal(true);
      if (toastId) showToast(null, null, toastId);
    } catch (e) {
      if (toastId) showToast(null, null, toastId);
      console.error('Remote manifest fetch failed:', e);
      showToast('Could not fetch latest version info!', { type: 'error' });
    }
  };

  // Handler for confirming install
  // Track OTA install toast
  const [otaInstallToastId, setOtaInstallToastId] = useState(null);

  const handleConfirmInstall = async () => {
    if (isUpgradeActive) return;
    setInstalling(true);
    setRemoteStartPending(true);
    try {
      const resp = await fetch(apiUrl('/api/update'), {
        method: 'POST',
      });
      const result = await resp.json();
      if (result?.success) {
        // Show persistent toast and keep it until progress starts
        const toastId = showToast('Preparing to upgrade firmware...', {
          type: 'info',
          hideDelay: 4000,
          persistent: true,
          autoHide: false,
        });
        setOtaInstallToastId(toastId);
      } else {
        setRemoteStartPending(false);
        showToast(result?.message ? result.message : 'No update found.', {
          type: 'info',
        });
      }
    } catch (e) {
      setRemoteStartPending(false);
      console.error('Update install failed:', e);
      showToast('Update install failed!', { type: 'error' });
    }
    setShowModal(false);
    setInstalling(false);
  };

  // Dismiss OTA install toast when:
  //  a) progress actually started (> 0), or
  //  b) an error arrived before progress began (otaProgress sentinel < -1)
  useEffect(() => {
    if (!otaInstallToastId) return;
    if (otaProgress > 0 || localOtaProgress > 0 || otaProgress < -1) {
      if (typeof hideToast === 'function') hideToast(otaInstallToastId);
      setOtaInstallToastId(null);
    }
  }, [otaProgress, localOtaProgress, otaInstallToastId, hideToast]);

  useEffect(() => {
    if (otaProgress < -1) {
      setRemoteStartPending(false);
    }
  }, [otaProgress]);

  return (
    <Fragment>
      <Modal
        open={showModal}
        title="Install Update?"
        description={
          <>
            {currentVersion && (
              <div className="modal-version">
                Current version: <span>{`v${currentVersion}`}</span>
              </div>
            )}
            <div className="modal-version">
              Latest version: <span>{latestVersion || 'unknown'}</span>
            </div>
            Are you sure you want to install this update?
          </>
        }
        onClose={() => !installing && setShowModal(false)}
        actions={[
          {
            label: installing ? 'Installing...' : 'Install',
            onClick: handleConfirmInstall,
            className: 'btn btn-primary',
            disabled: isUpgradeActive,
          },
          {
            label: 'Cancel',
            onClick: () => setShowModal(false),
            className: 'btn btn-secondary',
            disabled: isUpgradeActive,
          },
        ]}
      />
      <section class="card system-card">
        <h2>Firmware Update (OTA)</h2>
        <form
          class="ota-form"
          style={{
            width: '100%',
            display: 'flex',
            flexDirection: 'column',
            gap: 16,
          }}
          onSubmit={async (e) => {
            e.preventDefault();
            if (isUpgradeActive) return;
            const otaFile = otaInputRef.current?.files[0];
            if (!otaFile) {
              showToast('Please select a firmware file.', { type: 'error' });
              return;
            }
            try {
              setLocalOtaProgress(0);
              const xhr = new XMLHttpRequest();
              xhr.open('POST', apiUrl('/ota'), true);
              xhr.setRequestHeader('Accept', 'application/json');
              xhr.upload.onprogress = function (evt) {
                if (evt.lengthComputable) {
                  const percent = Math.round((evt.loaded / evt.total) * 100);
                  setLocalOtaProgress(percent);
                }
              };
              xhr.onload = function () {
                if (xhr.status === 200) {
                  showToast('Firmware uploaded! Rebooting...', {
                    type: 'success',
                  });
                  setTimeout(() => globalThis.location.reload(), 7000);
                  resetFileInput();
                } else {
                  showToast(
                    `OTA failed: ${xhr.responseText || xhr.statusText}`,
                    { type: 'error' }
                  );
                  setLocalOtaProgress(-1);
                  resetFileInput();
                }
              };
              xhr.onerror = function () {
                showToast('OTA upload error.', { type: 'error' });
                setLocalOtaProgress(-1);
                resetFileInput();
              };
              xhr.send(otaFile);
            } catch (err) {
              showToast(`OTA error: ${err}`, { type: 'error' });
              setLocalOtaProgress(-1);
              resetFileInput();
            }
          }}
        >
          <div
            style={{
              display: 'flex',
              gap: 10,
              alignItems: 'center',
              flexWrap: 'wrap',
            }}
          >
            {/* Flex container ensures vertical alignment */}
            <input
              ref={otaInputRef}
              type="file"
              name="otaFile"
              accept=".bin,.gz,.zip"
              required
              disabled={isUpgradeActive}
              style={{ display: 'none' }}
              id="otaFileInput"
              onChange={(e) => {
                const file = e.target.files?.[0];
                setOtaFileName(file ? file.name : '');
              }}
            />
            <button
              type="button"
              class="btn btn-primary"
              disabled={isUpgradeActive}
              style={{
                margin: 0,
                display: 'flex',
                alignItems: 'center',
                height: 40,
              }}
              onClick={() => {
                resetFileInput();
                document.getElementById('otaFileInput')?.click();
              }}
            >
              Choose File
            </button>
            <button
              type="submit"
              class="btn btn-primary"
              disabled={isUpgradeActive}
              style={{
                margin: 0,
                height: 40,
                display: 'flex',
                alignItems: 'center',
              }}
            >
              Upload
            </button>
            <span
              class="ota-file-name"
              id="otaFileName"
              style={{
                minWidth: 120,
                fontSize: '0.97em',
                marginLeft: 8,
                alignSelf: 'center',
                height: 40,
                display: 'flex',
                alignItems: 'center',
              }}
            >
              {otaFileName || 'No file chosen'}
            </span>
          </div>
          <button
            type="button"
            class="btn btn-info"
            disabled={isUpgradeActive}
            style={{ width: '100%', marginTop: 0 }}
            onClick={handleCheckForUpdates}
          >
            Check for Updates
          </button>
        </form>
        {/* OTA Progress Bar */}
        {(() => {
          const rawProgress = Number(otaProgress);
          if (!Number.isFinite(rawProgress) || rawProgress < 0) return null;
          const progress = Math.max(0, Math.min(100, Math.round(rawProgress)));
          const isUploading = progress < 100;
          const progressText = isUploading
            ? `Flashing... ${progress}%`
            : 'Update complete! Rebooting...';
          return (
            <div style={{ width: '100%', marginTop: '1em' }}>
              <div
                style={{
                  height: '12px',
                  background: '#eee',
                  borderRadius: '6px',
                  overflow: 'hidden',
                  boxShadow: '0 1px 2px #aaa inset',
                }}
              >
                <div
                  style={{
                    width: `${progress}%`,
                    height: '100%',
                    background: isUploading ? '#66ccff' : '#4caf50',
                    transition: 'width 0.2s',
                  }}
                />
              </div>
              <div
                style={{
                  fontSize: '0.9em',
                  marginTop: '2px',
                  textAlign: 'right',
                }}
              >
                {progressText}
              </div>
            </div>
          );
        })()}
      </section>
    </Fragment>
  );
}
