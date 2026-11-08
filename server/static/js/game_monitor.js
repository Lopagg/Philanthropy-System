/**
 * @file game_monitor.js
 * @brief Gestione del pannello di monitoraggio partita live (Timer globale, Timer bomba,
 *        barre di avanzamento innesco/disinnesco, punteggi Dominio e riepilogo regole).
 */

import { currentFactions } from './main.js';

let lastConfiguredMode = null;
let debounceTimer1 = null;
let debounceTimer2 = null;

// ============================================================================
// FUNZIONI DI UTILITÀ DOM E FORMATTAZIONE
// ============================================================================

function setElText(id, txt) {
    const el = document.getElementById(id);
    if (el) el.textContent = txt;
}

function formatTime(seconds) {
    if (seconds === undefined || seconds === null || seconds < 0) return "--:--";
    const m = Math.floor(seconds / 60);
    const s = seconds % 60;
    return `${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
}

// ============================================================================
// RESET E AGGIORNAMENTO MONITOR DI PARTITA
// ============================================================================

/**
 * @brief Ripristina il layout del monitor di missione allo stato di attesa iniziale.
 */
export function resetMonitorLayout() {
    setElText('global-timer-display', "--:--");
    setElText('sd-bomb-status', "WAITING...");
    setElText('d-val-a', "0");
    setElText('d-val-b', "0");
    setElText('sd-bomb-timer', "--:--");
    setElText('monitor-mode-title', "AWAITING MISSION DATA...");

    const statusEl = document.getElementById('sd-bomb-status');
    if (statusEl) statusEl.style.color = "#fff";

    const armBar = document.getElementById('sd-arm-bar');
    const defuseBar = document.getElementById('sd-defuse-bar');
    if (armBar) armBar.style.width = "0%";
    if (defuseBar) defuseBar.style.width = "0%";

    const rulesList = document.getElementById('sd-rules-list');
    if (rulesList) rulesList.innerHTML = '<li>WAITING FOR TELEMETRY...</li>';

    document.getElementById('widget-sd-tactical')?.classList.add('hidden');
    document.getElementById('widget-generic-map')?.classList.remove('hidden');
    document.querySelector('.bomb-timer-container')?.classList.add('hidden');
    document.getElementById('domination-scores')?.classList.add('hidden');
    document.querySelector('.progress-section')?.classList.add('hidden');

    const btnForceEnd = document.getElementById('btn-force-end');
    if (btnForceEnd && btnForceEnd.parentElement) {
        btnForceEnd.parentElement.classList.add('hidden');
    }

    lastConfiguredMode = null;
}

/**
 * @brief Aggiorna i contatori e i colori delle vittorie odierne nella schermata Monitor.
 */
export function updateMonitorDailyWins(factions) {
    setElText('monitor-wins-lbl-a', factions.ALPHA.name);
    setElText('monitor-wins-lbl-b', factions.BRAVO.name);
    setElText('monitor-wins-alpha', factions.ALPHA.daily_wins || 0);
    setElText('monitor-wins-bravo', factions.BRAVO.daily_wins || 0);

    const alphaNum = document.getElementById('monitor-wins-alpha');
    const alphaLbl = document.getElementById('monitor-wins-lbl-a');
    if (alphaNum && alphaLbl) {
        alphaNum.style.color = factions.ALPHA.color;
        alphaNum.style.textShadow = `0 0 10px ${factions.ALPHA.color}66`;
        alphaLbl.style.color = factions.ALPHA.color;
    }

    const bravoNum = document.getElementById('monitor-wins-bravo');
    const bravoLbl = document.getElementById('monitor-wins-lbl-b');
    if (bravoNum && bravoLbl) {
        bravoNum.style.color = factions.BRAVO.color;
        bravoNum.style.textShadow = `0 0 10px ${factions.BRAVO.color}66`;
        bravoLbl.style.color = factions.BRAVO.color;
    }
}

/**
 * @brief Elabora i pacchetti di telemetria provenienti dal server o dai nodi ESP32
 *        aggiornando timer, stato obiettivo e barre di progresso.
 */
export function updateUniversalFeed(payload, mode, msgType) {
    if (['KIOSK', 'MAIN MENU', 'TERMINAL'].includes(mode) || msgType === 'RFID_SCAN') return;

    // 1. Aggiornamento rapido del solo orologio globale di partita (1 Hz)
    if (msgType === 'TIME_UPDATE') {
        if (payload.game_time !== undefined) {
            const globalTimerEl = document.getElementById('global-timer-display');
            if (globalTimerEl) {
                globalTimerEl.textContent = formatTime(payload.game_time);
                globalTimerEl.style.color = 'var(--sop-primary)';
            }
        }
        return;
    }

    const isDom = (msgType === 'DOM_UPDATE' || mode === 'DOMINATION');
    const isTdm = (msgType === 'TDM_UPDATE' || mode === 'TEAM_DEATHMATCH');
    const isSd  = (msgType === 'SD_UPDATE'  || mode === 'SEARCH_AND_DESTROY');

    // 2. Configurazione iniziale del layout al cambio modalità
    if (mode && mode !== lastConfiguredMode) {
        lastConfiguredMode = mode;

        if (isTdm) {
            document.getElementById('widget-sd-tactical')?.classList.add('hidden');
            document.getElementById('widget-generic-map')?.classList.remove('hidden');
        } else {
            document.getElementById('widget-sd-tactical')?.classList.remove('hidden');
            document.getElementById('widget-generic-map')?.classList.add('hidden');
        }

        setElText('monitor-mode-title', `OPERATION: ${mode.replace(/_/g, ' ').toUpperCase()}`);

        const armBar = document.getElementById('sd-arm-bar');
        const defuseBar = document.getElementById('sd-defuse-bar');
        if (armBar) armBar.className = "progress-fill fill-red";
        if (defuseBar) defuseBar.className = "progress-fill fill-green";
    }

    // 3. Aggiornamento testo e colore dello stato missione / bomba
    if (payload.state) {
        let s = payload.state
            .replace('ALPHA', currentFactions.ALPHA.name)
            .replace('BRAVO', currentFactions.BRAVO.name);

        const statusEl = document.getElementById('sd-bomb-status');
        if (statusEl) {
            statusEl.textContent = s;
            if (s.includes('WINS') || s.includes('OWNED')) {
                if (s.includes('CT WINS') || s.includes('BRAVO WINS') || s.includes('OWNED BRAVO') || s.includes(currentFactions.BRAVO.name)) {
                    statusEl.style.color = currentFactions.BRAVO.color;
                } else if (s.includes('T WINS') || s.includes('ALPHA WINS') || s.includes('OWNED ALPHA') || s.includes(currentFactions.ALPHA.name)) {
                    statusEl.style.color = currentFactions.ALPHA.color;
                }
            } else if (s.includes('DEFUS') || s.includes('CAPTURING B')) {
                statusEl.style.color = '#55ff55';
            } else if (s.includes('ARM') || s.includes('EXPLODED') || s.includes('CAPTURING A')) {
                statusEl.style.color = '#ff3333';
            } else if (s === 'STANDBY' || s === 'PREPARING' || s === 'DRAW') {
                statusEl.style.color = '#ff9900';
            } else {
                statusEl.style.color = 'var(--sop-primary)';
            }
        }
        updateForceEndButton(payload.state);
    }

    // 4. Aggiornamento Timer Globale
    const globalTimerEl = document.getElementById('global-timer-display');
    if (globalTimerEl) {
        if (payload.game_time !== undefined) {
            globalTimerEl.textContent = formatTime(payload.game_time);
            globalTimerEl.style.color = 'var(--sop-primary)';
        } else if (payload.time_left !== undefined) {
            globalTimerEl.textContent = formatTime(payload.time_left);
        } else if (msgType === 'COUNTDOWN_UPDATE' && payload.time !== undefined) {
            globalTimerEl.textContent = `T-${payload.time}`;
            globalTimerEl.style.color = '#ff9900';
        }
    }

    // 5. Aggiornamento Pannello Specifico Modalità (S&D vs Domination)
    const bombContainer = document.querySelector('.bomb-timer-container');
    const domScores = document.getElementById('domination-scores');
    const progSection = document.querySelector('.progress-section');
    const title = document.getElementById('tactical-panel-title');

    if (isSd && title && bombContainer && domScores && progSection) {
        title.textContent = "TACTICAL FEED // S&D";
        bombContainer.classList.remove('hidden');
        domScores.classList.add('hidden');
        progSection.classList.remove('hidden');
        setElText('lbl-prog-1', "ARMING PROGRESS");
        setElText('lbl-prog-2', "DEFUSING PROGRESS");

        const bombTimerEl = document.getElementById('sd-bomb-timer');
        if (bombTimerEl) {
            if (payload.state === 'SAFE' || payload.state === 'ARMING...' || payload.bomb_time === null || payload.bomb_time < 0) {
                bombTimerEl.textContent = "--:--";
            } else if (payload.bomb_time !== undefined) {
                bombTimerEl.textContent = formatTime(payload.bomb_time);
            }
        }
    } else if (isDom && title && bombContainer && domScores && progSection) {
        title.textContent = "TACTICAL FEED // DOMINATION";
        bombContainer.classList.add('hidden');
        domScores.classList.remove('hidden');
        progSection.classList.remove('hidden');
        setElText('lbl-prog-1', `${currentFactions.ALPHA.name} ACTION`);
        setElText('lbl-prog-2', `${currentFactions.BRAVO.name} ACTION`);
        if (payload.score_a !== undefined) setElText('d-val-a', formatTime(payload.score_a));
        if (payload.score_b !== undefined) setElText('d-val-b', formatTime(payload.score_b));
    }

    // 6. Aggiornamento Barre di Avanzamento
    if (!isTdm && (msgType === 'SD_UPDATE' || msgType === 'DOM_UPDATE')) {
        let width1 = 0, width2 = 0, bar1Active = false, bar2Active = false;
        if (isSd) {
            if (payload.arm_prog !== undefined) { width1 = payload.arm_prog; bar1Active = width1 > 0; }
            if (payload.def_prog !== undefined) { width2 = payload.def_prog; bar2Active = width2 > 0; }
        } else if (isDom) {
            const prog = payload.capture_prog || 0;
            const state = payload.state || '';
            if (state.includes('CAPTURING A')) { width1 = prog; bar1Active = true; }
            else if (state.includes('CAPTURING B')) { width2 = prog; bar2Active = true; }
        }
        updateProgressBar(document.getElementById('sd-arm-bar'), width1, bar1Active, 1);
        updateProgressBar(document.getElementById('sd-defuse-bar'), width2, bar2Active, 2);
    }

    updateMonitorDailyWins(currentFactions);
    if (msgType === 'SETTINGS_UPDATE' || msgType === 'MODE_ENTER') {
        renderRules(payload);
    }
}

/**
 * @brief Aggiorna la larghezza di una barra di progresso con debounce di 200ms in chiusura.
 */
function updateProgressBar(barElem, width, isActive, timerRefName) {
    if (!barElem) return;
    if (isActive) {
        if (timerRefName === 1) { clearTimeout(debounceTimer1); debounceTimer1 = null; }
        else { clearTimeout(debounceTimer2); debounceTimer2 = null; }
        barElem.style.width = `${width}%`;
    } else if (barElem.style.width !== '0%') {
        if (timerRefName === 1) {
            if (!debounceTimer1) {
                debounceTimer1 = setTimeout(() => { barElem.style.width = "0%"; debounceTimer1 = null; }, 200);
            }
        } else {
            if (!debounceTimer2) {
                debounceTimer2 = setTimeout(() => { barElem.style.width = "0%"; debounceTimer2 = null; }, 200);
            }
        }
    }
}

/**
 * @brief Configura il pulsante di controllo missione ([TERMINATE MISSION] o [CLOSE & DISMISS]).
 */
function updateForceEndButton(state) {
    const btnForceEnd = document.getElementById('btn-force-end');
    if (!btnForceEnd || !btnForceEnd.parentElement) return;

    const footer = btnForceEnd.parentElement;
    const activeStates = [
        'SAFE', 'ARMING...', 'ARMED', 'DEFUSING...',
        'NEUTRAL', 'OWNED ALPHA', 'OWNED BRAVO', 'ALPHA OWNED', 'BRAVO OWNED',
        'CAPTURING A', 'CAPTURING B', 'CAPTURING A...', 'CAPTURING B...', 'ACTIVE'
    ];
    const endStates = ['T WINS', 'CT WINS', 'ALPHA WINS', 'BRAVO WINS', 'DRAW', 'STOPPED'];

    if (activeStates.includes(state)) {
        footer.classList.remove('hidden');
        btnForceEnd.textContent = "[ TERMINATE MISSION ]";
        btnForceEnd.dataset.action = "FORCE_END_GAME";
        btnForceEnd.className = "deck-btn btn-danger full-width";
        btnForceEnd.style.borderColor = "var(--sop-alert)";
        btnForceEnd.style.color = "var(--sop-alert)";
    } else if (endStates.includes(state)) {
        footer.classList.remove('hidden');
        btnForceEnd.textContent = "[ CLOSE & DISMISS ]";
        btnForceEnd.dataset.action = "CLOSE_MISSION";
        btnForceEnd.className = "deck-btn full-width";
        btnForceEnd.style.borderColor = "var(--sop-success)";
        btnForceEnd.style.color = "var(--sop-success)";
    } else {
        footer.classList.add('hidden');
    }
}

/**
 * @brief Renderizza la lista dei parametri e delle sequenze modulari nel pannello MISSION INTEL.
 */
export function renderRules(payload) {
    const elSdRulesList = document.getElementById('sd-rules-list');
    if (!elSdRulesList) return;
    elSdRulesList.innerHTML = '';

    if (payload.attacker) {
        const isAlpha = payload.attacker === 'ALPHA';
        const teamName = isAlpha ? currentFactions.ALPHA.name : currentFactions.BRAVO.name;
        const teamColor = isAlpha ? currentFactions.ALPHA.color : currentFactions.BRAVO.color;

        const li = document.createElement('li');
        li.innerHTML = `<span class="rule-key">ATTACCANTI (BOMBA)</span> <span class="rule-val" style="color:${teamColor}; text-shadow: 0 0 10px ${teamColor};">${teamName}</span>`;
        elSdRulesList.appendChild(li);
    }

    const keysMap = {
        'bomb_time':     'TIMER BOMBA (Min)',
        'arm_time':      'TEMPO INNESCO (Sec)',
        'defuse_time':   'TEMPO DISINNESCO (Sec)',
        'game_duration': 'DURATA ROUND (Min)',
        'capture_time':  'TEMPO CATTURA (Sec)',
        'countdown':     'START DELAY (Sec)'
    };

    for (const [key, val] of Object.entries(payload)) {
        if (!keysMap[key]) continue;
        let displayVal = val;
        if (val === true || val === 'YES') displayVal = 'SÌ';
        if (val === false || val === 'NO') displayVal = 'NO';

        const li = document.createElement('li');
        li.innerHTML = `<span class="rule-key">${keysMap[key]}</span> <span class="rule-val">${displayVal}</span>`;
        elSdRulesList.appendChild(li);
    }

    const formatSeq = (seq) => seq.map(s => {
        if (s.type === 'HOLD') return `HOLD(${s.val_num}s)`;
        if (s.type === 'PIN') return `PIN(${s.val_str})`;
        if (s.type === 'KEY') return `KEY(${(s.val_str || 'K1_ON').replace(/_/g, ' ')})`;
        if (s.type === 'RFID') {
            if (s.rfid_mode === 'TEAM') return `RFID(SQUADRA)`;
            if (s.rfid_mode === 'PLAYERS') {
                const names = (Array.isArray(s.player_names) && s.player_names.length > 0) ? s.player_names.join(', ') : 'NESSUNO';
                return `RFID(${names})`;
            }
            if (s.rfid_mode === 'CUSTOM') return `RFID(UID: ${s.val_str || '?'})`;
            return `RFID(QUALSIASI)`;
        }
        return s.type;
    }).join(' → ');

    if (Array.isArray(payload.arm_seq)) {
        const li = document.createElement('li');
        li.innerHTML = `<span class="rule-key">SEQ. INNESCO</span> <span class="rule-val">${formatSeq(payload.arm_seq)}</span>`;
        elSdRulesList.appendChild(li);
    }
    if (Array.isArray(payload.defuse_seq)) {
        const li = document.createElement('li');
        li.innerHTML = `<span class="rule-key">SEQ. DISINNESCO</span> <span class="rule-val">${formatSeq(payload.defuse_seq)}</span>`;
        elSdRulesList.appendChild(li);
    }

    const failLabels = {
        'RETRY_STEP':  'RIPETI STEP',
        'RESTART_SEQ': 'DA CAPO',
        'EXPLODE':     'ESPLOSIONE BOMBA!'
    };
    if (payload.arm_fail_mode) {
        const li = document.createElement('li');
        li.innerHTML = `<span class="rule-key">ERRORE INNESCO</span> <span class="rule-val">${failLabels[payload.arm_fail_mode] || payload.arm_fail_mode}</span>`;
        elSdRulesList.appendChild(li);
    }
    if (payload.defuse_fail_mode) {
        const isExplode = payload.defuse_fail_mode === 'EXPLODE';
        const valStyle = isExplode ? 'color:var(--sop-alert); font-weight:bold;' : '';
        const li = document.createElement('li');
        li.innerHTML = `<span class="rule-key">ERRORE DISINNESCO</span> <span class="rule-val" style="${valStyle}">${failLabels[payload.defuse_fail_mode] || payload.defuse_fail_mode}</span>`;
        elSdRulesList.appendChild(li);
    }

    if (Array.isArray(payload.alpha_seq)) {
        const li = document.createElement('li');
        li.innerHTML = `<span class="rule-key">CONQUISTA ${currentFactions.ALPHA.name}</span> <span class="rule-val">${formatSeq(payload.alpha_seq)}</span>`;
        elSdRulesList.appendChild(li);
    }
    if (Array.isArray(payload.bravo_seq)) {
        const li = document.createElement('li');
        li.innerHTML = `<span class="rule-key">CONQUISTA ${currentFactions.BRAVO.name}</span> <span class="rule-val">${formatSeq(payload.bravo_seq)}</span>`;
        elSdRulesList.appendChild(li);
    }
}