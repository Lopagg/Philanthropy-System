/**
 * @file ui_core.js
 * @brief Gestione della navigazione tra viste (con History API del browser e stack Indietro),
 *        Mini-Log di sistema, lista nodi hardware (Asset) e pannello Inspector.
 */

export let activeInspectorId = null;
let cachedDevicesList = [];
let currentActiveView = 'hub';
let viewHistoryStack = ['hub'];
let cachedNavSocket = null;
let historyListenerInitialized = false;

const MAX_LOG_ENTRIES = 50;

const VIEW_IDS = {
    'hub':            'view-hub',
    'setup':          'view-setup',
    'monitor':        'view-monitor',
    'inspector':      'view-inspector',
    'arena-menu':     'view-arena-menu',
    'arena-manage':   'view-arena-manage',
    'arena-tracking': 'view-arena-tracking',
    'arena-db':       'view-arena-db',
    'arena-planner':  'view-arena-planner'
};

// ============================================================================
// NAVIGAZIONE VISTE, HISTORY API (TASTO INDIETRO MOUSE) E LOG DI SISTEMA
// ============================================================================

/**
 * @brief Controlla se c'è una finestra modale aperta e, in caso affermativo, la chiude.
 * @return true se ha chiuso un modale, false se nessun modale era aperto.
 */
function closeTopOpenModalIfAny() {
    const dialogModal = document.getElementById('tactical-dialog-modal');
    if (dialogModal && !dialogModal.classList.contains('hidden')) {
        window.cancelTacticalDialog?.();
        return true;
    }
    const promptModal = document.getElementById('preset-name-modal');
    if (promptModal && !promptModal.classList.contains('hidden')) {
        window.closePresetNameModal?.();
        return true;
    }
    const cropModal = document.getElementById('crop-modal');
    if (cropModal && !cropModal.classList.contains('hidden')) {
        window.closeCropModal?.();
        return true;
    }
    const opModal = document.getElementById('operator-modal');
    if (opModal && !opModal.classList.contains('hidden')) {
        window.closeModal?.();
        return true;
    }
    const addModal = document.getElementById('add-player-modal');
    if (addModal && !addModal.classList.contains('hidden')) {
        window.closeAddPlayerModal?.();
        return true;
    }
    const regModal = document.getElementById('registration-modal');
    if (regModal && !regModal.classList.contains('hidden')) {
        window.closeRegistrationModal?.();
        return true;
    }
    const facModal = document.getElementById('faction-edit-modal');
    if (facModal && !facModal.classList.contains('hidden')) {
        window.closeFactionModal?.();
        return true;
    }
    return false;
}

/**
 * @brief Inizializza l'intercettazione del tasto Indietro del mouse/browser (popstate)
 *        impedendo di tornare accidentalmente alla pagina di login.
 */
function initBrowserHistoryNavigation() {
    if (historyListenerInitialized) return;
    historyListenerInitialized = true;

    // Inserisce uno stato sentinella + lo stato iniziale per bloccare il ritorno a /login
    window.history.replaceState({ view: 'hub', guard: true }, '', '#hub');
    window.history.pushState({ view: 'hub' }, '', '#hub');

    window.addEventListener('popstate', () => {
        // 1. Se c'è un modal aperto, il tasto Indietro del mouse chiude solo il modal
        if (closeTopOpenModalIfAny()) {
            window.history.pushState({ view: currentActiveView }, '', `#${currentActiveView}`);
            return;
        }

        // 2. Se eravamo nell'editor regole di una partita del Planner, torna al Planner
        if (currentActiveView === 'setup' && window.editingPlannerRoundIdx !== null) {
            window.editingPlannerRoundIdx = null;
            document.getElementById('setup-planner-round-banner')?.classList.add('hidden');
            window.syncSetupFactionLabels?.();
            showView('arena-planner', cachedNavSocket, false);
            window.history.pushState({ view: 'arena-planner' }, '', '#arena-planner');
            return;
        }

        // 3. Torna alla vista precedente nello stack interno
        if (viewHistoryStack.length > 1) {
            viewHistoryStack.pop(); // Rimuove la vista corrente
            const prevView = viewHistoryStack[viewHistoryStack.length - 1] || 'hub';
            showView(prevView, cachedNavSocket, false);
            window.history.pushState({ view: prevView }, '', `#${prevView}`);
        } else {
            // Siamo già su HUB: rimette lo stato per non far mai uscire il browser verso /login
            showView('hub', cachedNavSocket, false);
            window.history.pushState({ view: 'hub' }, '', '#hub');
        }
    });
}

/**
 * @brief Torna dinamicamente alla schermata visitata prima di quella attuale.
 */
export function goBackView(fallbackView = 'hub') {
    if (currentActiveView === 'setup' && window.editingPlannerRoundIdx !== null) {
        window.cancelRoundRulesEditor?.();
        return;
    }
    if (viewHistoryStack.length > 1) {
        viewHistoryStack.pop();
        const prevView = viewHistoryStack[viewHistoryStack.length - 1] || fallbackView;
        showView(prevView, cachedNavSocket, false);
        window.history.pushState({ view: prevView }, '', `#${prevView}`);
    } else {
        showView(fallbackView, cachedNavSocket, true);
    }
}

window.goBackView = goBackView;

export function showView(viewName, socket, pushToHistory = true) {
    initBrowserHistoryNavigation();
    if (socket) cachedNavSocket = socket;
    const activeSocket = socket || cachedNavSocket || window.__zuluSocket;

    // Se si cambia vista mentre si era in modalità pianificazione round, ripulisce il banner
    if (viewName !== 'setup' && window.editingPlannerRoundIdx !== null) {
        window.editingPlannerRoundIdx = null;
        document.getElementById('setup-planner-round-banner')?.classList.add('hidden');
        window.syncSetupFactionLabels?.();
    }

    if (viewName === 'arena-db' && activeSocket) {
        activeSocket.emit('request_database');
    } else if (viewName === 'arena-planner' && activeSocket) {
        activeSocket.emit('request_events');
        activeSocket.emit('request_database');
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

    if (viewName !== 'inspector') {
        activeInspectorId = null;
        document.querySelectorAll('.device-item').forEach(el => el.classList.remove('active'));
    }

    if (viewName === 'setup' && typeof window.updateUIForTargetVersion === 'function') {
        window.updateUIForTargetVersion();
    }

    if (pushToHistory && viewName !== currentActiveView) {
        // Evita duplicati consecutivi nello stack
        if (viewHistoryStack[viewHistoryStack.length - 1] !== viewName) {
            viewHistoryStack.push(viewName);
            if (viewHistoryStack.length > 25) viewHistoryStack.shift();
        }
        window.history.pushState({ view: viewName }, '', `#${viewName}`);
    }

    currentActiveView = viewName;
    updateQuickNavHighlight(viewName);
}

function updateQuickNavHighlight(viewName) {
    document.querySelectorAll('.quick-nav-btn').forEach(btn => {
        const target = btn.dataset.navTarget;
        const isMatch = (target === viewName) ||
                        (target === 'arena-menu' && viewName.startsWith('arena-') && viewName !== 'arena-planner');
        btn.style.background  = isMatch ? 'rgba(0, 255, 204, 0.18)' : 'transparent';
        btn.style.borderColor = isMatch ? 'var(--sop-primary)' : 'var(--sop-dim)';
        btn.style.color       = isMatch ? 'var(--sop-primary)' : '#aaa';
    });
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
            // Se clicchi sullo stesso nodo già aperto nell'Inspector, chiude l'Inspector e torna indietro!
            if (currentActiveView === 'inspector' && activeInspectorId === device.id) {
                goBackView('hub');
                return;
            }
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

    const isV2 = (device.version && device.version.toUpperCase().includes('V2')) || (device.id && device.id.toUpperCase().includes('V2'));
    const isKiosk = (device.mode === 'KIOSK' || (device.id && device.id.toUpperCase().includes('KIOSK')));

    setElText('insp-hw-ver', isV2 ? 'HW_VER: 2.0' : 'HW_VER: 1.0');
    setElText('insp-fw-ver', `FW_VER: ${device.version || '--'}`);

    // Stato Alimentazione / Batteria
    const battEl = document.getElementById('insp-batt');
    if (battEl) {
        if (isKiosk) {
            battEl.textContent = 'AC MAINS (EXT)';
            battEl.style.color = 'var(--sop-primary)';
        } else if (typeof device.batt_pct === 'number' && device.batt_pct >= 0 && typeof device.batt_v === 'number' && device.batt_v >= 5.5) {
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

    // Visibilità scheda Regia Audio Tattica (Solo per nodi RAT-V2)
    const audioDirCard = document.getElementById('insp-audio-director-card');
    if (audioDirCard) {
        audioDirCard.style.display = (isV2 && !isKiosk) ? 'block' : 'none';
    }
    const volSlider = document.getElementById('insp-vol-slider');
    const volValEl  = document.getElementById('insp-vol-val');
    if (volSlider && volValEl && typeof device.volume === 'number' && document.activeElement !== volSlider) {
        volSlider.value = device.volume;
        volValEl.textContent = `${device.volume}%`;
    }

    // Stato pulsante OTA Update
    const otaBtn = document.getElementById('ota-update-btn');
    if (otaBtn) {
        const isVirtual = (device.id === 'SERVER-TDM');
        otaBtn.style.display = isVirtual ? 'none' : 'block';
        const manifest = window.__zuluOtaManifest || {};
        const fwKey = isV2 ? 'rat-v2' : (isKiosk ? 'kiosk-v1' : 'rat-v1');
        const latestVer = manifest[fwKey]?.version;

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