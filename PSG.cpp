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
#include "PSG.h"
#include <math.h>

struct Ch {
  uint16_t freq; uint8_t ctrl, bal, noise, dda, widx; uint8_t wave[32];
  uint32_t phase, step, nphase, nstep, lfsr; int32_t gl, gr;
};
static Ch ch[6];
static uint8_t selCh, mainBal;
static float ampCh[32], ampPan[16];
#define PSG_CLK_PER_SAMPLE_Q16 ((uint32_t)((3579545.0 / AUDIO_SAMPLE_RATE) * 65536.0))

static void gains(Ch& c) {
  int cv = c.ctrl & 0x1F, bl = c.bal >> 4, br = c.bal & 15, ml = mainBal >> 4, mr = mainBal & 15;
  c.gl = (cv && bl && ml) ? (int32_t)(PSG_VOLUME * ampCh[cv] * ampPan[bl] * ampPan[ml]) : 0;
  c.gr = (cv && br && mr) ? (int32_t)(PSG_VOLUME * ampCh[cv] * ampPan[br] * ampPan[mr]) : 0;
}
static void gainsAll() { for (int i = 0; i < 6; i++) gains(ch[i]); }
static void steps(Ch& c) {
  uint32_t p = c.freq ? c.freq : 4096;
  c.step = PSG_CLK_PER_SAMPLE_Q16 / p;
  uint32_t nf = 31 - (c.noise & 0x1F);
  uint32_t np = nf ? nf * 64 : 32;
  c.nstep = PSG_CLK_PER_SAMPLE_Q16 / np;
}

void psgInit() {
  for (int i = 0; i < 32; i++) ampCh[i] = powf(10.0f, -1.5f * (31 - i) / 20.0f);
  for (int i = 0; i < 16; i++) ampPan[i] = powf(10.0f, -3.0f * (15 - i) / 20.0f);
}
void psgReset() {
  memset(ch, 0, sizeof(ch)); for (int i = 0; i < 6; i++) { ch[i].lfsr = 0x4000; steps(ch[i]); }
  selCh = 0; mainBal = 0; gainsAll();
}
uint8_t psgRead(uint8_t reg) { return (reg & 15) == 0 ? selCh : 0; }

void psgWrite(uint8_t reg, uint8_t v) {
  reg &= 15;
  if (reg == 0) { selCh = v & 7; return; }
  if (reg == 1) { mainBal = v; gainsAll(); return; }
  if (selCh > 5) return;
  Ch& c = ch[selCh];
  switch (reg) {
    case 2: c.freq = (c.freq & 0xF00) | v; steps(c); break;
    case 3: c.freq = (c.freq & 0x0FF) | ((v & 0xF) << 8); steps(c); break;
    case 4: c.ctrl = v; if (v & 0x40) { c.phase = 0; c.widx = 0; } gains(c); break;
    case 5: c.bal = v; gains(c); break;
    case 6:
      if (c.ctrl & 0x80) { if (c.ctrl & 0x40) c.dda = v & 0x1F; }
      else { c.wave[c.widx] = v & 0x1F; c.widx = (c.widx + 1) & 31; if (c.ctrl & 0x40) c.dda = v & 0x1F; }
      break;
    case 7: if (selCh >= 4) { c.noise = v; steps(c); } break;
    default: break;   // LFO ignorato
  }
}

void __not_in_flash_func(psgMix)(int16_t* out, int frames) {
  for (int n = 0; n < frames; n++) {
    int32_t l = 0, r = 0;
    for (int i = 0; i < 6; i++) {
      Ch& c = ch[i];
      if (!(c.ctrl & 0x80)) continue;
      int v;
      if (c.ctrl & 0x40) v = c.dda;
      else if (i >= 4 && (c.noise & 0x80)) {
        c.nphase += c.nstep;
        while (c.nphase >= 65536) { c.nphase -= 65536; c.lfsr = (c.lfsr >> 1) | (((c.lfsr ^ (c.lfsr >> 1)) & 1) << 14); }
        v = (c.lfsr & 1) ? 0x1F : 0;
      } else { v = c.wave[(c.phase >> 16) & 31]; c.phase += c.step; }
      v -= 16;
      l += v * c.gl; r += v * c.gr;
    }
    if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
    if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
    out[2 * n] = (int16_t)l; out[2 * n + 1] = (int16_t)r;
  }
}
