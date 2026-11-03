// include/GameMode.h

/**
 * @file GameMode.h
 * @brief Definizione della classe base astratta per tutte le modalità di gioco.
 * * Questo file definisce l'interfaccia (il "contratto") che ogni modalità di gioco
 * (es. SearchDestroyMode, DominationMode) deve rispettare.
 */

#ifndef GAME_MODE_H
#define GAME_MODE_H

/**
 * @class GameMode
 * @brief Classe base astratta per le modalità di gioco.
 * * Non può essere istanziata direttamente, ma serve come modello. Obbliga ogni
 * modalità di gioco derivata a implementare le funzioni essenziali del suo
 * ciclo di vita: enter(), loop(), e exit().
 */
class GameMode {
public:
    /**
     * @brief Costruttore predefinito.
     */
    GameMode() {}

    /**
     * @brief Distruttore virtuale.
     * @details Essenziale per garantire che la memoria venga liberata correttamente
     * quando si lavora con oggetti di classi derivate tramite puntatori della classe base.
     */
    virtual ~GameMode() {}

    /**
     * @brief Metodo chiamato una sola volta quando si entra nella modalità.
     * @details Usato per inizializzare le variabili specifiche della modalità,
     * visualizzare la schermata di ingresso e preparare lo stato iniziale.
     * È una funzione virtuale pura ( = 0), quindi deve essere implementata
     * dalle classi derivate.
     */
    virtual void enter() = 0;

    /**
     * @brief Metodo chiamato ripetutamente ad ogni ciclo del loop() principale.
     * @details Contiene la logica centrale e continua della modalità di gioco,
     * come la lettura degli input e l'aggiornamento dello stato.
     * È una funzione virtuale pura ( = 0).
     */
    virtual void loop() = 0;

    /**
     * @brief Metodo chiamato una sola volta quando si esce dalla modalità.
     * @details Usato per eseguire operazioni di pulizia, salvare le impostazioni
     * e preparare il ritorno al menu principale.
     * È una funzione virtuale pura ( = 0).
     */
    virtual void exit() = 0;
};

#endif // GAME_MODE_H