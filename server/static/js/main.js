/**
 * @file main.js
 * @brief Entry point principale del Frontend Web Zulu Game System.
 *
 * Gestisce:
 * - Connessione singleton Socket.IO e sincronizzazione real-time con il server Flask.
 * - Costruttore visuale delle sequenze modulari V2 (HOLD, PIN, KEY a 8 modalità, RFID a 4 modalità).
 * - Validazione automatica di coerenza per lo stato delle chiavi K1/K2 tra innesco e disinnesco.
 * - Gestione fazioni, meteo tattico, ritaglio foto profilo/prop (Cropper.js), OTA e registrazione tessere.
 */

import { showView, logSystem, updateDeviceList, refreshActiveInspector, updateClock, activeInspectorId } from './ui_core.js';
import { resetMonitorLayout, updateUniversalFeed, renderRules, updateMonitorDailyWins } from './game_monitor.js';
import { renderArenaRoster, renderDatabase, populateProfileModal, getCurrentProfileUid, globalPlayersDB } from './roster_manager.js';

// ============================================================================
// STATO GLOBALE CONDIVISO (SINGLETON SU WINDOW)
// ============================================================================

const socket = window.__zuluSocket || (window.__zuluSocket = io());
let currentDevices = [];
let arenaRoster = {};
let monitoredDeviceId = null;
let currentCropper = null;

window.v2ArmSequence = window.v2ArmSequence || [{ type: 'HOLD', val: 5, rfidMode: 'TEAM', rfidPlayers: [], rfidCustomUid: '' }];
window.v2DefuseSequence = window.v2DefuseSequence || [{ type: 'HOLD', val: 10, rfidMode: 'TEAM', rfidPlayers: [], rfidCustomUid: '' }];

export let currentFactions = window.__zuluFactions || (window.__zuluFactions = {
    ALPHA: { name: 'ALPHA', color: '#ff3333', daily_wins: 0 },
    BRAVO: { name: 'BRAVO', color: '#55ff55', daily_wins: 0 }
});

// ============================================================================
// STILI DINAMICI FAZIONI E SELEZIONE TERMINALE TARGET
// ============================================================================

function applyFactionStyles(factions) {
    let styleTag = document.getElementById('dynamic-faction-styles');
    if (!styleTag) {
        styleTag = document.createElement('style');
        styleTag.id = 'dynamic-faction-styles';
        document.head.appendChild(styleTag);
    }

    styleTag.innerHTML = `
        :root {
            --team-a-color: ${factions.ALPHA.color};
            --team-b-color: ${factions.BRAVO.color};
        }
        .team-alpha-box .arena-header, #header-alpha-track-in { background: ${factions.ALPHA.color}33 !important; color: var(--team-a-color) !important; }
        .setup-roster-alpha, .count-alpha, .score.alpha, .alpha-list li, .d-val-red, .faction-a .faction-score, #dom-lbl-alpha { 
            color: var(--team-a-color) !important; 
            text-shadow: 0 0 15px ${factions.ALPHA.color}66 !important;
            border-left-color: var(--team-a-color) !important;
        }
        .team-bravo-box .arena-header, #header-bravo-track-in { background: ${factions.BRAVO.color}33 !important; color: var(--team-b-color) !important; }
        .setup-roster-bravo, .count-bravo, .score.bravo, .bravo-list li, .d-val-green, .faction-b .faction-score, #dom-lbl-bravo { 
            color: var(--team-b-color) !important; 
            text-shadow: 0 0 15px ${factions.BRAVO.color}66 !important;
            border-left-color: var(--team-b-color) !important;
        }
    `;
}

export function updateSetupDropdown() {
    const elSetupTargetSelect = document.getElementById('setup-target-select');
    if (!elSetupTargetSelect) return;

    const currentSelection = elSetupTargetSelect.value;
    elSetupTargetSelect.innerHTML = '<option value="">-- Seleziona nodo target --</option>';

    const availableNodes = currentDevices.filter(d => {
        const idUpper = (d.id || '').toUpperCase();
        const modeUpper = (d.mode || '').toUpperCase();
        if (idUpper.includes('KIOSK') || modeUpper === 'KIOSK' || idUpper === 'SERVER-TDM') {
            return false;
        }
        return (d.game_state === 'STANDBY' || modeUpper === 'MAIN MENU' || modeUpper === 'TERMINAL' || modeUpper === 'STANDBY');
    });

    availableNodes.forEach(d => {
        const opt = document.createElement('option');
        opt.value = d.id;
        opt.textContent = d.name || d.id;
        elSetupTargetSelect.appendChild(opt);
    });

    if (currentSelection && availableNodes.some(d => d.id === currentSelection)) {
        elSetupTargetSelect.value = currentSelection;
    } else if (availableNodes.length > 0) {
        elSetupTargetSelect.value = availableNodes[0].id;
    }

    if (typeof window.updateUIForTargetVersion === 'function') {
        window.updateUIForTargetVersion();
    }
}

window.updateUIForTargetVersion = function() {
    const targetSelect = document.getElementById('setup-target-select');
    const v1Block = document.getElementById('sd-v1-legacy-params');
    const v2Block = document.getElementById('sd-v2-modular-params');
    const modeSelect = document.getElementById('setup-mode-select');

    if (!targetSelect || !modeSelect || modeSelect.value !== 'sd' || !v1Block || !v2Block) return;
    const targetId = targetSelect.value;

    let isV2 = false;
    if (targetId) {
        const device = currentDevices.find(d => d.id === targetId);
        if ((device && device.version && device.version.includes("V2")) || targetId.includes("V2")) {
            isV2 = true;
        }
    }

    if (isV2) {
        const wasHidden = v2Block.classList.contains('hidden');
        v1Block.classList.add('hidden');
        v2Block.classList.remove('hidden');
        const armContainer = document.getElementById('v2-arm-sequence-container');
        if (wasHidden || (armContainer && armContainer.children.length === 0)) {
            renderV2Sequences();
        }
    } else {
        v2Block.classList.add('hidden');
        v1Block.classList.remove('hidden');
    }
};

// ============================================================================
// COSTRUTTORE SEQUENZE MODULARI V2 (HOLD, PIN, KEY, RFID)
// ============================================================================

const KEY_MODES_META = [
    { value: 'K1_ON',         label: 'SLOT 1 -> ON (Inserisci/Gira K1)',     k1Req: 'OFF', k1Next: 'ON',  k2Req: null,  k2Next: null },
    { value: 'K1_OFF',        label: 'SLOT 1 -> OFF (Gira/Estrai K1)',       k1Req: 'ON',  k1Next: 'OFF', k2Req: null,  k2Next: null },
    { value: 'K2_ON',         label: 'SLOT 2 -> ON (Inserisci/Gira K2)',     k1Req: null,  k1Next: null,  k2Req: 'OFF', k2Next: 'ON' },
    { value: 'K2_OFF',        label: 'SLOT 2 -> OFF (Gira/Estrai K2)',       k1Req: null,  k1Next: null,  k2Req: 'ON',  k2Next: 'OFF' },
    { value: 'BOTH_ON',       label: 'K1 + K2 -> ON (Entrambe ON)',          k1Req: 'OFF', k1Next: 'ON',  k2Req: 'OFF', k2Next: 'ON' },
    { value: 'BOTH_OFF',      label: 'K1 + K2 -> OFF (Estrai Entrambe)',     k1Req: 'ON',  k1Next: 'OFF', k2Req: 'ON',  k2Next: 'OFF' },
    { value: 'BOTH_SYNC_ON',  label: 'K1 + K2 SYNC -> ON (Insieme < 1.5s)',  k1Req: 'OFF', k1Next: 'ON',  k2Req: 'OFF', k2Next: 'ON' },
    { value: 'BOTH_SYNC_OFF', label: 'K1 + K2 SYNC -> OFF (Estrai Insieme)', k1Req: 'ON',  k1Next: 'OFF', k2Req: 'ON',  k2Next: 'OFF' }
];

function isKeyModeAllowed(modeMeta, state) {
    if (modeMeta.value === 'BOTH_ON') return !(state.k1 === 'ON' && state.k2 === 'ON');
    if (modeMeta.value === 'BOTH_OFF') return !(state.k1 === 'OFF' && state.k2 === 'OFF');
    if (modeMeta.k1Req && state.k1 !== 'FREE' && state.k1 !== modeMeta.k1Req) return false;
    if (modeMeta.k2Req && state.k2 !== 'FREE' && state.k2 !== modeMeta.k2Req) return false;
    return true;
}

function applyKeyModeState(modeVal, state) {
    const meta = KEY_MODES_META.find(m => m.value === modeVal);
    if (!meta) return;
    if (meta.k1Next) state.k1 = meta.k1Next;
    if (meta.k2Next) state.k2 = meta.k2Next;
}

function sanitizeKeySequences() {
    const state = { k1: 'FREE', k2: 'FREE' };
    const allSteps = [...window.v2ArmSequence, ...window.v2DefuseSequence];

    allSteps.forEach(step => {
        if (step.type !== 'KEY') return;
        step.allowedKeyModes = KEY_MODES_META.filter(m => isKeyModeAllowed(m, state)).map(m => m.value);
        if (!step.val || !step.allowedKeyModes.includes(step.val)) {
            step.val = step.allowedKeyModes[0] || 'K1_ON';
        }
        applyKeyModeState(step.val, state);
    });
}

window.addSequenceStep = function(type) {
    const arr = type === 'arm' ? window.v2ArmSequence : window.v2DefuseSequence;
    if (arr.length >= 5) {
        alert("Massimo 5 step consentiti per sequenza.");
        return;
    }
    arr.push({ type: 'HOLD', val: 5, rfidMode: 'TEAM', rfidPlayers: [], rfidCustomUid: '' });
    renderV2Sequences();
};

window.removeSequenceStep = function(type, index) {
    const arr = type === 'arm' ? window.v2ArmSequence : window.v2DefuseSequence;
    arr.splice(index, 1);
    if (arr.length === 0) {
        arr.push({ type: 'HOLD', val: 5, rfidMode: 'TEAM', rfidPlayers: [], rfidCustomUid: '' });
    }
    renderV2Sequences();
};

window.moveSequenceStep = function(type, index, direction) {
    const arr = type === 'arm' ? window.v2ArmSequence : window.v2DefuseSequence;
    const targetIndex = index + direction;
    if (targetIndex < 0 || targetIndex >= arr.length) return;
    const temp = arr[index];
    arr[index] = arr[targetIndex];
    arr[targetIndex] = temp;
    renderV2Sequences();
};

window.updateSequenceStepType = function(type, index, selectElement) {
    const arr = type === 'arm' ? window.v2ArmSequence : window.v2DefuseSequence;
    arr[index].type = selectElement.value;
    if (arr[index].type === 'HOLD') arr[index].val = 5;
    else if (arr[index].type === 'PIN') arr[index].val = "1234";
    else if (arr[index].type === 'KEY') arr[index].val = "K1_ON";
    else if (arr[index].type === 'RFID') {
        arr[index].rfidMode = arr[index].rfidMode || 'TEAM';
        arr[index].rfidPlayers = arr[index].rfidPlayers || [];
        arr[index].rfidCustomUid = arr[index].rfidCustomUid || '';
    }
    renderV2Sequences();
};

window.updateSequenceStepVal = function(type, index, inputElement) {
    const arr = type === 'arm' ? window.v2ArmSequence : window.v2DefuseSequence;
    arr[index].val = inputElement.value;
    if (arr[index].type === 'KEY') {
        renderV2Sequences();
    }
};

window.updateRfidStepMode = function(type, index, selectElement) {
    const arr = type === 'arm' ? window.v2ArmSequence : window.v2DefuseSequence;
    arr[index].rfidMode = selectElement.value;
    renderV2Sequences();
};

window.addRfidPlayer = function(type, index, selectElement) {
    const uid = selectElement.value;
    if (!uid) return;
    const arr = type === 'arm' ? window.v2ArmSequence : window.v2DefuseSequence;
    if (!Array.isArray(arr[index].rfidPlayers)) arr[index].rfidPlayers = [];
    if (!arr[index].rfidPlayers.includes(uid)) {
        arr[index].rfidPlayers.push(uid);
    }
    renderV2Sequences();
};

window.removeRfidPlayer = function(type, index, uid) {
    const arr = type === 'arm' ? window.v2ArmSequence : window.v2DefuseSequence;
    if (!Array.isArray(arr[index].rfidPlayers)) return;
    arr[index].rfidPlayers = arr[index].rfidPlayers.filter(u => u !== uid);
    renderV2Sequences();
};

window.updateRfidCustomUid = function(type, index, inputElement) {
    const arr = type === 'arm' ? window.v2ArmSequence : window.v2DefuseSequence;
    arr[index].rfidCustomUid = inputElement.value.trim().toUpperCase();
};

function getPlayerDisplayName(uid) {
    if (arenaRoster && arenaRoster[uid]) {
        const teamName = arenaRoster[uid].team === 'ALPHA' ? currentFactions.ALPHA.name : currentFactions.BRAVO.name;
        return `${arenaRoster[uid].name} [${teamName}]`;
    }
    if (typeof globalPlayersDB !== 'undefined' && Array.isArray(globalPlayersDB)) {
        const found = globalPlayersDB.find(p => p.uid === uid);
        if (found) return found.alias;
    }
    return uid;
}

function renderV2Sequences() {
    const armContainer = document.getElementById('v2-arm-sequence-container');
    const defuseContainer = document.getElementById('v2-defuse-sequence-container');
    if (!armContainer || !defuseContainer) return;

    sanitizeKeySequences();

    const generateHtml = (arr, typeName) => {
        let html = '';
        arr.forEach((step, index) => {
            let inputHtml = '';
            if (step.type === 'HOLD') {
                inputHtml = `<input type="number" class="setup-input v2-step-val" style="width: 80px;" value="${step.val}" oninput="updateSequenceStepVal('${typeName}', ${index}, this)" onchange="updateSequenceStepVal('${typeName}', ${index}, this)"> <span style="color:#aaa; font-size:12px;">sec</span>`;
            } else if (step.type === 'PIN') {
                inputHtml = `<input type="text" class="setup-input v2-step-val" style="width: 100px;" value="${step.val}" oninput="updateSequenceStepVal('${typeName}', ${index}, this)" onchange="updateSequenceStepVal('${typeName}', ${index}, this)">`;
            } else if (step.type === 'KEY') {
                const allowed = step.allowedKeyModes || KEY_MODES_META.map(m => m.value);
                const opts = KEY_MODES_META.map(m => {
                    const isAllowed = allowed.includes(m.value);
                    const selected = (step.val === m.value) ? 'selected' : '';
                    const disabled = isAllowed ? '' : 'disabled';
                    const suffix = isAllowed ? '' : ' [Conflitto stato chiave]';
                    return `<option value="${m.value}" ${selected} ${disabled}>${m.label}${suffix}</option>`;
                }).join('');
                inputHtml = `<select class="tactical-select v2-step-val" style="width: 100%; margin-bottom: 0;" onchange="updateSequenceStepVal('${typeName}', ${index}, this)">${opts}</select>`;
            } else if (step.type === 'RFID') {
                const rfidMode = step.rfidMode || 'TEAM';
                const selectedPlayers = Array.isArray(step.rfidPlayers) ? step.rfidPlayers : [];
                const roleLabel = typeName === 'arm' ? 'SQUADRA ATTACCANTE' : 'SQUADRA DIFENSORE';

                let subControlHtml = '';
                if (rfidMode === 'TEAM') {
                    subControlHtml = `<span style="color:#aaa; font-size:12px; margin-left:8px;">Qualsiasi operatore della <b>${roleLabel}</b></span>`;
                } else if (rfidMode === 'PLAYERS') {
                    const chipsHtml = selectedPlayers.length > 0
                        ? selectedPlayers.map(uid => `
                            <span style="display:inline-flex; align-items:center; background:rgba(0,255,204,0.12); border:1px solid var(--sop-primary); color:var(--sop-text); padding:2px 8px; border-radius:3px; font-size:12px; margin:2px 4px 2px 0;">
                                ${getPlayerDisplayName(uid)}
                                <span style="cursor:pointer; margin-left:6px; color:var(--sop-alert); font-weight:bold;" onclick="removeRfidPlayer('${typeName}', ${index}, '${uid}')">×</span>
                            </span>`).join('')
                        : `<span style="color:var(--sop-alert); font-size:12px; margin-right:8px;">Nessun giocatore selezionato</span>`;

                    const allDbPlayers = (typeof globalPlayersDB !== 'undefined' && Array.isArray(globalPlayersDB)) ? globalPlayersDB : [];
                    const playerOptions = allDbPlayers
                        .filter(p => !selectedPlayers.includes(p.uid))
                        .map(p => {
                            const inRoster = arenaRoster && arenaRoster[p.uid];
                            const teamTag = inRoster ? ` [${inRoster.team === 'ALPHA' ? currentFactions.ALPHA.name : currentFactions.BRAVO.name}]` : '';
                            return `<option value="${p.uid}">${p.alias}${teamTag}</option>`;
                        }).join('');

                    subControlHtml = `
                        <div style="display:flex; flex-wrap:wrap; align-items:center; gap:6px; margin-top:6px; width:100%;">
                            <div>${chipsHtml}</div>
                            <select class="tactical-select" style="width:auto; min-width:190px; margin-bottom:0; padding:4px 8px; font-size:12px;" onchange="addRfidPlayer('${typeName}', ${index}, this)">
                                <option value="">+ Aggiungi giocatore...</option>
                                ${playerOptions}
                            </select>
                        </div>`;
                } else if (rfidMode === 'CUSTOM') {
                    subControlHtml = `<input type="text" class="setup-input v2-rfid-custom" style="width:190px; margin-left:8px;" placeholder="UID (es. 04863EB76F6180)" value="${step.rfidCustomUid || ''}" oninput="updateRfidCustomUid('${typeName}', ${index}, this)">`;
                } else {
                    subControlHtml = `<span style="color:#aaa; font-size:12px; margin-left:8px;">Qualsiasi tessera RFID/NFC</span>`;
                }

                inputHtml = `
                    <div style="display:flex; flex-wrap:wrap; align-items:center; width:100%;">
                        <select class="tactical-select v2-rfid-mode" style="width:auto; margin-bottom:0;" onchange="updateRfidStepMode('${typeName}', ${index}, this)">
                            <option value="TEAM" ${rfidMode === 'TEAM' ? 'selected' : ''}>${roleLabel}</option>
                            <option value="PLAYERS" ${rfidMode === 'PLAYERS' ? 'selected' : ''}>GIOCATORI SPECIFICI</option>
                            <option value="CUSTOM" ${rfidMode === 'CUSTOM' ? 'selected' : ''}>TESSERA SPECIFICA (UID)</option>
                            <option value="ANY" ${rfidMode === 'ANY' ? 'selected' : ''}>QUALSIASI TESSERA</option>
                        </select>
                        ${subControlHtml}
                    </div>`;
            }

            const canUp = index > 0;
            const canDown = index < arr.length - 1;

            html += `
                <div class="v2-step-row" style="display: flex; align-items: center; margin-bottom: 10px; background: rgba(0,0,0,0.3); padding: 6px 8px; border-radius: 4px;">
                    <span style="color:var(--sop-primary); font-weight:bold; margin-right:10px;">#${index+1}</span>
                    <select class="tactical-select v2-step-type" style="width: auto; margin-right: 10px; margin-bottom: 0;" onchange="updateSequenceStepType('${typeName}', ${index}, this)">
                        <option value="HOLD" ${step.type === 'HOLD' ? 'selected' : ''}>HOLD BUTTON</option>
                        <option value="PIN" ${step.type === 'PIN' ? 'selected' : ''}>ENTER PIN</option>
                        <option value="KEY" ${step.type === 'KEY' ? 'selected' : ''}>KEY SWITCH</option>
                        <option value="RFID" ${step.type === 'RFID' ? 'selected' : ''}>SCAN RFID</option>
                    </select>
                    <div style="flex: 1; margin-right: 10px;">${inputHtml}</div>
                    <div style="display:flex; align-items:center; gap:4px; font-family:monospace;">
                        <span class="action-btn" style="color:${canUp ? 'var(--sop-primary)' : '#444'}; pointer-events:${canUp ? 'auto' : 'none'};" onclick="moveSequenceStep('${typeName}', ${index}, -1)" title="Sposta su">[▲]</span>
                        <span class="action-btn" style="color:${canDown ? 'var(--sop-primary)' : '#444'}; pointer-events:${canDown ? 'auto' : 'none'};" onclick="moveSequenceStep('${typeName}', ${index}, 1)" title="Sposta giù">[▼]</span>
                        <span class="action-btn" style="color:var(--sop-alert); margin-left:4px;" onclick="removeSequenceStep('${typeName}', ${index})" title="Rimuovi">[X]</span>
                    </div>
                </div>`;
        });
        return html;
    };

    armContainer.innerHTML = generateHtml(window.v2ArmSequence, 'arm');
    defuseContainer.innerHTML = generateHtml(window.v2DefuseSequence, 'defuse');
}

window.renderV2Sequences = renderV2Sequences;

function readSequenceFromState(arr, fallbackArr) {
    if (!Array.isArray(arr) || arr.length === 0) return fallbackArr;
    return arr.map(step => {
        if (step.type === 'HOLD') {
            return { type: 'HOLD', val_num: parseInt(step.val) || 5 };
        }
        if (step.type === 'PIN') {
            return { type: 'PIN', val_str: String(step.val || '1234').trim() || '1234' };
        }
        if (step.type === 'KEY') {
            return { type: 'KEY', val_str: String(step.val || 'K1_ON').trim() || 'K1_ON' };
        }
        if (step.type === 'RFID') {
            const rfidMode = step.rfidMode || 'TEAM';
            if (rfidMode === 'TEAM') {
                return { type: 'RFID', rfid_mode: 'TEAM', val_str: 'TEAM' };
            }
            if (rfidMode === 'PLAYERS') {
                const uids = Array.isArray(step.rfidPlayers) ? step.rfidPlayers : [];
                const names = uids.map(u => {
                    const p = (typeof globalPlayersDB !== 'undefined' && Array.isArray(globalPlayersDB)) ? globalPlayersDB.find(x => x.uid === u) : null;
                    return p ? p.alias : u;
                });
                return { type: 'RFID', rfid_mode: 'PLAYERS', val_str: uids.join(','), player_names: names };
            }
            if (rfidMode === 'CUSTOM') {
                return { type: 'RFID', rfid_mode: 'CUSTOM', val_str: String(step.rfidCustomUid || '').trim().toUpperCase() };
            }
            return { type: 'RFID', rfid_mode: 'ANY', val_str: '*' };
        }
        return { type: step.type };
    });
}

function buildMissionConfig() {
    const mode = document.getElementById('setup-mode-select').value;
    let targetId = document.getElementById('setup-target-select').value;
    let commandData = {};
    let dur = 15;

    if (mode === 'sd') {
        dur = parseInt(document.getElementById('sd-game-dur').value) || 20;
        let isV2 = false;

        if (targetId) {
            const device = currentDevices.find(d => d.id === targetId);
            if ((device && device.version && device.version.includes("V2")) || targetId.includes("V2")) {
                isV2 = true;
            }
        }

        if (isV2) {
            const armSeqPayload = readSequenceFromState(window.v2ArmSequence, [{ type: 'HOLD', val_num: 5 }]);
            const defuseSeqPayload = readSequenceFromState(window.v2DefuseSequence, [{ type: 'HOLD', val_num: 10 }]);

            commandData = {
                cmd: "SET_SD_SETTINGS",
                attacker: document.getElementById('sd-attacker-team').value,
                game_duration: dur,
                bomb_time: parseInt(document.getElementById('sd-bomb-time').value) || 10,
                fx_smoke: document.getElementById('sd-v2-fx-smoke').value === 'true',
                fx_relay: document.getElementById('sd-v2-fx-relay').value === 'true',
                arm_seq: armSeqPayload,
                defuse_seq: defuseSeqPayload
            };
        } else {
            commandData = {
                cmd: "SET_SD_SETTINGS",
                attacker: document.getElementById('sd-attacker-team').value,
                game_duration: dur,
                bomb_time: parseInt(document.getElementById('sd-bomb-time').value) || 10,
                arm_time: parseInt(document.getElementById('sd-arm-time').value) || 5,
                defuse_time: parseInt(document.getElementById('sd-defuse-time').value) || 10,
                use_arm_pin: document.getElementById('sd-use-arm-pin').value === 'true',
                use_defuse_pin: document.getElementById('sd-use-defuse-pin').value === 'true',
                arm_pin: document.getElementById('sd-arm-pin').value,
                defuse_pin: document.getElementById('sd-defuse-pin').value
            };
        }
    } else if (mode === 'dom') {
        dur = parseInt(document.getElementById('dom-game-dur').value) || 15;
        commandData = {
            cmd: "SET_DOM_SETTINGS",
            duration: dur,
            capture_time: parseInt(document.getElementById('dom-capture-time').value),
            countdown: parseInt(document.getElementById('dom-countdown').value)
        };
    } else if (mode === 'tdm') {
        dur = parseInt(document.getElementById('tdm-game-dur').value) || 15;
        commandData = { cmd: "SET_TDM_SETTINGS", duration: dur };
        targetId = "SERVER-TDM";
    }

    return { mode, targetId, dur, commandData };
}

// ============================================================================
// INIZIALIZZAZIONE EVENTI DOM E LISTENER SOCKET.IO (GUARDIA SINGLETON)
// ============================================================================

if (!window.__zuluMainInitialized) {
    window.__zuluMainInitialized = true;

    document.addEventListener('DOMContentLoaded', () => {
        updateClock();
        resetMonitorLayout();
        renderV2Sequences();

        window.showView = (view) => showView(view, socket);
        window.clearActiveRoster = () => {
            if (confirm('ATTENZIONE: Rimuovere tutti gli operatori dal roster di oggi?')) {
                socket.emit('clear_roster');
            }
        };

        window.sendEnvCommand = (envCmd) => socket.emit('send_command', { target_id: "BROADCAST_ENV", command: { cmd: envCmd } });
        window.triggerServerReset = () => {
            if (confirm("ATTENZIONE CRITICA!\nVuoi forzare il riavvio del server Python?")) {
                socket.emit('server_reset_request');
            }
        };

        window.openFactionModal = function(teamId) {
            const f = currentFactions[teamId];
            document.getElementById('edit-faction-id').value = teamId;
            document.getElementById('edit-faction-name').value = f.name;
            document.getElementById('edit-faction-color').value = f.color;
            document.getElementById('edit-faction-wins').value = f.daily_wins || 0;
            document.getElementById('modal-faction-title').textContent = `EDIT ${teamId} SETTINGS`;
            document.getElementById('faction-edit-modal').classList.remove('hidden');
        };

        window.closeFactionModal = function() {
            document.getElementById('faction-edit-modal').classList.add('hidden');
        };

        window.resetFactionColor = function() {
            const teamId = document.getElementById('edit-faction-id').value;
            const colorInput = document.getElementById('edit-faction-color');
            if (teamId === 'ALPHA') colorInput.value = '#ff3333';
            if (teamId === 'BRAVO') colorInput.value = '#55ff55';
        };

        window.saveFactionConfig = function() {
            const teamId = document.getElementById('edit-faction-id').value;
            const newName = document.getElementById('edit-faction-name').value.trim().toUpperCase();
            const newColor = document.getElementById('edit-faction-color').value;
            const newWins = parseInt(document.getElementById('edit-faction-wins').value) || 0;

            if (newName !== '') {
                const data = {};
                if (teamId === 'ALPHA') {
                    data.alpha_name = newName;
                    data.alpha_color = newColor;
                    data.alpha_wins = newWins;
                } else if (teamId === 'BRAVO') {
                    data.bravo_name = newName;
                    data.bravo_color = newColor;
                    data.bravo_wins = newWins;
                }
                socket.emit('update_factions', data);
                closeFactionModal();
            }
        };

        const btnSaveAlias = document.getElementById('save-alias-btn');
        if (btnSaveAlias) {
            btnSaveAlias.addEventListener('click', () => {
                const aliasInput = document.getElementById('insp-alias-input').value.trim();
                if (activeInspectorId && aliasInput) {
                    socket.emit('rename_device', { id: activeInspectorId, name: aliasInput });
                    document.getElementById('inspector-title').textContent = `${aliasInput} // CONFIG`;
                }
            });
        }

        const btnOta = document.getElementById('ota-update-btn');
        if (btnOta) {
            btnOta.addEventListener('click', () => {
                if (activeInspectorId && confirm(`Avviare l'aggiornamento firmware OTA su ${activeInspectorId}? Il terminale si riavvierà automaticamente al termine.`)) {
                    socket.emit('send_command', { target_id: activeInspectorId, command: { cmd: "TRIGGER_OTA" } });
                    logSystem(`OTA: Inviato comando di aggiornamento a ${activeInspectorId}`);
                }
            });
        }

        const btnHardReset = document.getElementById('hard-reset-btn');
        if (btnHardReset) {
            btnHardReset.addEventListener('click', () => {
                if (activeInspectorId && confirm(`Forzare il riavvio hardware (RESET) di ${activeInspectorId}?`)) {
                    socket.emit('send_command', { target_id: activeInspectorId, command: { cmd: "RESET" } });
                }
            });
        }

        const btnHwTest = document.getElementById('hw-test-btn');
        if (btnHwTest) {
            btnHwTest.addEventListener('click', () => {
                if (!activeInspectorId) return;
                const isTesting = btnHwTest.dataset.testing === "true";
                const cmdToSend = isTesting ? "RETURN_TO_MENU" : "START_HW_TEST";
                socket.emit('send_command', { target_id: activeInspectorId, command: { cmd: cmdToSend } });
                logSystem(`INSPECTOR: Inviato ${cmdToSend} a ${activeInspectorId}`);
            });
        }

        const btnNetTest = document.getElementById('net-test-btn');
        if (btnNetTest) {
            btnNetTest.addEventListener('click', () => {
                if (!activeInspectorId) return;
                const rttEl = document.getElementById('insp-rtt');
                if (rttEl) {
                    rttEl.textContent = "PINGING...";
                    rttEl.style.color = "var(--sop-secondary)";
                }
                socket.emit('send_command', { target_id: activeInspectorId, command: { cmd: "NET_PING_REQ" } });
            });
        }

        socket.on('net_diag_result', (data) => {
            logSystem(`NET TEST [${data.id}]: RTT=${data.rtt_ms}ms | RSSI=${data.rssi}dBm | RAM=${data.heap_kb}KB`);
        });

        socket.on('mission_start_warning', (data) => {
            if (confirm(data.msg)) {
                data.original_request.force = true;
                socket.emit('request_mission_start', data.original_request);
            } else {
                const btnStart = document.getElementById('btn-start-mission');
                if (btnStart) {
                    btnStart.classList.remove('disabled');
                    btnStart.disabled = false;
                }
            }
        });

        const btnTransmit = document.getElementById('btn-transmit-setup');
        if (btnTransmit) {
            btnTransmit.addEventListener('click', () => {
                const { mode, targetId, dur, commandData } = buildMissionConfig();
                socket.emit('send_command', { target_id: targetId, command: commandData });
                socket.emit('send_command', { target_id: "broadcast", command: { cmd: "SET_READY", mode: mode, target_id: targetId, duration: dur } });

                const btnStart = document.getElementById('btn-start-mission');
                if (btnStart) {
                    btnStart.disabled = false;
                    btnStart.classList.remove('disabled');
                }
            });
        }

        const btnStartMission = document.getElementById('btn-start-mission');
        if (btnStartMission) {
            btnStartMission.addEventListener('click', () => {
                const { mode, targetId, dur, commandData } = buildMissionConfig();
                socket.emit('request_mission_start', {
                    mode: mode,
                    target_id: targetId,
                    duration: dur,
                    settings: commandData,
                    force: false
                });
                btnStartMission.disabled = true;
                btnStartMission.classList.add('disabled');
            });
        }

        const modeSelectEl = document.getElementById('setup-mode-select');
        if (modeSelectEl) {
            modeSelectEl.addEventListener('change', (e) => {
                ['setup-sd-params', 'setup-dom-params', 'setup-tdm-params', 'target-selection-row'].forEach(id => {
                    document.getElementById(id)?.classList.add('hidden');
                });
                if (e.target.value === 'sd') document.getElementById('setup-sd-params')?.classList.remove('hidden');
                else if (e.target.value === 'dom') document.getElementById('setup-dom-params')?.classList.remove('hidden');
                else if (e.target.value === 'tdm') document.getElementById('setup-tdm-params')?.classList.remove('hidden');

                if (e.target.value !== 'tdm') {
                    document.getElementById('target-selection-row')?.classList.remove('hidden');
                    if (typeof window.updateUIForTargetVersion === 'function') window.updateUIForTargetVersion();
                }
            });
        }

        setInterval(() => {
            if (socket.connected && socket.io && socket.io.engine) {
                const rtt = socket.io.engine.lastPingTimestamp ? (Date.now() - socket.io.engine.lastPingTimestamp) : 12;
                const loadEl = document.getElementById('net-load');
                if (loadEl) {
                    loadEl.textContent = `${Math.round(rtt / 5) * 5} ms`;
                    loadEl.style.color = (rtt < 50) ? '#55ff55' : (rtt < 150 ? '#ff9900' : 'var(--sop-alert)');
                }
            }
        }, 2500);
    });

    // ========================================================================
    // HANDLER EVENTI SOCKET.IO DAL SERVER
    // ========================================================================

    socket.on('connect', () => {
        logSystem("LINK ESTABLISHED WITH SOP SERVER.");
        const elGlobalStatus = document.getElementById('global-status');
        if (elGlobalStatus) {
            elGlobalStatus.textContent = "ONLINE";
            elGlobalStatus.className = "status-value status-normal";
        }
        socket.emit('request_roster');
    });

    socket.on('disconnect', () => {
        logSystem("CONNECTION LOST - RETRYING...");
        const elGlobalStatus = document.getElementById('global-status');
        if (elGlobalStatus) {
            elGlobalStatus.textContent = "OFFLINE";
            elGlobalStatus.className = "status-value status-alert";
        }
    });

    socket.on('server_restarting', () => {
        alert("IL SERVER SI STA RIAVVIANDO. Attendi qualche secondo che il pannello torni online.");
    });

    socket.on('ota_manifest_update', (manifest) => {
        window.__zuluOtaManifest = manifest || {};
        refreshActiveInspector();
    });

    socket.on('factions_update', (factions) => {
        Object.assign(currentFactions, factions);
        applyFactionStyles(currentFactions);

        renderArenaRoster(arenaRoster, socket);
        if (typeof globalPlayersDB !== 'undefined' && globalPlayersDB.length > 0) {
            renderDatabase(globalPlayersDB, socket, arenaRoster);
        }

        updateMonitorDailyWins(currentFactions);

        const attackerSelect = document.getElementById('sd-attacker-team');
        if (attackerSelect) {
            Array.from(attackerSelect.options).forEach(opt => {
                if (opt.value === 'ALPHA') opt.textContent = currentFactions.ALPHA.name;
                if (opt.value === 'BRAVO') opt.textContent = currentFactions.BRAVO.name;
            });
        }

        const setEl = (id, txt) => { const el = document.getElementById(id); if (el) el.textContent = txt; };
        setEl('setup-label-alpha',  `TEAM ${currentFactions.ALPHA.name}`);
        setEl('setup-label-bravo',  `TEAM ${currentFactions.BRAVO.name}`);
        setEl('monitor-name-alpha', currentFactions.ALPHA.name);
        setEl('monitor-name-bravo', currentFactions.BRAVO.name);
        setEl('dom-lbl-alpha',      currentFactions.ALPHA.name);
        setEl('dom-lbl-bravo',      currentFactions.BRAVO.name);
    });

    const tacticalWeatherIcons = {
        "CLEAR": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><circle cx='12' cy='12' r='5'/><line x1='12' y1='1' x2='12' y2='3'/><line x1='12' y1='21' x2='12' y2='23'/><line x1='4.22' y1='4.22' x2='5.64' y2='5.64'/><line x1='18.36' y1='18.36' x2='19.78' y2='19.78'/><line x1='1' y1='12' x2='3' y2='12'/><line x1='21' y1='12' x2='23' y2='12'/><line x1='4.22' y1='19.78' x2='5.64' y2='18.36'/><line x1='19.780001' y1='4.2199998' x2='18.360001' y2='5.6399999'/></svg>",
        "CLOUDS": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><path d='M18 10h-1.26A8 8 0 1 0 9 20h9a5 5 0 0 0 0-10z'/></svg>",
        "FOG": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><line x1='6.9733591' y1='12.782726' x2='22.97336' y2='12.782726'/><line x1='6.9733591' y1='16.782726' x2='22.97336' y2='16.782726'/><path d='M17.5 10A8 8 0 1 0 9 20'/></svg>",
        "RAIN": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><path d='M 18,8 H 16.74 A 8,8 0 1 0 9,18 h 9 A 5,5 0 0 0 18,8 Z'/><line x1='8' y1='21' x2='8' y2='23'/><line x1='12' y1='21' x2='12' y2='23'/><line x1='16' y1='21' x2='16' y2='23'/></svg>",
        "SHOWERS": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><path d='M 18.007482,7.0153147 H 16.749269 C 15.088764,0.57508049 6.7622439,-1.1408152 2.6970553,4.1188282 -1.3681334,9.3784716 2.378242,17.020275 9.0202494,17.015315 h 8.9872326 c 2.669447,-3.78e-4 4.849354,-2.098584 4.985456,-4.737915 2.3e-5,-4.52e-4 4.7e-5,-9.03e-4 7e-5,-0.0014 0.0044,-0.08635 0.0067,-0.173278 0.0067,-0.260731 0,-2.7611478 -2.234966,-4.9996098 -4.992198,-5.0000003 z' style='stroke-width:2'/><line x1='8.0362864' y1='19.172985' x2='8.0362864' y2='21.172985'/><line x1='10.585787' y1='20.74567' x2='10.585787' y2='22.74567'/><line x1='13.215767' y1='19.165728' x2='13.215767' y2='21.165728'/><line x1='15.679379' y1='20.76235' x2='15.679379' y2='22.76235'/><line x1='18.204504' y1='19.491768' x2='18.204504' y2='21.491768'/></svg>",
        "STORM": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><path d='m 18,17.000024 c 2.761426,0 4.999999,-2.238577 4.999999,-5.000001 C 22.999999,9.2385988 20.761426,7.000022 18,7.000022 H 16.74 C 15.077135,0.55978844 6.7387845,-1.1561056 2.6678211,4.1035381 -1.4031423,9.3631818 3.1506129,15.462568 6.9845758,16.321361'/><path style='fill:none;stroke-width:1.4;stroke-dasharray:none' d='M 8.8109392,15.596602 13.120172,8.2949509 v 7.3016511 h 3.567373 l -5.345876,7.037039 v -7.037039 z'/></svg>",
        "SNOW": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><line x1='12' y1='2' x2='12' y2='22'/><line x1='2.93' y1='11.93' x2='21.07' y2='11.93'/><line x1='4.93' y1='4.93' x2='19.07' y2='19.07'/><line x1='4.93' y1='19.07' x2='19.07' y2='4.93'/></svg>",
        "DEFAULT": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><circle cx='12' cy='12' r='10'/><line x1='12' y1='8' x2='12' y2='12'/><line x1='12' y1='16' x2='12.01' y2='16'/></svg>",
        "CLEAR_NIGHT": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><path d='M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z'/></svg>",
        "CLOUDS_NIGHT": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><defs><linearGradient id='swatch8'><stop style='stop-color:%23000000;stop-opacity:1;' offset='0'/></linearGradient><linearGradient xlink:href='%23swatch8' id='linearGradient8' x1='2.4719927' y1='15.843504' x2='23.939306' y2='15.843504' gradientUnits='userSpaceOnUse'/></defs><path d='M 19.107969,10.9391 A 9,9 0 1 1 9.3179692,1.1491003 7,7 0 0 0 19.107969,10.9391 Z'/><path d='m 18.512775,14.062746 h -1.115487 a 7.0824601,7.0539288 0 1 0 -6.85228,8.817411 h 7.967767 a 4.4265377,4.4087055 0 0 0 0,-8.817411 z' style='mix-blend-mode:normal;fill:url(%23linearGradient8);fill-opacity:1;stroke-width:1.99999'/></svg>"
    };

    socket.on('weather_update', (data) => {
        if (!data.weather) return;
        const elDisplay = document.getElementById('weather-display');
        const elIcon = document.getElementById('weather-icon');

        const parts = data.weather.split(' // ');
        if (parts.length === 2) {
            const temp = parts[0].trim();
            const condition = parts[1].trim();
            if (elIcon) elIcon.src = tacticalWeatherIcons[condition] || tacticalWeatherIcons["DEFAULT"];
            if (elDisplay) elDisplay.textContent = `${temp} // ${condition.replace('_NIGHT', '')}`;
        } else if (elDisplay) {
            elDisplay.textContent = data.weather.replace('_NIGHT', '');
        }
    });

    socket.on('roster_update', (rosterData) => {
        arenaRoster = rosterData;
        renderArenaRoster(arenaRoster, socket);
    });

    socket.on('database_update', (players) => renderDatabase(players, socket, arenaRoster));
    socket.on('profile_data', (data) => populateProfileModal(data));
    socket.on('devices_update', (devices) => {
        currentDevices = devices;
        updateDeviceList(devices, socket, (id) => monitoredDeviceId = id);
        updateSetupDropdown();
    });

    socket.on('mission_start_success', () => showView('monitor', socket));

    socket.on('mission_start_error', (data) => {
        alert(data.msg);
        const btnStart = document.getElementById('btn-start-mission');
        if (btnStart) {
            btnStart.classList.remove('disabled');
            btnStart.disabled = false;
        }
    });

    socket.on('esp_event', (msg) => {
        const raw = msg.parsed_data || {};
        const type = raw.type || 'UNKNOWN';
        const payload = raw.payload || {};

        if (type === 'RFID_SCAN') {
            const statusStr = payload.accepted ? 'AUTORIZZATA' : 'RIFIUTATA';
            logSystem(`RAT RFID [${raw.id || 'RAT'}]: ${payload.alias || payload.uid} (${statusStr})`);
            return;
        }

        let mode = payload.mode || raw.mode;
        if (!mode) {
            if (type === 'SD_UPDATE') mode = 'SEARCH_AND_DESTROY';
            else if (type === 'DOM_UPDATE') mode = 'DOMINATION';
            else if (type === 'TDM_UPDATE') mode = 'TEAM_DEATHMATCH';
        }

        if (type === 'MODE_EXIT') { resetMonitorLayout(); return; }
        if (type === 'MODE_ENTER') { resetMonitorLayout(); }
        if (type === 'SETTINGS_UPDATE' || (type === 'MODE_ENTER' && payload.bomb_time)) {
            renderRules(payload);
        }

        updateUniversalFeed(payload, mode, type);
    });

    // ========================================================================
    // AZIONI GLOBALI OPERATORE, FOTO PROFILO/PROP E REGISTRAZIONE TESSERE
    // ========================================================================

    window.uiForceEndMission = function() {
        const btn = document.getElementById('btn-force-end');
        const action = btn ? (btn.dataset.action || "FORCE_END_GAME") : "FORCE_END_GAME";
        socket.emit('send_command', { target_id: "SERVER-ROUTER", command: { cmd: action } });
        if (action === "CLOSE_MISSION") {
            resetMonitorLayout();
        }
    };

    window.saveOperatorNotes = function() {
        const uid = getCurrentProfileUid();
        const notes = document.getElementById('modal-op-notes').value;
        if (uid) socket.emit('save_notes', { uid: uid, notes: notes });
        alert("Annotazioni tattiche registrate.");
    };

    window.resetOperatorStats = function() {
        if (confirm("ATTENZIONE CRITICA: Vuoi azzerare definitivamente le partite e le vittorie di questo operatore?")) {
            socket.emit('update_db_player', { uid: getCurrentProfileUid(), action: 'reset_stats' });
            window.closeModal();
        }
    };

    window.modKarma = function(val) {
        const el = document.getElementById('modal-op-karma');
        if (!el) return;
        let currentKarma = (parseInt(el.textContent) || 0) + val;
        el.textContent = currentKarma;

        const badge = el.parentElement;
        badge.classList.remove('positive', 'negative');
        if (currentKarma < 0) badge.classList.add('negative');
        else if (currentKarma > 0) badge.classList.add('positive');

        socket.emit('update_karma', { uid: getCurrentProfileUid(), karma: currentKarma });
    };

    window.renameFromProfile = function() {
        const uid = getCurrentProfileUid();
        const currentNameText = document.getElementById('modal-op-name-text').innerText;
        const currentName = currentNameText.replace('PROFILE // ', '');
        const newName = prompt("Inserisci nuovo nome operatore:", currentName);

        if (newName && newName.trim() !== '') {
            socket.emit('update_db_player', { uid: uid, action: 'rename', value: newName.trim().toUpperCase() });
            setTimeout(() => socket.emit('request_profile', { uid: uid }), 100);
        }
    };

    window.editAsdFromProfile = function() {
        const uid = getCurrentProfileUid();
        const currentAsd = document.getElementById('modal-op-asd').innerText;
        const newAsd = prompt("Inserisci il nome della SQUADRA o dell'ASD (lascia vuoto per 'NESSUNA'):", currentAsd === 'NESSUNA' ? '' : currentAsd);

        if (newAsd !== null) {
            const finalAsd = newAsd.trim() === '' ? 'NESSUNA' : newAsd.trim().toUpperCase();
            socket.emit('update_db_player', { uid: uid, action: 'edit_asd', value: finalAsd });
            setTimeout(() => socket.emit('request_profile', { uid: uid }), 100);
        }
    };

    window.toggleProStatus = function() {
        const uid = getCurrentProfileUid();
        if (confirm("Vuoi cambiare il tipo di account (PRO / GUEST) per questa tessera?")) {
            socket.emit('update_db_player', { uid: uid, action: 'toggle_pro' });
            setTimeout(() => socket.emit('request_profile', { uid: uid }), 100);
        }
    };

    window.purgeGuests = function() {
        if (confirm("ATTENZIONE: Verranno azzerate statistiche, note, karma e foto di TUTTE le tessere GUEST. Il nome tornerà quello di default (OP-X). Continuare?")) {
            socket.emit('purge_guests');
            alert("Operazione di pulizia completata. Le tessere GUEST sono pronte per il prossimo evento.");
        }
    };

    window.resetDailyWins = function() {
        if (confirm("ATTENZIONE: Vuoi azzerare i contatori delle vittorie odierne per ENTRAMBE le Fazioni?")) {
            socket.emit('reset_daily_wins');
        }
    };

    let cropTargetType = 'operator';

    window.handlePhotoUpload = function(input) {
        if (input.files && input.files[0]) {
            cropTargetType = 'operator';
            const reader = new FileReader();
            reader.onload = function(e) {
                const img = document.getElementById('image-to-crop');
                img.src = e.target.result;

                document.getElementById('operator-modal').classList.add('hidden');
                document.getElementById('crop-modal').classList.remove('hidden');

                if (currentCropper) currentCropper.destroy();

                currentCropper = new Cropper(img, {
                    aspectRatio: 280 / 300,
                    viewMode: 1,
                    background: false,
                    zoomable: true,
                    autoCropArea: 1
                });
            };
            reader.readAsDataURL(input.files[0]);
            input.value = '';
        }
    };

    window.handleDevicePhotoUpload = function(input) {
        if (input.files && input.files[0] && activeInspectorId) {
            cropTargetType = 'device';
            const reader = new FileReader();
            reader.onload = function(e) {
                const img = document.getElementById('image-to-crop');
                img.src = e.target.result;

                document.getElementById('crop-modal').classList.remove('hidden');

                if (currentCropper) currentCropper.destroy();

                currentCropper = new Cropper(img, {
                    aspectRatio: 1,
                    viewMode: 1,
                    background: false,
                    zoomable: true,
                    autoCropArea: 1
                });
            };
            reader.readAsDataURL(input.files[0]);
            input.value = '';
        }
    };

    window.deleteDevicePhoto = function() {
        if (activeInspectorId && confirm("Vuoi rimuovere la foto associata a questo dispositivo?")) {
            socket.emit('delete_device_photo', { id: activeInspectorId });
        }
    };

    window.closeCropModal = function() {
        document.getElementById('crop-modal').classList.add('hidden');
        if (cropTargetType === 'operator') {
            document.getElementById('operator-modal').classList.remove('hidden');
        }
        if (currentCropper) {
            currentCropper.destroy();
            currentCropper = null;
        }
    };

    window.confirmCrop = function() {
        if (!currentCropper) return;

        const isDevice = (cropTargetType === 'device');
        const targetW = isDevice ? 300 : 280;
        const targetH = 300;

        currentCropper.getCroppedCanvas({
            width: targetW,
            height: targetH,
            imageSmoothingEnabled: true,
            imageSmoothingQuality: 'high'
        }).toBlob((blob) => {
            const formData = new FormData();
            formData.append('photo', blob, 'cropped.jpg');

            if (isDevice) {
                formData.append('device_id', activeInspectorId);
                fetch('/upload_device_photo', { method: 'POST', body: formData })
                    .then(response => response.json())
                    .then(() => closeCropModal());
            } else {
                formData.append('uid', getCurrentProfileUid());
                fetch('/upload_photo', { method: 'POST', body: formData })
                    .then(response => response.json())
                    .then(data => {
                        if (data.status === 'ok') {
                            document.getElementById('modal-op-photo').src = `/static/uploads/${data.filename}?t=${new Date().getTime()}`;
                        }
                        closeCropModal();
                    });
            }
        }, 'image/jpeg', 0.9);
    };

    window.openRegistrationModal = function() {
        document.getElementById('reg-uid').value = '';

        let maxOp = 0;
        if (globalPlayersDB) {
            globalPlayersDB.forEach(p => {
                const nameToCheck = p.alias;
                if (nameToCheck && nameToCheck.startsWith('OP-')) {
                    const num = parseInt(nameToCheck.replace('OP-', ''));
                    if (!isNaN(num) && num > maxOp) maxOp = num;
                }
            });
        }

        document.getElementById('reg-alias').value = `OP-${maxOp + 1}`;
        document.getElementById('reg-pro').value = '0';
        document.getElementById('reg-asd').value = '';
        document.getElementById('registration-modal').classList.remove('hidden');
    };

    window.closeRegistrationModal = function() {
        document.getElementById('registration-modal').classList.add('hidden');
    };

    window.submitRegistration = function() {
        const uid = document.getElementById('reg-uid').value.trim();
        const alias = document.getElementById('reg-alias').value.trim();
        const isPro = document.getElementById('reg-pro').value;
        const tagInfo = document.getElementById('reg-tag-info').value.trim();
        const asdTeam = document.getElementById('reg-asd').value.trim();

        if (!uid || !alias) {
            alert("Attendi la scansione dell'UID sul Kiosk e inserisci un Nome.");
            return;
        }

        socket.emit('register_player_manual', {
            uid: uid,
            alias: alias,
            is_pro: parseInt(isPro),
            tag_info: tagInfo,
            asd_team: asdTeam === '' ? 'NESSUNA' : asdTeam
        });

        closeRegistrationModal();
    };

    window.deleteOperatorPhoto = function() {
        const uid = getCurrentProfileUid();
        if (uid && confirm("Vuoi eliminare la foto profilo di questo operatore?")) {
            socket.emit('update_db_player', { uid: uid, action: 'delete_photo' });
            setTimeout(() => socket.emit('request_profile', { uid: uid }), 100);
        }
    };

    socket.on('raw_tag_scanned', (data) => {
        const modal = document.getElementById('registration-modal');
        if (modal && !modal.classList.contains('hidden')) {
            const uidInput = document.getElementById('reg-uid');
            if (uidInput) {
                uidInput.value = data.uid;
                uidInput.style.color = 'var(--sop-success)';
                logSystem(`KIOSK: Intercettato nuovo TAG [${data.uid}]`);
            }
        }
    });

    document.body.addEventListener('input', (e) => {
        if (e.target.id === 'db-search' && typeof globalPlayersDB !== 'undefined' && globalPlayersDB.length > 0) {
            renderDatabase(globalPlayersDB, socket, arenaRoster);
        }
    });

    document.body.addEventListener('change', (e) => {
        if ((e.target.id === 'db-filter' || e.target.id === 'db-sort') && typeof globalPlayersDB !== 'undefined' && globalPlayersDB.length > 0) {
            renderDatabase(globalPlayersDB, socket, arenaRoster);
        }
    });
}