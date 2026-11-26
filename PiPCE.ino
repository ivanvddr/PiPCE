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
 *
 * PiPCE.ino - Emulatore PC Engine / TurboGrafx-16 per Raspberry Pi Pico (RP2040), Arduino IDE
 * GPLv3
 *
 * Architettura:
 *  - CORE0 (setup/loop): CPU HuC6280 + VDC/VCE + PSG + input. Il VDC e' renderizzato
 *    RIGA PER RIGA (raster effect fedeli) in strip da STRIP_LINES righe; il PSG produce
 *    i campioni audio riga per riga (2,8 campioni/riga) nel ring SPSC.
 *  - CORE1 (setup1/loop1): invio strip al TFT via DMA + pump audio ring -> libreria I2S
 *    (MAX98357A). Il clock audio e' il bit-clock I2S: write16() bloccante fa da freno
 *    (backpressure) e quindi da orologio dell'intera emulazione.
 *  - Timing reale > framerate: se l'audio sta per andare a secco (ring sotto soglia) o se
 *    il display non e' ancora libero, il frame video viene saltato ma l'emulazione prosegue.
 *  - ROM: copiata da SD/LittleFS in una zona RAW di flash e letta in XIP (zero RAM).
 */
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <I2S.h>
#include "hardware/vreg.h"
#include "hardware/clocks.h"

#include "configs.h"
#include "PCE.h"
#include "ring_buffer_spsc.h"
#include "audio_pipeline.h"
#include "PceStorage.h"
#include "PceUI.h"

#pragma GCC optimize("O3")

// ============================================================================
// OGGETTI GLOBALI
// ============================================================================
TFT_eSPI tft = TFT_eSPI();
I2S i2sOut(OUTPUT);
AudioRing audioRing;

volatile bool core1CanStart = false;
volatile bool core1BlitPaused = false;   // core0 -> core1
volatile bool core1BlitActive = false;   // core1 -> core0
volatile bool audioMute = false;         // core0 -> core1: silenzia l'I2S (scritture in flash)
volatile bool audioMuteAck = false;      // core1 -> core0: buffer I2S azzerati

enum AppState { STATE_MENU, STATE_BROWSER, STATE_EMULATION };
AppState currentState = STATE_MENU;
bool gameLoaded = false;

// ============================================================================
// INPUT
// ============================================================================
struct DebouncedPin { uint8_t pin; bool stable = false; bool last = false; uint32_t changeMs = 0; };
static DebouncedPin pins[9] = {
  {PIN_UP}, {PIN_DOWN}, {PIN_LEFT}, {PIN_RIGHT}, {PIN_CENTER},
  {PIN_BTN_I}, {PIN_BTN_II}, {PIN_BTN_SELECT}, {PIN_BTN_RUN}
};
enum { P_UP, P_DOWN, P_LEFT, P_RIGHT, P_CENTER, P_I, P_II, P_SELECT, P_RUN };

static void setupInputPins() { for (auto& p : pins) pinMode(p.pin, INPUT_PULLUP); }
static void pollPins() {
  uint32_t now = millis();
  for (auto& p : pins) {
    bool raw = !digitalRead(p.pin);
    if (raw != p.last) { p.last = raw; p.changeMs = now; }
    else if (raw != p.stable && (now - p.changeMs) >= DEBOUNCE_MS) p.stable = raw;
  }
}
static uint8_t uiPad() {
  pollPins();
  uint8_t pad = 0;
  if (pins[P_UP].stable)    pad |= UIPAD_UP;
  if (pins[P_DOWN].stable)  pad |= UIPAD_DOWN;
  if (pins[P_LEFT].stable)  pad |= UIPAD_LEFT;
  if (pins[P_RIGHT].stable) pad |= UIPAD_RIGHT;
  if (pins[P_I].stable)     pad |= UIPAD_A;
  if (pins[P_II].stable)    pad |= UIPAD_B;
  return pad;
}
static void pollInputEmu() {
  pollPins();
  uint8_t b = 0;
  if (pins[P_I].stable)      b |= 0x01;
  if (pins[P_II].stable)     b |= 0x02;
  if (pins[P_SELECT].stable) b |= 0x04;
  if (pins[P_RUN].stable)    b |= 0x08;
  if (pins[P_UP].stable)     b |= 0x10;
  if (pins[P_RIGHT].stable)  b |= 0x20;
  if (pins[P_DOWN].stable)   b |= 0x40;
  if (pins[P_LEFT].stable)   b |= 0x80;
  padButtons = b;
}

// ============================================================================
// VIDEO: ring di strip  (core0 produce, core1 invia via DMA)
// ============================================================================
struct Strip { uint16_t buf[STRIP_LINES * TFT_W]; int16_t x, y, w, rows; bool last, clear; };
static Strip strips[NUM_STRIPS];
static volatile uint8_t stHead = 0, stTail = 0;
static volatile bool displayBusy = false;
static int fX, fY, fH, curRow, curLine, lastW = -1, lastH = -1;
static bool lastPub, needClear;

static bool hostFrameBegin(int srcW, int lines) {
  if (displayBusy) return false;                       // il display non e' ancora libero: salta
  int step = srcW > 352 ? 2 : 1, weff = srcW / step;
  int w = weff > TFT_W ? TFT_W : weff, h = lines > TFT_H ? TFT_H : lines;
  vdcLay.w = w; vdcLay.step = step; vdcLay.off = ((weff - w) / 2) * step;
  vdcLay.h = h; vdcLay.cropTop = (lines - h) / 2;
  fX = (TFT_W - w) / 2; fY = (TFT_H - h) / 2; fH = h;
  needClear = (w != lastW || h != lastH); lastW = w; lastH = h;
  curRow = 0; curLine = 0; lastPub = false; displayBusy = true;
  return true;
}
static inline void waitFreeStrip() {
  while (((stHead + 1) % NUM_STRIPS) == stTail) tight_loop_contents();
}
static void publishStrip(int rows, bool forceLast) {
  Strip& s = strips[stHead];
  s.x = fX; s.y = fY + curLine; s.w = vdcLay.w; s.rows = rows;
  curLine += rows;
  s.last = forceLast || (curLine >= fH);
  s.clear = needClear; needClear = false;
  if (s.last) lastPub = true;
  __sync_synchronize();
  stHead = (stHead + 1) % NUM_STRIPS;
  curRow = 0;
}
static uint16_t* hostLineBuf() {
  if (curRow == 0) waitFreeStrip();
  return strips[stHead].buf + curRow * vdcLay.w;
}
static void hostLineDone() { if (++curRow == STRIP_LINES) publishStrip(STRIP_LINES, false); }
static void hostFrameEnd() {
  if (curRow > 0) publishStrip(curRow, true);
  else if (!lastPub) { waitFreeStrip(); publishStrip(0, true); }
}

// ============================================================================
// AUDIO: produttore (core0) / consumatore (core1)
// ============================================================================
static void hostAudioOut(const int16_t* st, int n) {
  for (int i = 0; i < n; i++) {
    uint32_t v = audioPack(st[2 * i], st[2 * i + 1]);
    uint32_t t0 = micros();
    while (!audioRing.push(v)) {                       // backpressure: l'I2S e' l'orologio
      if (micros() - t0 > 100000) return;
      tight_loop_contents();
    }
  }
}
static void pumpAudio(uint32_t maxSamples) {
  uint32_t v;
  if (audioMute) {
    // Durante le scritture in flash core1 viene fermato e gli IRQ disabilitati per decine di ms:
    // il DMA I2S rilegge in loop gli ultimi buffer. Li riempio di zeri => il loop e' silenzio.
    while (audioRing.pop(v)) {}
    if (!audioMuteAck) {
      for (int i = 0; i < AUDIO_I2S_BUFFERS * AUDIO_I2S_WORDS * 2; i++) i2sOut.write16(0, 0);
      audioMuteAck = true;
    }
    return;
  }
  while (maxSamples-- && audioRing.pop(v)) i2sOut.write16((int16_t)(v & 0xFFFF), (int16_t)(v >> 16));
}

// ============================================================================
// GESTIONE STATI
// ============================================================================
static void pauseCore1Blit() {
  core1BlitPaused = true;
  uint32_t t0 = millis();
  while (core1BlitActive && (millis() - t0 < 300)) tight_loop_contents();
}
static void resumeCore1Blit() { core1BlitPaused = false; }
static void audioMuteBegin() {
  audioMuteAck = false; audioMute = true;
  uint32_t t0 = millis();
  while (!audioMuteAck && (millis() - t0 < 500)) tight_loop_contents();
}
static void audioMuteEnd() { audioMute = false; }

static void cleanupTFT() {
  tft.endWrite();
  uint32_t t0 = millis();
  while (tft.dmaBusy() && (millis() - t0 < 200)) delay(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
}
static void startVideo() {
  tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLACK);
  tft.startWrite();
  lastW = lastH = -1;
  displayBusy = false; stHead = stTail = 0;
}

static void goToMenu() {
  pauseCore1Blit();
  while (displayBusy && stHead != stTail) delay(1);   // lascia finire le strip in volo
  cleanupTFT();
  menuDraw(gameLoaded);
  resumeCore1Blit();
  currentState = STATE_MENU;
}
static void resumeEmulation() {
  pauseCore1Blit();
  startVideo();
  resumeCore1Blit();
  currentState = STATE_EMULATION;
}
static void launchGame(const char* name) {
  pauseCore1Blit();
  cleanupTFT();
  uiLoading(name);
  audioMuteBegin();
  int rc = storageOpenRom(name, uiProgress);
  audioMuteEnd();
  if (rc != 0) {
    uiError(storageError(rc));
    gameLoaded = false;
    resumeCore1Blit();
    goToMenu();
    return;
  }
  bool streamed = storageRomTotalSize() > storageRomResidentSize();
  if (!pceSetRom(storageRomPtr(), storageRomResidentSize(), storageRomTotalSize(), streamed ? storageFetchPage : nullptr)) {
    uiError("ROM NOT SUPPORTED");
    gameLoaded = false;
    resumeCore1Blit();
    goToMenu();
    return;
  }
  pceReset();
  padButtons = 0;
  gameLoaded = true;
  startVideo();
  resumeCore1Blit();
  currentState = STATE_EMULATION;
}

static void handleMenuState() {
  MenuAction a = menuUpdate(uiPad(), gameLoaded);
  if (a == MENU_BROWSER) { if (browserBegin()) currentState = STATE_BROWSER; else menuDraw(gameLoaded); }
  else if (a == MENU_RESUME) resumeEmulation();
  delay(10);
}
static void handleBrowserState() {
  BrowserResult r = browserUpdate(uiPad());
  if (r == BR_SELECTED) {
    static char sel[PCE_NAME_MAX];
    strncpy(sel, browserSelected(), PCE_NAME_MAX - 1); sel[PCE_NAME_MAX - 1] = 0;
    launchGame(sel);
  } else if (r == BR_BACK) { menuDraw(gameLoaded); currentState = STATE_MENU; }
  delay(10);
}

static void handleEmulationState() {
  static uint8_t skipRun = 0;
  static bool late = false;
  static uint32_t exitStart = 0;

  pollInputEmu();
  if (pins[P_CENTER].stable) {
    if (!exitStart) exitStart = millis();
    else if (millis() - exitStart >= EXIT_HOLD_MS) { exitStart = 0; goToMenu(); return; }
  } else exitStart = 0;

  // Timing reale > framerate: se l'audio e' quasi a secco non si disegna (max N di fila)
  bool render = !(late && skipRun < PACING_MAX_CONSECUTIVE_SKIP);
  if (render) skipRun = 0; else skipRun++;

  pceRunFrame(render);          // l'audio blocca qui quando il ring e' pieno (= tempo reale)

  late = audioRing.available() < AUDIO_GUARDRAIL_LOW;
}

// ============================================================================
// CORE0
// ============================================================================
void setup() {
  vreg_set_voltage(VREG_VOLTAGE_1_30);
  delay(10);
  if (!set_sys_clock_khz(PICO_CLOCK_KHZ, true)) set_sys_clock_khz(250000, true);
  uint32_t f = clock_get_hz(clk_sys);
  clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, f, f);   // SPI fino a clk_sys/2
  __sync_synchronize();
  delay(100);

  setupInputPins();

  tft.init();
  tft.setRotation(TFT_ROTATION);
  tft.initDMA();
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);

  pceInit();
  pceHost.frameBegin = hostFrameBegin;
  pceHost.lineBuf    = hostLineBuf;
  pceHost.lineDone   = hostLineDone;
  pceHost.frameEnd   = hostFrameEnd;
  pceHost.audioOut   = hostAudioOut;

  storageInit();                 // SD con fallback LittleFS (non fatale)
  uiInit(&tft);
  menuDraw(false);

  core1CanStart = true;          // core1: I2S + pump audio + blit
}

void loop() {
  switch (currentState) {
    case STATE_MENU:      handleMenuState();      break;
    case STATE_BROWSER:   handleBrowserState();   break;
    case STATE_EMULATION: handleEmulationState(); break;
  }
}

// ============================================================================
// CORE1: blit DMA a strip + pump audio
// ============================================================================
void setup1() {
  while (!core1CanStart) tight_loop_contents();
  i2sOut.setBCLK(AUDIO_BCLK_PIN);          // LRCLK = BCLK + 1
  i2sOut.setDATA(AUDIO_DATA_PIN);
  i2sOut.setBitsPerSample(16);
  i2sOut.setBuffers(AUDIO_I2S_BUFFERS, AUDIO_I2S_WORDS, 0);
  i2sOut.begin(AUDIO_SAMPLE_RATE);
}

void loop1() {
  core1BlitActive = true;
  if (core1BlitPaused) {                   // core0 possiede il display: solo audio
    core1BlitActive = false;
    pumpAudio(512);
    return;
  }
  if (stTail != stHead) {
    Strip& s = strips[stTail];
    if (s.clear) tft.fillScreen(TFT_BLACK);
    if (s.rows > 0) {
      tft.pushImageDMA(s.x, s.y, s.w, s.rows, s.buf);
      pumpAudio(AUDIO_STRIP_PUMP_CAP);     // pompa MENTRE la strip viaggia sullo SPI
      tft.dmaWait();
    }
    bool last = s.last;
    __sync_synchronize();
    stTail = (stTail + 1) % NUM_STRIPS;
    if (last) displayBusy = false;
    core1BlitActive = false;
  } else {
    core1BlitActive = false;
    pumpAudio(256);
  }
}
