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
// PSG HuC6280: 6 canali wavetable, DDA, rumore (ch 5-6). LFO non emulato.
void    psgInit();
void    psgReset();
void    psgWrite(uint8_t reg, uint8_t v);
uint8_t psgRead(uint8_t reg);
void    psgMix(int16_t* stereo, int frames);   // genera frames campioni stereo interleaved
