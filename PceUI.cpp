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
#include <Arduino.h>
#include "configs.h"
#include "PceUI.h"
#include "PceStorage.h"

static TFT_eSPI* T;
static int  menuSel = 0;
static uint32_t lastNav = 0;
static bool waitRelease = true;
#define NAV_MS 200

static char games[PCE_MAX_GAMES][PCE_NAME_MAX];
static int  nGames = 0, bSel = 0, bTop = 0;
#define ROWS 10

void uiInit(TFT_eSPI* t) { T = t; }

void menuDraw(bool gameLoaded) {
  T->fillScreen(TFT_BLACK);
  T->setTextDatum(MC_DATUM);
  T->setTextColor(T->color565(255, 160, 0));
  T->setTextSize(5); T->drawString("PiPCE", 160, 50);
  T->setTextSize(1); T->setTextColor(TFT_DARKGREY); T->drawString("PC Engine for Pico", 160, 88);
  T->setTextSize(2);
  const char* items[2] = {"GAMES BROWSER", "RESUME GAME"};
  for (int i = 0; i < 2; i++) {
    bool dis = (i == 1 && !gameLoaded);
    uint16_t c = dis ? T->color565(70, 70, 70) : (i == menuSel ? TFT_WHITE : T->color565(150, 150, 150));
    if (i == menuSel && !dis) T->fillRoundRect(60, 120 + i * 40, 200, 30, 6, T->color565(0, 70, 140));
    T->setTextColor(c);
    T->drawString(items[i], 160, 135 + i * 40);
  }
  T->setTextSize(1); T->setTextColor(TFT_DARKGREY);
  T->drawString("UP/DOWN select   I confirm", 160, 225);
}

MenuAction menuUpdate(uint8_t pad, bool gameLoaded) {
  uint32_t now = millis();
  if (waitRelease) { if (!pad) waitRelease = false; return MENU_NONE; }
  if (!pad || now - lastNav < NAV_MS) return MENU_NONE;
  if (pad & (UIPAD_UP | UIPAD_DOWN)) {
    menuSel = gameLoaded ? (menuSel ^ 1) : 0;
    lastNav = now; menuDraw(gameLoaded);
  } else if (pad & UIPAD_A) {
    lastNav = now; waitRelease = true;
    if (menuSel == 0) return MENU_BROWSER;
    if (gameLoaded) return MENU_RESUME;
  }
  return MENU_NONE;
}

static void browserDraw() {
  T->fillScreen(TFT_BLACK);
  T->setTextDatum(TL_DATUM);
  T->setTextSize(2); T->setTextColor(T->color565(255, 160, 0));
  T->drawString("GAMES", 6, 4);
  T->setTextSize(1); T->setTextColor(TFT_DARKGREY);
  String s = String(storageName()) + "  " + String(bSel + 1) + "/" + String(nGames);
  T->drawString(s, 200, 10);
  T->setTextSize(2);
  for (int r = 0; r < ROWS; r++) {
    int i = bTop + r; if (i >= nGames) break;
    int y = 28 + r * 20;
    char nm[26]; strncpy(nm, games[i], 25); nm[25] = 0;
    if (i == bSel) { T->fillRect(0, y - 1, 320, 19, T->color565(0, 70, 140)); T->setTextColor(TFT_WHITE); }
    else T->setTextColor(T->color565(170, 170, 170));
    T->drawString(nm, 6, y);
  }
  T->setTextSize(1); T->setTextColor(TFT_DARKGREY);
  T->drawString("I play    II back", 6, 230);
}

bool browserBegin() {
  if (!storageInit()) { uiError("NO FILESYSTEM!"); return false; }
  nGames = storageList(games, PCE_MAX_GAMES);
  if (nGames == 0) { uiError("NO GAMES IN /PiPCE/games"); return false; }
  bSel = bTop = 0; waitRelease = true; browserDraw();
  return true;
}

BrowserResult browserUpdate(uint8_t pad) {
  uint32_t now = millis();
  if (waitRelease) { if (!pad) waitRelease = false; return BR_NONE; }
  if (!pad || now - lastNav < NAV_MS) return BR_NONE;
  lastNav = now;
  if (pad & UIPAD_B) { waitRelease = true; return BR_BACK; }
  if (pad & UIPAD_A) { waitRelease = true; return BR_SELECTED; }
  bool ch = false;
  if (pad & UIPAD_DOWN)  { if (bSel < nGames - 1) { bSel++; ch = true; } }
  if (pad & UIPAD_UP)    { if (bSel > 0) { bSel--; ch = true; } }
  if (pad & UIPAD_RIGHT) { bSel = min(nGames - 1, bSel + ROWS); ch = true; }
  if (pad & UIPAD_LEFT)  { bSel = max(0, bSel - ROWS); ch = true; }
  if (bSel < bTop) bTop = bSel;
  if (bSel >= bTop + ROWS) bTop = bSel - ROWS + 1;
  if (ch) browserDraw();
  return BR_NONE;
}
const char* browserSelected() { return games[bSel]; }

void uiLoading(const char* name) {
  T->fillScreen(TFT_BLACK); T->setTextDatum(MC_DATUM);
  T->setTextColor(TFT_WHITE); T->setTextSize(2); T->drawString("LOADING ROM", 160, 80);
  T->setTextSize(1); T->setTextColor(TFT_DARKGREY); T->drawString(name, 160, 110);
  T->drawRect(40, 140, 240, 16, TFT_DARKGREY);
}
void uiProgress(int pct) { T->fillRect(42, 142, (236 * pct) / 100, 12, T->color565(255, 160, 0)); }
void uiError(const char* msg) {
  T->fillScreen(TFT_BLACK); T->setTextDatum(MC_DATUM);
  T->setTextColor(TFT_RED); T->setTextSize(2); T->drawString(msg, 160, 120);
  delay(2000);
}
