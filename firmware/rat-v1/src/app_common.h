// src/app_common.h

/**
 * @file app_common.h
 * @brief Definizioni globali e tipi comuni per l'intera applicazione.
 * * Questo file contiene definizioni essenziali come la versione del firmware,
 * l'enumerazione degli stati principali dell'applicazione (AppState), e tipi
 * di dato personalizzati, rendendoli disponibili a tutto il progetto.
 */

#ifndef APP_COMMON_H
#define APP_COMMON_H

/**
 * @brief Versione attuale del firmware in formato "MAJOR.MINOR.PATCH".
 * Utile per il debug, la visualizzazione all'avvio e le comunicazioni di rete.
 */
#define FIRMWARE_VERSION "0.4.1"

/**
 * @brief Enumerazione degli stati principali dell'applicazione.
 * * Definisce tutti i possibili "stati" o "schermate" in cui il programma si può trovare.
 * Viene usata come una macchina a stati nel file main.cpp per decidere quale
 * logica eseguire in un dato momento.
 */
enum AppState {
  APP_STATE_WELCOME,              // Stato iniziale: mostra la schermata di benvenuto.
  APP_STATE_MAIN_MENU,            // Stato operativo: l'utente è nel menu principale.
  APP_STATE_TEST_HARDWARE,        // Stato di test: per verificare il funzionamento dei componenti.
  APP_STATE_SEARCH_DESTROY_MODE,  // Stato di gioco: la modalità "Cerca e Distruggi" è attiva.
  APP_STATE_DOMINATION_MODE,      // Stato di gioco: la modalità "Dominio" è attiva.
  APP_STATE_MUSIC_ROOM,           // Stato utility: la "Stanza dei Suoni" è attiva.
  APP_STATE_FOXHUNT,              // Stato di gioco: la modalità "Foxhunt" è attiva.
  APP_STATE_TERMINAL_MODE
};

/**
 * @brief Definizione di un tipo per un puntatore a funzione.
 * * Crea un "alias" di tipo chiamato MainMenuDisplayFunction che rappresenta un
 * puntatore a una funzione che non restituisce nulla (void) e non accetta parametri ().
 * Viene usato per passare la funzione displayMainMenu() del main.cpp alle classi delle 
 * modalità di gioco, permettendo loro di richiederne la visualizzazione senza
 * creare dipendenze circolari.
 */
typedef void (*MainMenuDisplayFunction)();

#endif // APP_COMMON_H