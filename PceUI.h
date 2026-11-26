#pragma once
/*
 * Questo file fa parte del progetto PiPCE.
 *
 * Copyright (C) 2026 Ivan Vettori
 *
 * Questo file è opera originale dell'autore e viene rilasciato
 * sotto licenza GNU General Public License v3.0.
 * Puoi ridistribuirlo e/o modificarlo secondo i termini della GPLv3.
 *
 * Questo programma è distribuito nella speranza che sia utile,
 * ma SENZA ALCUNA GARANZIA; senza neppure la garanzia implicita
 * di COMMERCIABILITÀ o IDONEITÀ PER UN PARTICOLARE SCOPO.
 * Vedi la licenza GPLv3 per maggiori dettagli.
 */
#include <stdint.h>
#include <TFT_eSPI.h>
// Pad per la UI (bit attivi alti)
#define UIPAD_UP    0x01
#define UIPAD_DOWN  0x02
#define UIPAD_LEFT  0x04
#define UIPAD_RIGHT 0x08
#define UIPAD_A     0x10   // conferma  (tasto I)
#define UIPAD_B     0x20   // indietro  (tasto II)

enum MenuAction { MENU_NONE, MENU_BROWSER, MENU_RESUME };
enum BrowserResult { BR_NONE, BR_SELECTED, BR_BACK };

void          uiInit(TFT_eSPI* t);
void          menuDraw(bool gameLoaded);
MenuAction    menuUpdate(uint8_t pad, bool gameLoaded);
bool          browserBegin();                 // false se nessun gioco / FS assente (mostra messaggio)
BrowserResult browserUpdate(uint8_t pad);
const char*   browserSelected();
void          uiLoading(const char* name);
void          uiProgress(int pct);
void          uiError(const char* msg);
