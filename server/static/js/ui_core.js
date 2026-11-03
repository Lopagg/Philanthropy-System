/**
 * @file ui_core.js
 * @brief Gestione della navigazione tra viste, Mini-Log di sistema, lista nodi
 *        hardware (Asset) e pannello Inspector.
 */

export let activeInspectorId = null;
let cachedDevicesList = [];

const MAX_LOG_ENTRIES = 50;

const VIEW_IDS = {
    'hub':            'view-hub',
    'setup':          'view-setup',
    'monitor':        'view-monitor',
    'inspector':      'view-inspector',
    'arena-menu':     'view-arena-menu',
    'arena-manage':   'view-arena-manage',
    'arena-tracking': 'view-arena-tracking',
    'arena-db':       'view-arena-db'
};

// ============================================================================
// NAVIGAZIONE VISTE E LOG DI SISTEMA
// ============================================================================

export function showView(viewName, socket) {
    if (viewName === 'arena-db' && socket) {
        socket.emit('request_database');
    }

    Object.values(VIEW_IDS).forEach(id => {
        const el = document.getElementById(id);
        if (el) {
            el.classList.remove('active');
            el.classList.add('hidden');
        }
    });

    const targetId = VIEW_IDS[viewName];
    const targetEl = targetId ? document.getElementById(targetId) : null;
    if (targetEl) {
        targetEl.classList.remove('hidden');
        targetEl.classList.add('active');
    }

    if (viewName === 'hub') {
        activeInspectorId = null;
        document.querySelectorAll('.device-item').forEach(el => el.classList.remove('active'));
    } else if (viewName === 'setup' && typeof window.updateUIForTargetVersion === 'function') {
        window.updateUIForTargetVersion();
    }
}

export function logSystem(text) {
    const elMiniLog = document.getElementById('mini-log');
    if (!elMiniLog) return;

    const div = document.createElement('div');
    div.textContent = `> ${text}`;
    elMiniLog.prepend(div);

    while (elMiniLog.children.length > MAX_LOG_ENTRIES) {
        elMiniLog.removeChild(elMiniLog.lastElementChild);
    }
}

// ============================================================================
// LISTA DISPOSITIVI CONNESSI E PANNELLO INSPECTOR
// ============================================================================

export function updateDeviceList(devices, socket, setMonitoredDeviceCb) {
    cachedDevicesList = devices || [];
    const elDeviceList = document.getElementById('device-list');
    const elAssetCount = document.getElementById('asset-count');
    if (!elDeviceList) return;

    elDeviceList.innerHTML = '';
    if (elAssetCount) elAssetCount.textContent = cachedDevicesList.length;

    if (cachedDevicesList.length === 0) {
        elDeviceList.innerHTML = '<li class="placeholder-msg">SCANNING NETWORK...</li>';
        return;
    }

    cachedDevicesList.forEach(device => {
        const isOnline = device.status === 'ONLINE';
        const cssClass = isOnline ? 'status-online' : 'status-offline';
        const displayName = device.name || device.id;

        const li = document.createElement('li');
        li.className = `device-item ${cssClass}`;
        if (device.id === activeInspectorId) {
            li.classList.add('active');
        }

        const isKiosk = device.mode === 'KIOSK';
        const iconLetter = isKiosk ? 'K' : 'N';
        const iconStyle = isKiosk ? 'background: var(--sop-secondary); color: #000;' : '';

        li.innerHTML = `
            <div class="device-icon" style="${iconStyle}">${iconLetter}</div>
            <div class="device-info">
                <span class="device-id">${displayName}</span>
                <span class="device-mode">${device.mode || 'UNKNOWN'}</span>
            </div>
        `;

        li.addEventListener('click', () => {
            activeInspectorId = device.id;
            updateInspectorData(device);
            showView('inspector', socket);

            if (device.mode !== 'KIOSK' && device.mode !== 'MAIN MENU') {
                setMonitoredDeviceCb(device.id);
                logSystem(`UI LOCKED ON NODE: ${device.id.slice(-4)}`);
            }
        });

        elDeviceList.appendChild(li);
        if (device.id === activeInspectorId) {
            updateInspectorData(device);
        }
    });
}

export function refreshActiveInspector() {
    if (!activeInspectorId) return;
    const dev = cachedDevicesList.find(d => d.id === activeInspectorId);
    if (dev) updateInspectorData(dev);
}

function updateInspectorData(device) {
    const setElText = (id, txt) => {
        const el = document.getElementById(id);
        if (el) el.textContent = txt;
    };

    const displayName = device.name || device.id;
    setElText('inspector-title', `${displayName} // CONFIG`);

    const aliasInput = document.getElementById('insp-alias-input');
    if (aliasInput && document.activeElement !== aliasInput) {
        aliasInput.value = (device.name === device.id) ? "" : (device.name || "");
    }

    setElText('insp-mode', device.mode || 'N/A');
    setElText('insp-state', device.status || 'UNKNOWN');
    setElText('insp-ip', device.ip || 'UNKNOWN');
    setElText('insp-fw-ver', `FW_VER: ${device.version || '--'}`);

    // Foto personalizzata del Prop
    const defaultIcon = document.getElementById('insp-default-icon');
    const devPhotoImg = document.getElementById('insp-device-photo');
    const delPhotoBtn = document.getElementById('del-dev-photo-btn');
    if (defaultIcon && devPhotoImg && delPhotoBtn) {
        if (device.photo) {
            devPhotoImg.src = `/static/uploads/${device.photo}?t=${new Date().getTime()}`;
            devPhotoImg.classList.remove('hidden');
            delPhotoBtn.classList.remove('hidden');
            defaultIcon.classList.add('hidden');
        } else {
            devPhotoImg.classList.add('hidden');
            delPhotoBtn.classList.add('hidden');
            defaultIcon.classList.remove('hidden');
        }
    }

    // Stato Batteria LiPo 2S (Tensione + Percentuale)
    const battEl = document.getElementById('insp-batt');
    if (battEl) {
        if (typeof device.batt_pct === 'number' && device.batt_pct >= 0 && typeof device.batt_v === 'number' && device.batt_v > 1.0) {
            battEl.textContent = `${device.batt_pct}% (${device.batt_v.toFixed(2)}V)`;
            battEl.style.color = device.batt_pct > 40 ? 'var(--sop-success)' : (device.batt_pct > 15 ? 'var(--sop-secondary)' : 'var(--sop-alert)');
        } else {
            battEl.textContent = 'EXT / USB (N/C)';
            battEl.style.color = '#888';
        }
    }

    // Dati diagnostici di rete (RSSI, RTT, Free Heap)
    const tel = device.telemetry || {};
    const rssiEl = document.getElementById('insp-rssi');
    if (rssiEl) {
        if (tel.rssi !== undefined) {
            const qual = Math.max(0, Math.min(100, 2 * (tel.rssi + 100)));
            rssiEl.textContent = `${tel.rssi} dBm (${qual}%)`;
            rssiEl.style.color = tel.rssi >= -67 ? 'var(--sop-success)' : (tel.rssi >= -78 ? 'var(--sop-secondary)' : 'var(--sop-alert)');
        } else {
            rssiEl.textContent = '-- dBm';
            rssiEl.style.color = 'var(--sop-text)';
        }
    }

    const rttEl = document.getElementById('insp-rtt');
    if (rttEl) {
        if (tel.rtt_ms !== undefined) {
            rttEl.textContent = `${tel.rtt_ms} ms`;
            rttEl.style.color = tel.rtt_ms < 50 ? 'var(--sop-success)' : (tel.rtt_ms < 150 ? 'var(--sop-secondary)' : 'var(--sop-alert)');
        } else {
            rttEl.textContent = '-- ms';
            rttEl.style.color = 'var(--sop-text)';
        }
    }

    setElText('insp-heap', tel.heap_kb !== undefined ? `${tel.heap_kb} KB` : '-- KB');

    const isV2 = (device.version && device.version.includes('V2')) || (device.id && device.id.includes('V2'));

    // Stato pulsante Hardware Test (Launch vs Exit)
    const hwTestBtn = document.getElementById('hw-test-btn');
    if (hwTestBtn) {
        hwTestBtn.style.display = isV2 ? 'block' : 'none';
        if (device.mode === 'HW_TEST') {
            hwTestBtn.textContent = "[ EXIT HW TEST ]";
            hwTestBtn.dataset.testing = "true";
        } else {
            hwTestBtn.textContent = "[ LAUNCH HW TEST ]";
            hwTestBtn.dataset.testing = "false";
        }
    }

    // Stato pulsante OTA Update
    const otaBtn = document.getElementById('ota-update-btn');
    if (otaBtn) {
        const isVirtual = (device.id === 'SERVER-TDM' || device.mode === 'KIOSK');
        otaBtn.style.display = isVirtual ? 'none' : 'block';
        const manifest = window.__zuluOtaManifest || {};
        const latestVer = isV2 ? (manifest['rat-v2']?.version) : (manifest['rat-v1']?.version);

        if (latestVer && device.version && device.version !== latestVer) {
            otaBtn.textContent = `[ UPDATE OTA AVAILABLE: ${latestVer} ]`;
            otaBtn.style.borderColor = 'var(--sop-success)';
            otaBtn.style.color = 'var(--sop-success)';
        } else {
            otaBtn.textContent = latestVer ? `[ REFLASH OTA (${latestVer}) ]` : `[ UPDATE FIRMWARE OTA ]`;
            otaBtn.style.borderColor = 'var(--sop-primary)';
            otaBtn.style.color = 'var(--sop-primary)';
        }
    }

    const hwTitle = document.getElementById('insp-hw-title');
    const hwDesc = document.getElementById('insp-hw-desc');
    if (hwTitle && hwDesc) {
        if (device.mode === 'KIOSK') {
            hwTitle.textContent = "TACTICAL KIOSK V1";
            hwDesc.textContent = "Terminale RFID per l'assegnazione squadre e gestione sistema.";
        } else if (device.id === 'SERVER-TDM' || device.mode === 'TEAM_DEATHMATCH') {
            hwTitle.textContent = "VIRTUAL TDM SERVER";
            hwDesc.textContent = "Motore logico software in esecuzione sul server centrale.";
        } else if (isV2) {
            hwTitle.textContent = "REMOTE ACCESS TERMINAL V2";
            hwDesc.textContent = "Prop interattivo avanzato I2C/I2S basato su ESP32-S3.";
        } else {
            hwTitle.textContent = "REMOTE ACCESS TERMINAL V1";
            hwDesc.textContent = "Terminale tattico multi-ruolo basato su architettura ESP32.";
        }
    }
}

// ============================================================================
// OROLOGIO E DATA DI SISTEMA
// ============================================================================

export function updateClock() {
    const elMissionClock = document.getElementById('mission-clock');
    const elMissionDate = document.getElementById('mission-date');
    const dateOptions = { weekday: 'long', year: 'numeric', month: 'long', day: 'numeric' };

    const tick = () => {
        const now = new Date();
        if (elMissionClock) {
            elMissionClock.textContent = now.toLocaleTimeString('it-IT', { hour12: false });
        }
        if (elMissionDate) {
            elMissionDate.textContent = now.toLocaleDateString('it-IT', dateOptions).toUpperCase();
        }
    };

    tick();
    setInterval(tick, 1000);
}