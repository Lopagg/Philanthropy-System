/**
 * @file main.js
 * @brief Entry point principale del Frontend Web Zulu Game System.
 *
 * Gestisce:
 * - Connessione singleton Socket.IO e sincronizzazione real-time con il server Flask.
 * - Costruttore visuale delle sequenze modulari V2 (HOLD, PIN, KEY a 8 modalità, RFID a 4 modalità) per S&D e Dominio.
 * - Validazione automatica di coerenza per lo stato delle chiavi K1/K2.
 * - Regia Audio Tattica (Soundboard Micro-SD, Annunciatore TTS Live e Intercom Push-To-Talk).
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

window.v2ArmSequence      = window.v2ArmSequence      || [{ type: 'HOLD', val: 5,  rfidMode: 'TEAM', rfidPlayers: [], rfidCustomUid: '' }];
window.v2DefuseSequence   = window.v2DefuseSequence   || [{ type: 'HOLD', val: 10, rfidMode: 'TEAM', rfidPlayers: [], rfidCustomUid: '' }];
window.v2DomAlphaSequence = window.v2DomAlphaSequence || [{ type: 'HOLD', val: 10, rfidMode: 'TEAM', rfidPlayers: [], rfidCustomUid: '' }];
window.v2DomBravoSequence = window.v2DomBravoSequence || [{ type: 'HOLD', val: 10, rfidMode: 'TEAM', rfidPlayers: [], rfidCustomUid: '' }];

function getSeqArrayByType(type) {
    if (type === 'arm')       return window.v2ArmSequence;
    if (type === 'defuse')    return window.v2DefuseSequence;
    if (type === 'dom_alpha') return window.v2DomAlphaSequence;
    if (type === 'dom_bravo') return window.v2DomBravoSequence;
    return window.v2ArmSequence;
}

window.copyDomAlphaToBravo = function() {
    window.v2DomBravoSequence = JSON.parse(JSON.stringify(window.v2DomAlphaSequence));
    renderV2Sequences();
};

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
    const modeSelect = document.getElementById('setup-mode-select');
    if (!targetSelect || !modeSelect) return;

    const targetId = targetSelect.value;
    let isV2 = false;
    if (targetId) {
        const device = currentDevices.find(d => d.id === targetId);
        if ((device && device.version && device.version.includes("V2")) || targetId.includes("V2")) {
            isV2 = true;
        }
    }

    const sdV1  = document.getElementById('sd-v1-legacy-params');
    const sdV2  = document.getElementById('sd-v2-modular-params');
    const domV1 = document.getElementById('dom-v1-legacy-params');
    const domV2 = document.getElementById('dom-v2-modular-params');

    if (sdV1 && sdV2) {
        sdV1.classList.toggle('hidden', isV2);
        sdV2.classList.toggle('hidden', !isV2);
    }
    if (domV1 && domV2) {
        domV1.classList.toggle('hidden', isV2);
        domV2.classList.toggle('hidden', !isV2);
    }

    if (isV2) {
        renderV2Sequences();
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

function sanitizeChain(stepsArray) {
    const state = { k1: 'FREE', k2: 'FREE' };
    stepsArray.forEach(step => {
        if (step.type !== 'KEY') return;
        step.allowedKeyModes = KEY_MODES_META.filter(m => isKeyModeAllowed(m, state)).map(m => m.value);
        if (!step.val || !step.allowedKeyModes.includes(step.val)) {
            step.val = step.allowedKeyModes[0] || 'K1_ON';
        }
        applyKeyModeState(step.val, state);
    });
}

function sanitizeKeySequences() {
    sanitizeChain([...window.v2ArmSequence, ...window.v2DefuseSequence]);
    sanitizeChain(window.v2DomAlphaSequence);
    sanitizeChain(window.v2DomBravoSequence);
}

window.addSequenceStep = function(type) {
    const arr = getSeqArrayByType(type);
    if (arr.length >= 5) {
        alert("Massimo 5 step consentiti per sequenza.");
        return;
    }
    const defaultHold = (type === 'arm') ? 5 : 10;
    arr.push({ type: 'HOLD', val: defaultHold, rfidMode: 'TEAM', rfidPlayers: [], rfidCustomUid: '' });
    renderV2Sequences();
};

window.removeSequenceStep = function(type, index) {
    const arr = getSeqArrayByType(type);
    arr.splice(index, 1);
    if (arr.length === 0) {
        const defaultHold = (type === 'arm') ? 5 : 10;
        arr.push({ type: 'HOLD', val: defaultHold, rfidMode: 'TEAM', rfidPlayers: [], rfidCustomUid: '' });
    }
    renderV2Sequences();
};

window.moveSequenceStep = function(type, index, direction) {
    const arr = getSeqArrayByType(type);
    const targetIndex = index + direction;
    if (targetIndex < 0 || targetIndex >= arr.length) return;
    const temp = arr[index];
    arr[index] = arr[targetIndex];
    arr[targetIndex] = temp;
    renderV2Sequences();
};

window.updateSequenceStepType = function(type, index, selectElement) {
    const arr = getSeqArrayByType(type);
    arr[index].type = selectElement.value;
    if (arr[index].type === 'HOLD') arr[index].val = (type === 'arm') ? 5 : 10;
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
    const arr = getSeqArrayByType(type);
    arr[index].val = inputElement.value;
    if (arr[index].type === 'KEY') {
        renderV2Sequences();
    }
};

window.updateRfidStepMode = function(type, index, selectElement) {
    const arr = getSeqArrayByType(type);
    arr[index].rfidMode = selectElement.value;
    renderV2Sequences();
};

window.addRfidPlayer = function(type, index, selectElement) {
    const uid = selectElement.value;
    if (!uid) return;
    const arr = getSeqArrayByType(type);
    if (!Array.isArray(arr[index].rfidPlayers)) arr[index].rfidPlayers = [];
    if (!arr[index].rfidPlayers.includes(uid)) {
        arr[index].rfidPlayers.push(uid);
    }
    renderV2Sequences();
};

window.removeRfidPlayer = function(type, index, uid) {
    const arr = getSeqArrayByType(type);
    if (!Array.isArray(arr[index].rfidPlayers)) return;
    arr[index].rfidPlayers = arr[index].rfidPlayers.filter(u => u !== uid);
    renderV2Sequences();
};

window.updateRfidCustomUid = function(type, index, inputElement) {
    const arr = getSeqArrayByType(type);
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
    const armContainer      = document.getElementById('v2-arm-sequence-container');
    const defuseContainer   = document.getElementById('v2-defuse-sequence-container');
    const domAlphaContainer = document.getElementById('v2-dom-alpha-sequence-container');
    const domBravoContainer = document.getElementById('v2-dom-bravo-sequence-container');

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
                const roleLabel = typeName === 'arm' ? 'SQUADRA ATTACCANTE'
                                : (typeName === 'defuse' ? 'SQUADRA DIFENSORE'
                                : (typeName === 'dom_alpha' ? `SQUADRA ${currentFactions.ALPHA.name}` : `SQUADRA ${currentFactions.BRAVO.name}`));

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

    if (armContainer)      armContainer.innerHTML      = generateHtml(window.v2ArmSequence, 'arm');
    if (defuseContainer)   defuseContainer.innerHTML   = generateHtml(window.v2DefuseSequence, 'defuse');
    if (domAlphaContainer) domAlphaContainer.innerHTML = generateHtml(window.v2DomAlphaSequence, 'dom_alpha');
    if (domBravoContainer) domBravoContainer.innerHTML = generateHtml(window.v2DomBravoSequence, 'dom_bravo');
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

    let isV2 = false;
    if (targetId) {
        const device = currentDevices.find(d => d.id === targetId);
        if ((device && device.version && device.version.includes("V2")) || targetId.includes("V2")) {
            isV2 = true;
        }
    }

    if (mode === 'sd') {
        dur = parseInt(document.getElementById('sd-game-dur').value) || 20;

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
                arm_fail_mode: document.getElementById('sd-v2-arm-fail-mode')?.value || 'RETRY_STEP',
                defuse_fail_mode: document.getElementById('sd-v2-defuse-fail-mode')?.value || 'RETRY_STEP',
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

        if (isV2) {
            const alphaSeqPayload = readSequenceFromState(window.v2DomAlphaSequence, [{ type: 'HOLD', val_num: 10 }]);
            const bravoSeqPayload = readSequenceFromState(window.v2DomBravoSequence, [{ type: 'HOLD', val_num: 10 }]);

            commandData = {
                cmd: "SET_DOM_SETTINGS",
                duration: dur,
                game_duration: dur,
                fx_smoke: document.getElementById('dom-v2-fx-smoke')?.value === 'true',
                fx_relay: document.getElementById('dom-v2-fx-relay')?.value === 'true',
                alpha_seq: alphaSeqPayload,
                bravo_seq: bravoSeqPayload
            };
        } else {
            commandData = {
                cmd: "SET_DOM_SETTINGS",
                duration: dur,
                game_duration: dur,
                capture_time: parseInt(document.getElementById('dom-capture-time')?.value) || 10,
                countdown: parseInt(document.getElementById('dom-countdown')?.value) || 10
            };
        }
    } else if (mode === 'tdm') {
        dur = parseInt(document.getElementById('tdm-game-dur').value) || 15;
        commandData = { cmd: "SET_TDM_SETTINGS", duration: dur, game_duration: dur };
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

        window.setAllPlayersInField = () => {
            socket.emit('set_all_players_in');
            logSystem("TRACKING: Tutti gli operatori impostati IN CAMPO.");
        };

        window.sendEnvCommand = (envCmd) => socket.emit('send_command', { target_id: "BROADCAST_ENV", command: { cmd: envCmd } });
        window.triggerServerReset = () => {
            if (confirm("ATTENZIONE CRITICA!\nVuoi forzare il riavvio del server Python?")) {
                socket.emit('server_reset_request');
            }
        };

        // ====================================================================
        // REGIA AUDIO TATTICA: SOUNDBOARD SD, TTS LIVE E INTERCOM PTT
        // ====================================================================

        function setAudioDirectorStatus(msg, color = '#888') {
            const el = document.getElementById('insp-audio-status');
            if (el) {
                el.textContent = msg;
                el.style.color = color;
            }
        }

        window.playSdSound = function(sdPath) {
            if (!activeInspectorId || !sdPath) return;
            socket.emit('send_command', {
                target_id: activeInspectorId,
                command: { cmd: "PLAY_AUDIO", path: sdPath }
            });
            setAudioDirectorStatus(`SD PLAY -> ${sdPath}`, 'var(--sop-primary)');
            logSystem(`AUDIO SD [${activeInspectorId}]: ${sdPath}`);
        };

        window.sendNodeVolume = function(vol) {
            if (!activeInspectorId) return;
            const v = Math.max(0, Math.min(100, parseInt(vol)));
            socket.emit('send_command', {
                target_id: activeInspectorId,
                command: { cmd: "SET_VOLUME", volume: isNaN(v) ? 100 : v }
            });
            setAudioDirectorStatus(`MASTER VOLUME IMPOSTATO A ${v}%`, 'var(--sop-primary)');
        };

        window.sendLiveTts = function() {
            if (!activeInspectorId) return;
            const inputEl = document.getElementById('insp-tts-input');
            const profEl  = document.getElementById('insp-tts-profile');
            const btnEl   = document.getElementById('insp-tts-btn');
            const text = (inputEl?.value || '').trim();
            if (!text) return;

            setAudioDirectorStatus("SINTESI E MASTERING TTS IN CORSO...", "var(--sop-secondary)");
            if (btnEl) btnEl.disabled = true;

            fetch('/api/audio/tts', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    target_id: activeInspectorId,
                    text: text,
                    profile: profEl ? profEl.value : 'COMMANDER'
                })
            })
            .then(r => r.json())
            .then(data => {
                if (data.status === 'ok') {
                    setAudioDirectorStatus(`TTS TRASMESSO: "${text}"`, 'var(--sop-success)');
                    logSystem(`TTS LIVE [${activeInspectorId}]: "${text}"`);
                    if (inputEl) inputEl.value = '';
                } else {
                    setAudioDirectorStatus(`ERRORE TTS: ${data.msg || 'Fallito'}`, 'var(--sop-alert)');
                }
            })
            .catch(err => setAudioDirectorStatus(`ERRORE RETE TTS: ${err}`, 'var(--sop-alert)'))
            .finally(() => { if (btnEl) btnEl.disabled = false; });
        };

        function transmitIntercomBlob(blob, filename = 'ptt.webm') {
            if (!activeInspectorId || !blob || blob.size < 200) {
                setAudioDirectorStatus("AUDIO TROPPO BREVE - ANNULLATO", "#888");
                return;
            }
            setAudioDirectorStatus("MASTERING WALKIE-TALKIE E INVIO AL RAT...", "var(--sop-secondary)");
            const formData = new FormData();
            formData.append('target_id', activeInspectorId);
            formData.append('audio', blob, filename);

            fetch('/api/audio/intercom', { method: 'POST', body: formData })
                .then(r => r.json())
                .then(data => {
                    if (data.status === 'ok') {
                        setAudioDirectorStatus("INTERCOM VOCALE RIPRODOTTO SUL RAT!", "var(--sop-success)");
                        logSystem(`INTERCOM PTT trasmesso a ${activeInspectorId}`);
                    } else {
                        setAudioDirectorStatus(`ERRORE INTERCOM: ${data.msg}`, "var(--sop-alert)");
                    }
                })
                .catch(err => setAudioDirectorStatus(`ERRORE UPLOAD PTT: ${err}`, "var(--sop-alert)"));
        }

        window.uploadIntercomFileFallback = function(input) {
            if (input.files && input.files[0]) {
                transmitIntercomBlob(input.files[0], input.files[0].name);
                input.value = '';
            }
        };

        let pttRecorder = null;
        let pttChunks = [];
        let pttStream = null;
        const pttBtn = document.getElementById('insp-ptt-btn');

        async function startPttRecording(e) {
            if (e) e.preventDefault();
            if (!activeInspectorId) return;
            if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
                setAudioDirectorStatus("Su HTTP non-sicuro usa il tasto [📱 VOCALE MOBILE] a destra.", "var(--sop-secondary)");
                return;
            }
            try {
                pttStream = await navigator.mediaDevices.getUserMedia({ audio: true });
                pttChunks = [];
                pttRecorder = new MediaRecorder(pttStream);
                pttRecorder.ondataavailable = (ev) => {
                    if (ev.data && ev.data.size > 0) pttChunks.push(ev.data);
                };
                pttRecorder.onstop = () => {
                    const mime = pttRecorder.mimeType || 'audio/webm';
                    const ext = mime.includes('mp4') ? 'm4a' : (mime.includes('ogg') ? 'ogg' : 'webm');
                    const blob = new Blob(pttChunks, { type: mime });
                    if (pttStream) {
                        pttStream.getTracks().forEach(t => t.stop());
                        pttStream = null;
                    }
                    transmitIntercomBlob(blob, `ptt.${ext}`);
                };
                pttRecorder.start();
                if (pttBtn) {
                    pttBtn.textContent = "🔴 REGISTRAZIONE IN CORSO... RILASCIA PER TRASMETTERE";
                    pttBtn.style.borderColor = "var(--sop-alert)";
                    pttBtn.style.color = "var(--sop-alert)";
                }
                setAudioDirectorStatus("MICROFONO APERTO - PARLA ORA...", "var(--sop-alert)");
            } catch (err) {
                setAudioDirectorStatus(`Accesso microfono negato (${err.message}). Usa [📱 VOCALE MOBILE].`, "var(--sop-alert)");
            }
        }

        function stopPttRecording(e) {
            if (e) e.preventDefault();
            if (pttRecorder && pttRecorder.state === 'recording') {
                pttRecorder.stop();
            }
            if (pttBtn) {
                pttBtn.textContent = "[ 🎙️ TIENI PREMUTO PER PARLARE (PTT WALKIE-TALKIE) ]";
                pttBtn.style.borderColor = "var(--sop-secondary)";
                pttBtn.style.color = "var(--sop-secondary)";
            }
        }

        if (pttBtn) {
            pttBtn.addEventListener('mousedown', startPttRecording);
            pttBtn.addEventListener('mouseup', stopPttRecording);
            pttBtn.addEventListener('mouseleave', stopPttRecording);
            pttBtn.addEventListener('touchstart', startPttRecording, { passive: false });
            pttBtn.addEventListener('touchend', stopPttRecording, { passive: false });
        }

        // ====================================================================
        // GESTIONE FAZIONI, INSPECTOR E MISSION SETUP
        // ====================================================================

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

    // ========================================================================
    // GESTIONE PRESET DI CONFIGURAZIONE MISSIONE (LOAD / SAVE / UPDATE / DEL)
    // ========================================================================

    let missionPresets = [];
    let selectedPresetId = null;

    function setPresetStatus(text, color = '#888') {
        const el = document.getElementById('preset-status-msg');
        if (el) {
            el.textContent = text;
            el.style.color = color;
        }
    }

    function updatePresetActionButtons() {
        const hasSelection = (selectedPresetId !== null);
        document.getElementById('btn-overwrite-preset')?.classList.toggle('hidden', !hasSelection);
        document.getElementById('btn-rename-preset')?.classList.toggle('hidden', !hasSelection);
        document.getElementById('btn-delete-preset')?.classList.toggle('hidden', !hasSelection);
    }

    function renderPresetDropdown() {
        const selectEl = document.getElementById('setup-preset-select');
        if (!selectEl) return;

        selectEl.innerHTML = '<option value="">-- Configurazione Manuale / Seleziona Preset --</option>';
        const modeBadges = { 'sd': 'S&D', 'dom': 'DOM', 'tdm': 'TDM' };

        missionPresets.forEach(p => {
            const opt = document.createElement('option');
            opt.value = String(p.id);
            const badge = modeBadges[p.mode] || p.mode.toUpperCase();
            opt.textContent = `[${badge}] ${p.name}`;
            selectEl.appendChild(opt);
        });

        if (selectedPresetId && missionPresets.some(p => p.id === selectedPresetId)) {
            selectEl.value = String(selectedPresetId);
        } else {
            selectedPresetId = null;
            selectEl.value = "";
        }
        updatePresetActionButtons();
    }

    function extractCurrentSetupPreset() {
        const mode = document.getElementById('setup-mode-select')?.value || 'sd';
        if (mode === 'sd') {
            return {
                attacker:         document.getElementById('sd-attacker-team')?.value || 'ALPHA',
                game_duration:    parseInt(document.getElementById('sd-game-dur')?.value) || 20,
                bomb_time:        parseInt(document.getElementById('sd-bomb-time')?.value) || 10,
                fx_smoke:         document.getElementById('sd-v2-fx-smoke')?.value === 'true',
                fx_relay:         document.getElementById('sd-v2-fx-relay')?.value === 'true',
                arm_fail_mode:    document.getElementById('sd-v2-arm-fail-mode')?.value || 'RETRY_STEP',
                defuse_fail_mode: document.getElementById('sd-v2-defuse-fail-mode')?.value || 'RETRY_STEP',
                v2_arm_seq:       JSON.parse(JSON.stringify(window.v2ArmSequence || [])),
                v2_defuse_seq:    JSON.parse(JSON.stringify(window.v2DefuseSequence || [])),
                arm_time:         parseInt(document.getElementById('sd-arm-time')?.value) || 5,
                defuse_time:      parseInt(document.getElementById('sd-defuse-time')?.value) || 10,
                use_arm_pin:      document.getElementById('sd-use-arm-pin')?.value || 'true',
                use_defuse_pin:   document.getElementById('sd-use-defuse-pin')?.value || 'true',
                arm_pin:          document.getElementById('sd-arm-pin')?.value || '1234',
                defuse_pin:       document.getElementById('sd-defuse-pin')?.value || '4321'
            };
        } else if (mode === 'dom') {
            return {
                game_duration:    parseInt(document.getElementById('dom-game-dur')?.value) || 15,
                fx_smoke:         document.getElementById('dom-v2-fx-smoke')?.value === 'true',
                fx_relay:         document.getElementById('dom-v2-fx-relay')?.value === 'true',
                v2_dom_alpha_seq: JSON.parse(JSON.stringify(window.v2DomAlphaSequence || [])),
                v2_dom_bravo_seq: JSON.parse(JSON.stringify(window.v2DomBravoSequence || [])),
                capture_time:     parseInt(document.getElementById('dom-capture-time')?.value) || 10,
                countdown:        parseInt(document.getElementById('dom-countdown')?.value) || 10
            };
        } else {
            return {
                game_duration:    parseInt(document.getElementById('tdm-game-dur')?.value) || 15
            };
        }
    }

    function applyPresetToUI(preset) {
        if (!preset || !preset.config) return;
        const cfg = preset.config;
        const modeSelect = document.getElementById('setup-mode-select');
        if (modeSelect) {
            modeSelect.value = preset.mode || 'sd';
            modeSelect.dispatchEvent(new Event('change'));
        }

        const setVal = (id, val) => {
            const el = document.getElementById(id);
            if (el && val !== undefined && val !== null) el.value = String(val);
        };

        if (preset.mode === 'sd') {
            setVal('sd-attacker-team',       cfg.attacker || 'ALPHA');
            setVal('sd-game-dur',            cfg.game_duration ?? 20);
            setVal('sd-bomb-time',           cfg.bomb_time ?? 10);
            setVal('sd-v2-fx-smoke',         cfg.fx_smoke ? 'true' : 'false');
            setVal('sd-v2-fx-relay',         cfg.fx_relay ? 'true' : 'false');
            setVal('sd-v2-arm-fail-mode',    cfg.arm_fail_mode || 'RETRY_STEP');
            setVal('sd-v2-defuse-fail-mode', cfg.defuse_fail_mode || 'RETRY_STEP');
            setVal('sd-arm-time',            cfg.arm_time ?? 5);
            setVal('sd-defuse-time',         cfg.defuse_time ?? 10);
            setVal('sd-use-arm-pin',         cfg.use_arm_pin ?? 'true');
            setVal('sd-use-defuse-pin',      cfg.use_defuse_pin ?? 'true');
            setVal('sd-arm-pin',             cfg.arm_pin ?? '1234');
            setVal('sd-defuse-pin',          cfg.defuse_pin ?? '4321');

            if (Array.isArray(cfg.v2_arm_seq) && cfg.v2_arm_seq.length > 0) {
                window.v2ArmSequence = JSON.parse(JSON.stringify(cfg.v2_arm_seq));
            }
            if (Array.isArray(cfg.v2_defuse_seq) && cfg.v2_defuse_seq.length > 0) {
                window.v2DefuseSequence = JSON.parse(JSON.stringify(cfg.v2_defuse_seq));
            }
        } else if (preset.mode === 'dom') {
            setVal('dom-game-dur',       cfg.game_duration ?? 15);
            setVal('dom-v2-fx-smoke',    cfg.fx_smoke ? 'true' : 'false');
            setVal('dom-v2-fx-relay',    cfg.fx_relay ? 'true' : 'false');
            setVal('dom-capture-time',   cfg.capture_time ?? 10);
            setVal('dom-countdown',      cfg.countdown ?? 10);

            if (Array.isArray(cfg.v2_dom_alpha_seq) && cfg.v2_dom_alpha_seq.length > 0) {
                window.v2DomAlphaSequence = JSON.parse(JSON.stringify(cfg.v2_dom_alpha_seq));
            }
            if (Array.isArray(cfg.v2_dom_bravo_seq) && cfg.v2_dom_bravo_seq.length > 0) {
                window.v2DomBravoSequence = JSON.parse(JSON.stringify(cfg.v2_dom_bravo_seq));
            }
        } else if (preset.mode === 'tdm') {
            setVal('tdm-game-dur', cfg.game_duration ?? 15);
        }

        renderV2Sequences();

        // Richiede nuovamente [1. TRANSMIT CONFIG] quando si cambia preset per sicurezza operativa
        const btnStart = document.getElementById('btn-start-mission');
        if (btnStart) {
            btnStart.disabled = true;
            btnStart.classList.add('disabled');
        }
    }

    window.onPresetSelectChange = function(val) {
        if (!val) {
            selectedPresetId = null;
            updatePresetActionButtons();
            setPresetStatus("MODALITÀ CONFIGURAZIONE MANUALE", "#888");
            return;
        }
        const idNum = parseInt(val);
        const found = missionPresets.find(p => p.id === idNum);
        if (found) {
            selectedPresetId = found.id;
            applyPresetToUI(found);
            updatePresetActionButtons();
            setPresetStatus(`PRESET CARICATO: ${found.name}`, "var(--sop-primary)");
            logSystem(`PRESET CARICATO: [${found.mode.toUpperCase()}] ${found.name}`);
        }
    };

    window.saveNewPreset = function() {
        const mode = document.getElementById('setup-mode-select')?.value || 'sd';
        const defaultName = mode === 'sd' ? 'S&D CUSTOM' : (mode === 'dom' ? 'DOMINIO CUSTOM' : 'TDM CUSTOM');
        const name = prompt("Inserisci il nome per il nuovo Preset di Regole:", defaultName);
        if (!name || name.trim() === '') return;

        const cfg = extractCurrentSetupPreset();
        socket.emit('save_preset', {
            id: null,
            name: name.trim().toUpperCase(),
            mode: mode,
            config: cfg
        });
    };

    window.overwriteCurrentPreset = function() {
        if (!selectedPresetId) return;
        const current = missionPresets.find(p => p.id === selectedPresetId);
        if (!current) return;

        if (!confirm(`Vuoi sovrascrivere il preset "${current.name}" con le regole attuali?`)) return;
        const mode = document.getElementById('setup-mode-select')?.value || current.mode;
        const cfg = extractCurrentSetupPreset();
        socket.emit('save_preset', {
            id: current.id,
            name: current.name,
            mode: mode,
            config: cfg
        });
    };

    window.renameCurrentPreset = function() {
        if (!selectedPresetId) return;
        const current = missionPresets.find(p => p.id === selectedPresetId);
        if (!current) return;

        const newName = prompt("Inserisci il nuovo nome del Preset:", current.name);
        if (!newName || newName.trim() === '') return;

        const mode = document.getElementById('setup-mode-select')?.value || current.mode;
        const cfg = extractCurrentSetupPreset();
        socket.emit('save_preset', {
            id: current.id,
            name: newName.trim().toUpperCase(),
            mode: mode,
            config: cfg
        });
    };

    window.deleteCurrentPreset = function() {
        if (!selectedPresetId) return;
        const current = missionPresets.find(p => p.id === selectedPresetId);
        if (!current) return;

        if (confirm(`Eliminare definitivamente il preset "${current.name}"?`)) {
            socket.emit('delete_preset', { id: current.id });
            selectedPresetId = null;
            setPresetStatus("PRESET ELIMINATO", "var(--sop-alert)");
        }
    };

    socket.on('presets_update', (presetsList) => {
        missionPresets = Array.isArray(presetsList) ? presetsList : [];
        renderPresetDropdown();
    });

    socket.on('preset_saved_ack', (data) => {
        if (data && data.id) {
            selectedPresetId = data.id;
            renderPresetDropdown();
            setPresetStatus(`SALVATO: ${data.name}`, "var(--sop-success)");
            logSystem(`PRESET SALVATO: ${data.name}`);
        }
    });

    socket.on('factions_update', (factions) => {
        Object.assign(currentFactions, factions);
        applyFactionStyles(currentFactions);

        renderArenaRoster(arenaRoster, socket);
        if (typeof globalPlayersDB !== 'undefined' && globalPlayersDB.length > 0) {
            renderDatabase(globalPlayersDB, socket, arenaRoster);
        }

        updateMonitorDailyWins(currentFactions);
        renderV2Sequences();

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
        "CLEAR": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'><circle cx='12' cy='12' r='4.2'/><line x1='12' y1='2.5' x2='12' y2='4.5'/><line x1='12' y1='19.5' x2='12' y2='21.5'/><line x1='5.28' y1='5.28' x2='6.7' y2='6.7'/><line x1='17.3' y1='17.3' x2='18.72' y2='18.72'/><line x1='2.5' y1='12' x2='4.5' y2='12'/><line x1='19.5' y1='12' x2='21.5' y2='12'/><line x1='5.28' y1='18.72' x2='6.7' y2='17.3'/><line x1='17.3' y1='6.7' x2='18.72' y2='5.28'/></svg>",
        "CLEAR_NIGHT": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'><path d='M19 15.5A7.5 7.5 0 0 1 8.5 5a8.5 8.5 0 1 0 10.5 10.5Z'/><line x1='13' y1='2.5' x2='13' y2='6.5'/><line x1='11' y1='4.5' x2='15' y2='4.5'/><line x1='18.5' y1='7' x2='18.5' y2='11'/><line x1='16.5' y1='9' x2='20.5' y2='9'/></svg>",
        "CLOUDS": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'><path d='M7 18a4.2 4.2 0 0 1-.8-8.32A5.8 5.8 0 0 1 17.6 9.5 4.25 4.25 0 0 1 17.5 18H7Z'/></svg>",
        "CLOUDS_NIGHT": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'><path d='M6 20a3.5 3.5 0 0 1-.65-6.94 4.8 4.8 0 0 1 9.5-.16A3.5 3.5 0 0 1 14.8 20H6Z'/><path d='M12.2 7.2A6.5 6.5 0 0 1 20.8 3.8a6.5 6.5 0 0 0 0 11.8'/></svg>",
        "FOG": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'><path d='M7 15a4 4 0 0 1-.75-7.93A5.5 5.5 0 0 1 17.1 6.9 4 4 0 0 1 17 15H7Z'/><line x1='3' y1='18' x2='7' y2='18'/><line x1='9.5' y1='18' x2='21' y2='18'/><line x1='5' y1='21' x2='12' y2='21'/><line x1='15.5' y1='21' x2='19.5' y2='21'/></svg>",
        "RAIN": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'><path d='M7 15a4 4 0 0 1-.75-7.93A5.5 5.5 0 0 1 17.1 6.9 4 4 0 0 1 17 15H7Z'/><line x1='8' y1='18' x2='6.2' y2='21.5'/><line x1='12' y1='18' x2='10.2' y2='21.5'/><line x1='16' y1='18' x2='14.2' y2='21.5'/></svg>",
        "SHOWERS": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'><path d='M5 15.2A4.2 4.2 0 0 1 6.2 7.3a5.8 5.8 0 0 1 11.4-.2 4.25 4.25 0 0 1 1.4 8.1'/><line x1='8' y1='11.5' x2='8' y2='14.5'/><line x1='8' y1='17.5' x2='8' y2='20.5'/><line x1='12' y1='12.5' x2='12' y2='15.5'/><line x1='12' y1='18.5' x2='12' y2='21.5'/><line x1='16' y1='11.5' x2='16' y2='14.5'/><line x1='16' y1='17.5' x2='16' y2='20.5'/></svg>",
        "STORM": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'><path d='M6 16.2A4.2 4.2 0 0 1 6.2 7.8a5.8 5.8 0 0 1 11.4-.2 4.25 4.25 0 0 1 .4 8.6'/><polyline points='13 11 9.5 16.5 14.5 16.5 11 22'/></svg>",
        "SNOW": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'><line x1='12' y1='2.5' x2='12' y2='21.5'/><line x1='2.5' y1='12' x2='21.5' y2='12'/><polyline points='8 4 12 8 16 4'/><polyline points='8 20 12 16 16 20'/><polyline points='4 8 8 12 4 16'/><polyline points='20 8 16 12 20 16'/></svg>",
        "DEFAULT": "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2300ffcc' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'><circle cx='12' cy='12' r='10'/><line x1='12' y1='8' x2='12' y2='12'/><line x1='12' y1='16' x2='12.01' y2='16'/></svg>"
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
        if (type === 'SETTINGS_UPDATE' || type === 'MODE_ENTER') {
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