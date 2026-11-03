/**
 * @file GameMode.h
 * @brief Interfaccia base polimorfica per tutte le modalità di gioco del RAT-V2.
 *
 * Ogni modalità operativa (Search & Destroy, Domination, Hardware Test, ecc.)
 * eredita da questa classe astratta, permettendo a main.cpp di gestire il ciclo
 * di vita della partita senza conoscere i dettagli implementativi della modalità.
 */

#ifndef GAMEMODE_H
#define GAMEMODE_H

#include "events.h"
#include "HardwareManager.h"
#include "NetworkManager.h"

class GameMode {
public:
    virtual ~GameMode() {}

    /**
     * @brief Inizializza la modalità di gioco, preparando schermi, LED e stato iniziale.
     */
    virtual void begin() = 0;

    /**
     * @brief Gestisce un singolo evento prelevato dalla coda principale FreeRTOS.
     * @param event L'evento da elaborare (input utente o comando server).
     */
    virtual void handleEvent(const SystemEvent& event) = 0;

    /**
     * @brief Ciclo di aggiornamento periodico (chiamato a ~50 Hz dal loop principale).
     * Gestisce timer, barre di avanzamento e invio telemetria periodica.
     */
    virtual void update() = 0;
};

#endif // GAMEMODE_H