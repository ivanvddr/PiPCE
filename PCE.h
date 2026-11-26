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
#include "configs.h"
#include "VDC.h"

// Hook verso l'host (core0): video a strip e audio a ring.
struct PceHost {
  bool      (*frameBegin)(int srcW, int lines);        // riempie vdcLay; false = salta il render del frame
  uint16_t* (*lineBuf)(void);                          // riga di destinazione (w pixel)
  void      (*lineDone)(void);
  void      (*frameEnd)(void);
  void      (*audioOut)(const int16_t* stereo, int frames);   // puo' bloccare (backpressure)
};
extern PceHost pceHost;

// bit pad (attivi alti): 0 I, 1 II, 2 SELECT, 3 RUN, 4 UP, 5 RIGHT, 6 DOWN, 7 LEFT
extern uint8_t padButtons;
extern uint8_t pceRAM[0x2000];

void pceInit();
// ROM: 'resident' = prime residentSize byte letti direttamente (XIP/RAM).
// Se totalSize > residentSize il resto viene richiesto pagina per pagina (8KB) con fetch().
// Qualsiasi dimensione: >1MB usa il mapper a finestre da 512KB ($1FF0-$1FF3, stile SF2).
typedef const uint8_t* (*PceFetchPageFn)(uint32_t filePage);
bool pceSetRom(const uint8_t* resident, uint32_t residentSize, uint32_t totalSize, PceFetchPageFn fetch);
void pceReset();
void pceRunFrame(bool render);
void pceUpdateIrq();
