/**
 * @file roster_manager.js
 * @brief Gestione del Roster attivo di giornata (assegnazione squadre, tracking IN/FUORI),
 *        del Database generale operatori (filtri, ricerca, ordinamento) e del Modal Profilo.
 */

import { currentFactions } from './main.js';

const ADMIN_UID = '04863EB76F6180';
const DEFAULT_AVATAR_SVG = "data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAxMDAgMTAwIiBmaWxsPSIjMzMzIj48Y2lyY2xlIGN4PSI1MCIgY3k9IjM1IiByPSIyMCIvPjxwYXRoIGQ9Ik0yMCA5MCBRMjAgNjAgNTAgNjAgUTgwIDYwIDgwIDkwIFoiLz48L3N2Zz4=";

let currentProfileUid = null;
export let globalPlayersDB = [];

let cachedSocket = null;
let cachedArenaRoster = {};

export function getCurrentProfileUid() {
    return currentProfileUid;
}

// ============================================================================
// RENDERING ROSTER ATTIVO (GESTIONE SQUADRE E TRACKING IN CAMPO)
// ============================================================================

/**
 * @brief Aggiorna tutte le liste e i contatori relativi al roster attivo di giornata.
 */
export function renderArenaRoster(arenaRoster, socket) {
    cachedSocket = socket;
    cachedArenaRoster = arenaRoster;

    const listAlphaManage  = document.getElementById('roster-alpha-manage');
    const listBravoManage  = document.getElementById('roster-bravo-manage');
    const listAlphaIn      = document.getElementById('track-alpha-in');
    const listAlphaOut     = document.getElementById('track-alpha-out');
    const listBravoIn      = document.getElementById('track-bravo-in');
    const listBravoOut     = document.getElementById('track-bravo-out');
    const listMonitorAlpha = document.getElementById('monitor-roster-alpha');
    const listMonitorBravo = document.getElementById('monitor-roster-bravo');

    if (!listAlphaManage || !listAlphaIn) return;

    [listAlphaManage, listBravoManage, listAlphaIn, listAlphaOut, listBravoIn, listBravoOut].forEach(el => {
        if (el) el.innerHTML = '';
    });
    if (listMonitorAlpha) listMonitorAlpha.innerHTML = '';
    if (listMonitorBravo) listMonitorBravo.innerHTML = '';

    let alphaCount = 0, bravoCount = 0, inFieldCount = 0;
    let alphaInCount = 0, bravoInCount = 0;

    Object.values(arenaRoster).forEach(player => {
        const isAlpha = player.team === 'ALPHA';
        const isInField = player.status === 'IN';

        if (isAlpha) alphaCount++; else bravoCount++;

        if (isInField) {
            inFieldCount++;
            if (isAlpha) {
                alphaInCount++;
                addToList(listMonitorAlpha, player, socket);
            } else {
                bravoInCount++;
                addToList(listMonitorBravo, player, socket);
            }
        }

        const liManage = createManageItem(player, socket);
        if (isAlpha) listAlphaManage.appendChild(liManage);
        else listBravoManage.appendChild(liManage);

        const liTrack = createTrackItem(player, socket, isInField);
        if (isAlpha && isInField) listAlphaIn.appendChild(liTrack);
        if (isAlpha && !isInField) listAlphaOut.appendChild(liTrack);
        if (!isAlpha && isInField) listBravoIn.appendChild(liTrack);
        if (!isAlpha && !isInField) listBravoOut.appendChild(liTrack);
    });

    if (alphaCount === 0) {
        listAlphaManage.innerHTML = '<li class="placeholder-text" style="color:var(--sop-dim); text-align:center; margin-top:20px;">NESSUN OPERATORE</li>';
    }
    if (bravoCount === 0) {
        listBravoManage.innerHTML = '<li class="placeholder-text" style="color:var(--sop-dim); text-align:center; margin-top:20px;">NESSUN OPERATORE</li>';
    }

    const headerAlphaManage = document.getElementById('header-alpha-manage');
    const headerBravoManage = document.getElementById('header-bravo-manage');
    if (headerAlphaManage) {
        headerAlphaManage.innerHTML = `${currentFactions.ALPHA.name} (${alphaCount}) <span style="color:#fff; margin-left:15px; font-size:16px;">VITTORIE: ${currentFactions.ALPHA.daily_wins || 0}</span> <span class="action-btn" onclick="openFactionModal('ALPHA')" style="font-size:14px; float:right;">[EDIT]</span>`;
    }
    if (headerBravoManage) {
        headerBravoManage.innerHTML = `${currentFactions.BRAVO.name} (${bravoCount}) <span style="color:#fff; margin-left:15px; font-size:16px;">VITTORIE: ${currentFactions.BRAVO.daily_wins || 0}</span> <span class="action-btn" onclick="openFactionModal('BRAVO')" style="font-size:14px; float:right;">[EDIT]</span>`;
    }

    const setEl = (id, txt) => { const el = document.getElementById(id); if (el) el.textContent = txt; };
    setEl('header-alpha-track-in',  `${currentFactions.ALPHA.name} IN CAMPO (${alphaInCount})`);
    setEl('header-alpha-track-out', `${currentFactions.ALPHA.name} FUORI (${alphaCount - alphaInCount})`);
    setEl('header-bravo-track-in',  `${currentFactions.BRAVO.name} IN CAMPO (${bravoInCount})`);
    setEl('header-bravo-track-out', `${currentFactions.BRAVO.name} FUORI (${bravoCount - bravoInCount})`);

    setEl('total-op-count',    alphaCount + bravoCount);
    setEl('track-in-count',    inFieldCount);
    setEl('score-a',           alphaInCount);
    setEl('score-b',           bravoInCount);
    setEl('setup-alpha-count', `${alphaInCount}/${alphaCount}`);
    setEl('setup-bravo-count', `${bravoInCount}/${bravoCount}`);

    if (typeof window.renderV2Sequences === 'function') {
        window.renderV2Sequences();
    }
}

// ============================================================================
// DATABASE GENERALE OPERATORI E FILTRI DI RICERCA
// ============================================================================

/**
 * @brief Renderizza l'elenco completo degli operatori applicando i filtri di ricerca e ordinamento.
 */
export function renderDatabase(players, socket, arenaRoster) {
    globalPlayersDB = players || [];
    cachedSocket = socket;
    cachedArenaRoster = arenaRoster;

    const modal = document.getElementById('add-player-modal');
    if (modal && !modal.classList.contains('hidden')) {
        renderAddPlayerList(socket, arenaRoster);
    }

    if (typeof window.renderV2Sequences === 'function') {
        window.renderV2Sequences();
    }

    const dbList = document.getElementById('db-list');
    if (!dbList) return;

    const searchVal  = (document.getElementById('db-search')?.value || '').toLowerCase();
    const filterType = document.getElementById('db-filter')?.value || 'ALL';
    const sortBy     = document.getElementById('db-sort')?.value || 'NAME';

    const filteredPlayers = globalPlayersDB.filter(p => {
        const aliasSafe = (p.alias || '').toLowerCase();
        const uidSafe   = (p.uid || '').toLowerCase();
        const matchSearch = aliasSafe.includes(searchVal) || uidSafe.includes(searchVal);

        let matchType = true;
        if (filterType === 'PRO')   matchType = (p.is_pro === 1);
        if (filterType === 'GUEST') matchType = (p.is_pro !== 1);
        return matchSearch && matchType;
    });

    filteredPlayers.sort((a, b) => {
        if (sortBy === 'NAME')  return (a.alias || '').localeCompare(b.alias || '');
        if (sortBy === 'GAMES') return (b.games_played || 0) - (a.games_played || 0);
        if (sortBy === 'WINS')  return (b.wins || 0) - (a.wins || 0);
        return 0;
    });

    dbList.innerHTML = '';
    const totalCountEl = document.getElementById('db-total-count');
    if (totalCountEl) {
        totalCountEl.textContent = `${filteredPlayers.length} / ${globalPlayersDB.length}`;
    }

    if (filteredPlayers.length === 0) {
        dbList.innerHTML = '<li class="placeholder-text" style="color:var(--sop-dim); text-align:center; margin-top:20px;">NESSUN RISULTATO TROVATO</li>';
        return;
    }

    filteredPlayers.forEach(player => {
        const li = document.createElement('li');
        li.className = 'roster-item';

        const nameSpan = document.createElement('span');
        const games    = player.games_played || 0;
        const wins     = player.wins || 0;
        const deaths   = player.deaths || 0;
        const winRate  = games > 0 ? Math.round((wins / games) * 100) : 0;

        const isAdmin = (player.uid === ADMIN_UID);
        let badge = '';
        let aliasStyle = 'color: inherit;';

        if (isAdmin) {
            badge = '<span style="color: #b15bff; font-weight: bold; margin-right: 12px; text-shadow: 0 0 10px rgba(177,91,255,0.6); font-size: 16px;">[👑 ADMIN]</span>';
            aliasStyle = 'color: #b15bff; text-shadow: 0 0 10px rgba(177,91,255,0.4);';
        } else {
            badge = (player.is_pro === 1) ? '<span class="pro-badge">[★ PRO]</span>' : '<span class="guest-badge">[GUEST]</span>';
        }

        nameSpan.innerHTML = `${badge} <span style="${aliasStyle}">${player.alias}</span> <span style="font-size: 16px; font-weight: normal; color: #aaa; margin-left: 15px; font-family: monospace;">(UID: ${player.uid}) &nbsp;&nbsp;|&nbsp;&nbsp; P: <span style="color:var(--sop-primary); font-weight:bold">${games}</span> &nbsp;&nbsp;V: <span style="color:var(--sop-success); font-weight:bold">${wins}</span> &nbsp;&nbsp;D: <span style="color:var(--sop-alert); font-weight:bold">${deaths}</span> <span style="color: #666; font-size:14px;">(${winRate}%)</span></span>`;
        nameSpan.style.fontWeight = 'bold';
        nameSpan.style.fontSize = '22px';
        nameSpan.className = 'clickable-name';
        nameSpan.onclick = () => openOperatorProfile(player.uid, socket);

        const controlsDiv = document.createElement('div');
        controlsDiv.style.fontSize = '18px';
        controlsDiv.style.fontFamily = 'monospace';

        const btnRename = document.createElement('span');
        btnRename.textContent = '[REN]';
        btnRename.className = 'action-btn';
        btnRename.style.color = 'var(--sop-primary)';
        btnRename.onclick = () => {
            const newName = prompt("Inserisci nuovo nome operatore:", player.alias);
            if (newName && newName.trim() !== '') {
                socket.emit('update_db_player', { uid: player.uid, action: 'rename', value: newName.trim().toUpperCase() });
            }
        };

        const btnDel = document.createElement('span');
        btnDel.textContent = '[DEL]';
        btnDel.className = 'action-btn';
        btnDel.style.color = 'var(--sop-alert)';
        btnDel.onclick = () => {
            if (confirm(`ELIMINARE PERMANENTEMENTE ${player.alias} DAL DATABASE? L'azione è irreversibile.`)) {
                socket.emit('update_db_player', { uid: player.uid, action: 'delete' });
            }
        };

        controlsDiv.appendChild(btnRename);
        controlsDiv.appendChild(btnDel);
        li.appendChild(nameSpan);
        li.appendChild(controlsDiv);
        dbList.appendChild(li);
    });
}

/**
 * @brief Popola il modal per l'aggiunta manuale di giocatori dal DB al roster di giornata.
 */
export function renderAddPlayerList(socket, arenaRoster) {
    const list = document.getElementById('add-player-list');
    if (!list) return;
    list.innerHTML = '';

    if (!globalPlayersDB || !arenaRoster) return;

    const availablePlayers = globalPlayersDB.filter(p => !arenaRoster[p.uid]);
    if (availablePlayers.length === 0) {
        list.innerHTML = '<li class="placeholder-text" style="color:var(--sop-dim); text-align:center; margin-top:20px;">NESSUN GIOCATORE DISPONIBILE O GIÀ IN CAMPO</li>';
        return;
    }

    availablePlayers.forEach(player => {
        const li = document.createElement('li');
        li.className = 'roster-item';
        li.style.justifyContent = 'space-between';

        const nameSpan = document.createElement('span');
        nameSpan.textContent = player.alias;
        nameSpan.style.fontWeight = 'bold';
        nameSpan.style.fontSize = '18px';

        const controlsDiv = document.createElement('div');

        const btnAlpha = document.createElement('button');
        btnAlpha.textContent = `[ + ${currentFactions.ALPHA.name} ]`;
        btnAlpha.className = 'deck-btn';
        btnAlpha.style.color = 'var(--team-a-color)';
        btnAlpha.style.borderColor = 'var(--team-a-color)';
        btnAlpha.style.marginRight = '10px';
        btnAlpha.onclick = () => socket.emit('register_operator', { uid: player.uid, team: 'ALPHA' });

        const btnBravo = document.createElement('button');
        btnBravo.textContent = `[ + ${currentFactions.BRAVO.name} ]`;
        btnBravo.className = 'deck-btn';
        btnBravo.style.color = 'var(--team-b-color)';
        btnBravo.style.borderColor = 'var(--team-b-color)';
        btnBravo.onclick = () => socket.emit('register_operator', { uid: player.uid, team: 'BRAVO' });

        controlsDiv.appendChild(btnAlpha);
        controlsDiv.appendChild(btnBravo);
        li.appendChild(nameSpan);
        li.appendChild(controlsDiv);
        list.appendChild(li);
    });
}

// ============================================================================
// MODAL PROFILO OPERATORE E CRONOLOGIA PARTITE
// ============================================================================

/**
 * @brief Popola e apre il modal del profilo tattico di un singolo operatore.
 */
export function populateProfileModal(data) {
    const setEl = (id, txt) => { const el = document.getElementById(id); if (el) el.textContent = txt; };
    setEl('modal-op-name-text', `PROFILE // ${data.alias}`);
    setEl('modal-op-uid',       data.uid);
    setEl('modal-op-date',      data.registered_at || 'Sconosciuta');
    setEl('modal-op-asd',       data.asd_team || 'NESSUNA');

    const games   = data.games_played || 0;
    const wins    = data.wins || 0;
    const deaths  = data.deaths || 0;
    const winRate = games > 0 ? Math.round((wins / games) * 100) : 0;

    setEl('modal-stat-games',  games);
    setEl('modal-stat-wins',   wins);
    setEl('modal-stat-deaths', deaths);
    setEl('modal-stat-rate',   `${winRate}%`);

    const isPro   = (data.is_pro === 1);
    const isAdmin = (data.uid === ADMIN_UID);

    const typeEl    = document.getElementById('modal-op-type');
    const toggleBtn = document.getElementById('btn-toggle-pro');
    if (typeEl && toggleBtn) {
        if (isAdmin) {
            typeEl.innerHTML = '<span style="color:#b15bff; font-weight:bold; letter-spacing:2px; text-shadow: 0 0 10px rgba(177,91,255,0.5);">[👑 ADMIN]</span>';
            toggleBtn.style.display = 'none';
        } else {
            toggleBtn.style.display = 'inline-block';
            if (isPro) {
                typeEl.innerHTML = '<span style="color:#ffd700">[★ PRO]</span>';
                toggleBtn.textContent = "[ DOWNGRADE TO GUEST ]";
                toggleBtn.className = "deck-btn btn-warning";
                toggleBtn.style.color = "";
                toggleBtn.style.borderColor = "";
            } else {
                typeEl.innerHTML = '<span style="color:#888">[GUEST]</span>';
                toggleBtn.textContent = "[ UPGRADE TO PRO ]";
                toggleBtn.className = "deck-btn";
                toggleBtn.style.color = "var(--sop-primary)";
                toggleBtn.style.borderColor = "var(--sop-primary)";
            }
        }
    }

    const karma = data.karma || 0;
    const karmaEl = document.getElementById('modal-op-karma');
    const karmaContainer = document.querySelector('.karma-container');
    if (karmaContainer) {
        if (isAdmin) {
            karmaContainer.style.display = 'none';
        } else {
            karmaContainer.style.display = 'flex';
            if (karmaEl) {
                karmaEl.textContent = karma;
                const badge = karmaEl.parentElement;
                badge.classList.remove('positive', 'negative');
                if (karma < 0) badge.classList.add('negative');
                else if (karma > 0) badge.classList.add('positive');
            }
        }
    }

    const notesEl = document.getElementById('modal-op-notes');
    if (notesEl) notesEl.value = data.notes || '';

    const photoEl = document.getElementById('modal-op-photo');
    if (photoEl) {
        photoEl.src = data.photo ? `/static/uploads/${data.photo}?t=${new Date().getTime()}` : DEFAULT_AVATAR_SVG;
    }

    const lastMatchEl   = document.getElementById('modal-op-lastmatch');
    const historyListEl = document.getElementById('modal-op-history-list');

    if (data.history && data.history.length > 0) {
        if (lastMatchEl) lastMatchEl.textContent = data.history[0].match_date;
        if (historyListEl) {
            historyListEl.innerHTML = '';
            data.history.forEach(match => {
                let resultColor = '#ff9900';
                let resultText  = 'PAREGGIO';
                if (match.result === 'VITTORIA')  { resultColor = 'var(--sop-success)'; resultText = 'VITTORIA'; }
                if (match.result === 'SCONFITTA') { resultColor = 'var(--sop-alert)';   resultText = 'SCONFITTA'; }

                historyListEl.innerHTML += `
                    <li style="display: flex; justify-content: space-between; border-bottom: 1px dashed rgba(255,255,255,0.1); padding: 6px 0; font-size: 13px;">
                        <span style="color: #aaa; width: 40%;">${match.match_date}</span>
                        <span style="color: var(--sop-text); width: 35%;">${match.game_mode}</span>
                        <span style="color: ${resultColor}; font-weight: bold; width: 25%; text-align: right;">${resultText}</span>
                    </li>
                `;
            });
        }
    } else {
        if (lastMatchEl) lastMatchEl.textContent = "MAI IN CAMPO";
        if (historyListEl) {
            historyListEl.innerHTML = '<li style="text-align: center; color: var(--sop-dim); padding: 10px;">Nessuna registrazione tattica.</li>';
        }
    }

    document.getElementById('operator-modal')?.classList.remove('hidden');
}

// ============================================================================
// HELPER CREAZIONE ELEMENTI LISTA
// ============================================================================

function addToList(list, player, socket) {
    if (!list) return;
    const li = document.createElement('li');
    li.textContent = player.name;
    li.className = 'clickable-name';
    li.onclick = () => openOperatorProfile(player.uid, socket);
    list.appendChild(li);
}

function createManageItem(player, socket) {
    const li = document.createElement('li');
    li.className = 'roster-item';
    li.innerHTML = `
        <span class="clickable-name" style="font-weight:bold; font-size:20px;">${player.name}</span>
        <div style="font-size:18px; font-family:monospace;">
            <span class="action-btn" style="color:var(--sop-primary)">[REN]</span>
            <span class="action-btn" style="color:var(--sop-secondary)">[SWAP]</span>
            <span class="action-btn" style="color:var(--sop-alert)">[DEL]</span>
        </div>
    `;

    li.querySelector('span.clickable-name').onclick = () => openOperatorProfile(player.uid, socket);
    const btns = li.querySelectorAll('.action-btn');
    btns[0].onclick = () => {
        const newName = prompt("Inserisci nuovo nome:", player.name);
        if (newName && newName.trim() !== '') {
            socket.emit('update_operator', { uid: player.uid, action: 'rename', value: newName.trim().toUpperCase() });
        }
    };
    btns[1].onclick = () => socket.emit('update_operator', { uid: player.uid, action: 'swap' });
    btns[2].onclick = () => {
        if (confirm(`Rimuovere ${player.name} dal roster attivo?`)) {
            socket.emit('update_operator', { uid: player.uid, action: 'delete' });
        }
    };
    return li;
}

function createTrackItem(player, socket, isInField) {
    const li = document.createElement('li');
    li.className = `roster-item ${isInField ? 'in-field' : 'eliminated'}`;
    li.innerHTML = `
        <span class="clickable-name" style="font-weight:bold; font-size:20px;">${player.name}</span>
        <div style="font-size:18px; font-family:monospace;">
            <span class="action-btn" style="color:${isInField ? 'var(--team-a-color)' : 'var(--team-b-color)'}">${isInField ? '[RECALL]' : '[DEPLOY]'}</span>
        </div>
    `;
    li.querySelector('span.clickable-name').onclick = () => openOperatorProfile(player.uid, socket);
    li.querySelector('.action-btn').onclick = () => socket.emit('update_operator', { uid: player.uid, action: 'status_toggle' });
    return li;
}

export function openOperatorProfile(uid, socket) {
    currentProfileUid = uid;
    socket.emit('request_profile', { uid: uid });
}

window.openAddPlayerModal = function() {
    renderAddPlayerList(cachedSocket, cachedArenaRoster);
    document.getElementById('add-player-modal')?.classList.remove('hidden');
};

window.closeAddPlayerModal = function() {
    document.getElementById('add-player-modal')?.classList.add('hidden');
};

window.closeModal = function() {
    document.getElementById('operator-modal')?.classList.add('hidden');
};