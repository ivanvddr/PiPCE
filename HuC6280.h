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
// HuC6280 (65C02 + estensioni PC Engine). Tempo in "tick" a 7.16MHz.
struct HuC6280 {
  uint8_t  A, X, Y, S;
  uint16_t PC;
  uint8_t  fN, fZ, fC, fV, fD, fI, fT;   // N = bit7 di fN, Z = (fZ==0)
  uint8_t  shift;                         // 0 = 7.16MHz, 2 = 1.79MHz (CSL)
  uint8_t  irq;                           // sorgenti pendenti mascherate: b0 IRQ2, b1 IRQ1, b2 TIMER
  uint8_t  mpr[8];
  const uint8_t* rd[8];
  uint8_t*       wr[8];
};
extern HuC6280 cpu;

void cpuReset();
int  cpuRun(int budgetTicks);              // ritorna i tick di "debito" oltre il budget
void cpuSetMPR(int i, uint8_t v);

// forniti da PCE.cpp
uint8_t        pceIoRead(uint16_t off);
void           pceIoWrite(uint16_t off, uint8_t v);
const uint8_t* pceMapRead(uint8_t mprValue);
uint8_t*       pceMapWrite(uint8_t mprValue);
uint8_t        pceRomSlowRead(uint8_t mprValue, uint16_t offs);
void           pceRomSlowWrite(uint8_t mprValue, uint16_t offs, uint8_t v);
