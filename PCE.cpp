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
#include "platform.h"
#include "configs.h"
#include "PCE.h"
#include "HuC6280.h"
#include "PSG.h"
#include <stdlib.h>

PceHost pceHost;
uint8_t padButtons = 0;
uint8_t pceRAM[0x2000];

static const uint8_t* romData = 0;
static uint32_t romPages = 0, romResPages = 0;
static PceFetchPageFn romFetch = 0;
static uint8_t  sf2Sel = 0, sf2Mask = 0;
static bool     isSf2 = false, isPopulous = false;
static uint8_t* extraRam = 0;              // Populous: 32KB su banks $40-$43
static bool     extraRamOn = false;
static uint8_t  irqMask = 0;               // 1 = disabilitata (b0 IRQ2, b1 IRQ1, b2 timer)
static bool     timerIrq = false, timerOn = false;
static uint8_t  timerReload = 0, timerCnt = 0;
static int      timerAcc = 0;
static uint8_t  padSel = 0, padClr = 0, padCount = 0;
static int      carry = 0;
static uint32_t sampAcc = 0;

void pceInit() { vdcInit(); psgInit(); }

bool pceSetRom(const uint8_t* resident, uint32_t residentSize, uint32_t totalSize, PceFetchPageFn fetch) {
  if (totalSize < 0x2000 || !resident) return false;
  romData = resident; romPages = (totalSize + 0x1FFF) >> 13;
  romResPages = (residentSize + 0x1FFF) >> 13; if (romResPages > romPages) romResPages = romPages;
  romFetch = fetch; sf2Sel = 0;
  if (romResPages < romPages && !fetch) return false;

  // Mapper a finestre (stile Street Fighter II): ROM >= 2MB
  uint32_t mlen = romPages << 13;
  isSf2 = mlen >= 2048u * 1024u;
  if (isSf2) {
    uint32_t ml;
    if (mlen < 512u * 1024 * 6) ml = 512u * 1024 * 5;
    else { uint32_t t = mlen - 512u * 1024, p2 = 1; while (p2 < t) p2 <<= 1; ml = p2 + 512u * 1024; }
    sf2Mask = (uint8_t)(((ml - 512u * 1024) / (512u * 1024)) - 1);
  }
  // Populous: card con 32KB di RAM extra, riconosciuta dalla stringa a $1F26
  isPopulous = !isSf2 && residentSize >= 0x2000 && memcmp(resident + 0x1F26, "POPULOUS", 8) == 0;
  return true;
}

// banco logico (0..$7F) -> pagina da 8KB nel file
static uint32_t filePageOf(uint8_t v) {
  if (isSf2) {                                           // $00-$3F fissi, $40-$7F = finestra da 512KB selezionata
    if (v < 0x40) return v;
    return 64 + (uint32_t)(sf2Sel & sf2Mask) * 64 + (v - 0x40);
  }
  if (romPages == 48) return (v < 0x40) ? (v & 0x1F) : ((v & 0x0F) + 32);   // 384KB: 256K ripetuti + 128K ripetuti
  if (romPages == 64) return (v < 0x40) ? v : ((v & 0x1F) + 32);            // 512KB: la meta' alta e' ripetuta
  return v % romPages;
}
static void remapAll() { for (int i = 0; i < 8; i++) cpuSetMPR(i, cpu.mpr[i]); }

const uint8_t* pceMapRead(uint8_t v) {
  if (v <= 0x7F) {
    if (extraRamOn && v >= 0x40 && v <= 0x43) return extraRam + ((v - 0x40) << 13);
    if (!romData) return 0;
    uint32_t fp = filePageOf(v);
    return (fp < romResPages) ? romData + (fp << 13) : 0;   // 0 = slow path (streaming / fuori file)
  }
  if (v >= 0xF8 && v <= 0xFB) return pceRAM;
  return 0;
}
uint8_t* pceMapWrite(uint8_t v) {
  if (v >= 0xF8 && v <= 0xFB) return pceRAM;
  if (extraRamOn && v >= 0x40 && v <= 0x43) return extraRam + ((v - 0x40) << 13);
  return 0;
}

uint8_t pceRomSlowRead(uint8_t v, uint16_t off) {
  static uint32_t lastFp = 0xFFFFFFFFu; static const uint8_t* lastPtr = 0;
  if (!romData) return 0xFF;
  uint32_t fp = filePageOf(v);
  if (fp >= romPages) return 0xFF;
  if (fp < romResPages) return romData[(fp << 13) + off];
  if (!romFetch) return 0xFF;
  if (fp != lastFp) { lastPtr = romFetch(fp); lastFp = lastPtr ? fp : 0xFFFFFFFFu; }
  return lastPtr ? lastPtr[off] : 0xFF;
}
void pceRomSlowWrite(uint8_t v, uint16_t off, uint8_t val) {
  // Mapper SF2: scrittura su $1FF0-$1FFF del banco ROM 0 = selezione finestra
  if (isSf2 && v == 0 && (off & 0x1FF0) == 0x1FF0) { sf2Sel = off & 0x0F; remapAll(); }
}

void pceUpdateIrq() {
  uint8_t p = 0;
  if (vdcIrq)   p |= 2;
  if (timerIrq) p |= 4;
  cpu.irq = p & (uint8_t)~irqMask & 7;
}

uint8_t pceIoRead(uint16_t off) {
  switch (off >> 10) {
    case 0: return vdcRead(off & 3);
    case 1: return vceRead(off & 7);
    case 2: return psgRead(off & 15);
    case 3: return (off & 1) ? (timerOn ? 1 : 0) : timerCnt;
    case 4: {
      uint8_t v;
      if (padCount) v = 0x0F;
      else if (padSel) v = (uint8_t)(~(padButtons >> 4)) & 0xF;
      else v = (uint8_t)(~padButtons) & 0xF;
      return v | 0xB0;
    }
    case 5:
      if ((off & 3) == 2) return irqMask;
      if ((off & 3) == 3) return (uint8_t)((timerIrq ? 4 : 0) | (vdcIrq ? 2 : 0));
      return 0xFF;
    default: return 0xFF;
  }
}

void pceIoWrite(uint16_t off, uint8_t v) {
  switch (off >> 10) {
    case 0: vdcWrite(off & 3, v); break;
    case 1: vceWrite(off & 7, v); break;
    case 2: psgWrite(off & 15, v); break;
    case 3:
      if (off & 1) { bool on = v & 1; if (on && !timerOn) { timerCnt = timerReload; timerAcc = 0; } timerOn = on; }
      else timerReload = v & 0x7F;
      break;
    case 4: {
      uint8_t oldSel = padSel; padSel = v & 1; padClr = (v >> 1) & 1;
      if (padClr) padCount = 0; else if (padSel && !oldSel && padCount < 5) padCount++;
    } break;
    case 5:
      if ((off & 3) == 2) { irqMask = v & 7; pceUpdateIrq(); }
      else if ((off & 3) == 3) { timerIrq = false; pceUpdateIrq(); }
      break;
    default: break;
  }
}

void pceReset() {
  memset(pceRAM, 0, sizeof(pceRAM));
  vdcReset(); psgReset();
  irqMask = 0; timerIrq = timerOn = false; timerReload = timerCnt = 0; timerAcc = 0;
  padSel = padClr = padCount = 0; carry = 0; sampAcc = 0; sf2Sel = 0;
  if (extraRam) { free(extraRam); extraRam = 0; } extraRamOn = false;
  if (isPopulous) { extraRam = (uint8_t*)malloc(0x8000); if (extraRam) { memset(extraRam, 0xFF, 0x8000); extraRamOn = true; } }
  cpuReset();
}

static inline void timerStep(int ticks) {
  if (!timerOn) return;
  timerAcc += ticks;
  while (timerAcc >= 1024) {
    timerAcc -= 1024;
    if (timerCnt == 0) { timerCnt = timerReload; timerIrq = true; pceUpdateIrq(); }
    else timerCnt--;
  }
}

void __not_in_flash_func(pceRunFrame)(bool render) {
  int lines = (vceCR() & 4) ? 263 : 262;
  bool drawing = false, lastDone = false; (void)lastDone;
  int16_t abuf[16];
  for (int line = 0; line < lines; line++) {
    vdcLineEvents(line, lines);
    int dy = line - vdcT.activeStart;
    if (dy == 0 && render && vdcT.activeLines > 0 && pceHost.frameBegin) drawing = pceHost.frameBegin(vdcT.width, vdcT.activeLines);

    int budget = PCE_TICKS_PER_LINE - carry;
    if (budget > 0) carry = cpuRun(budget); else carry = -budget;
    timerStep(PCE_TICKS_PER_LINE);

    if (drawing && dy >= 0 && dy < vdcT.activeLines) {
      int ly = dy - vdcLay.cropTop;
      if (ly >= 0 && ly < vdcLay.h) {
        uint16_t* b = pceHost.lineBuf();
        vdcRenderLine(dy, b);
        pceHost.lineDone();
      }
    }
    vdcLineEnd(line);

    sampAcc += AUDIO_SAMPLE_RATE;
    int n = (int)(sampAcc / PCE_LINE_RATE);
    sampAcc -= (uint32_t)n * PCE_LINE_RATE;
    if (n > 0) { if (n > 8) n = 8; psgMix(abuf, n); if (pceHost.audioOut) pceHost.audioOut(abuf, n); }
  }
  if (drawing && pceHost.frameEnd) pceHost.frameEnd();
}
