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
// HuC6270 (VDC) + HuC6260 (VCE)
struct VdcTiming   { int activeStart, activeLines, width, frameLines; };
struct DisplayLayout { int w, off, step, h, cropTop; };   // riempita dall'host in frameBegin

extern VdcTiming     vdcT;
extern DisplayLayout vdcLay;
extern bool          vdcIrq;
extern uint16_t      vcePal565[512];

void    vdcInit();
void    vdcReset();
uint8_t vdcRead(uint8_t port);
void    vdcWrite(uint8_t port, uint8_t v);
uint8_t vceRead(uint8_t port);
void    vceWrite(uint8_t port, uint8_t v);
uint8_t vceCR();
void    vdcLineEvents(int line, int frameLines);   // a inizio riga
void    vdcRenderLine(int dy, uint16_t* out);      // a fine riga, dy = riga attiva
void    vdcLineEnd(int line);                      // sempre a fine riga

extern uint16_t vdcVram[0x8000];
