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
#include "VDC.h"
#include "PCE.h"

uint16_t vdcVram[0x8000];
static uint16_t satb[256];
static uint16_t R[0x20];
static uint8_t  sel, status, wLatch;
static uint16_t readBuf;
static bool     satbPending;
static int      curY;
bool            vdcIrq = false;
VdcTiming       vdcT = {19, 240, 256, 262};
DisplayLayout   vdcLay = {256, 0, 1, 224, 0};

static uint16_t vcePal[512];
uint16_t        vcePal565[512];
static uint8_t  vceCr;
static uint16_t vceCta;

#define MAXW 512
static uint16_t bgBuf[MAXW + 16 + 16];
static uint16_t spBuf[MAXW + 64];
static uint32_t spread[256];

static inline uint16_t* bgLine() { return bgBuf + 8; }
static inline uint16_t* spLine() { return spBuf + 32; }

static void vceUpdate(int i) {
  uint16_t c = vcePal[i];
  uint16_t g = (c >> 6) & 7, r = (c >> 3) & 7, b = c & 7;
  uint16_t v = (uint16_t)((((r << 2) | (r >> 1)) << 11) | (((g << 3) | g) << 5) | ((b << 2) | (b >> 1)));
#if PCE_COLOR_SWAP
  v = (uint16_t)((v >> 8) | (v << 8));
#endif
  vcePal565[i] = v;
}

void vdcInit() {
  for (int i = 0; i < 256; i++) {
    uint32_t v = 0;
    for (int k = 0; k < 8; k++) if (i & (1 << k)) v |= 1u << (4 * k);
    spread[i] = v;
  }
}

void vdcReset() {
  memset(vdcVram, 0, sizeof(vdcVram)); memset(satb, 0, sizeof(satb)); memset(R, 0, sizeof(R));
  memset(vcePal, 0, sizeof(vcePal)); for (int i = 0; i < 512; i++) vceUpdate(i);
  sel = status = wLatch = 0; readBuf = 0; satbPending = false; curY = 0; vdcIrq = false;
  vceCr = 0; vceCta = 0;
  R[9] = 0; R[11] = 0x1F; R[12] = 0x0F02; R[13] = 0xEF; R[14] = 4;
}

static inline int incStep() { static const uint8_t t[4] = {1, 0x20, 0x40, 0x80}; return t[(R[5] >> 11) & 3]; }

static void vdcUpdate() {
  uint16_t cr = R[5];
  bool irq = ((status & 1) && (cr & 1)) || ((status & 2) && (cr & 2)) || ((status & 4) && (cr & 4)) ||
             ((status & 0x20) && (cr & 8)) || ((status & 8) && (R[15] & 1)) || ((status & 0x10) && (R[15] & 2));
  if (irq != vdcIrq) { vdcIrq = irq; pceUpdateIrq(); }
}

static void vramDma() {
  uint32_t len = (uint32_t)R[0x12] + 1;
  uint16_t src = R[0x10], dst = R[0x11];
  int ds = (R[15] & 4) ? -1 : 1, dd = (R[15] & 8) ? -1 : 1;
  for (uint32_t i = 0; i < len; i++) {
    vdcVram[dst & 0x7FFF] = vdcVram[src & 0x7FFF];
    src += ds; dst += dd;
  }
  R[0x10] = src; R[0x11] = dst;
  status |= 0x10;
}

static void sideEffects(uint8_t s, bool hi) {
  switch (s) {
    case 1:  if (hi) readBuf = vdcVram[R[1] & 0x7FFF]; break;
    case 8:  curY = ((R[8] & 0x1FF) + BYR_WRITE_ADJUST) & 0x1FF; break;
    case 0x12: if (hi) vramDma(); break;
    case 0x13: if (hi) satbPending = true; break;
  }
}

uint8_t vdcRead(uint8_t port) {
  switch (port & 3) {
    case 0: { uint8_t v = status & 0x7F; status &= 0xC0; vdcUpdate(); return v; }
    case 2: return (sel == 2) ? (readBuf & 0xFF) : 0;
    case 3: if (sel == 2) { uint8_t v = readBuf >> 8; R[1] += incStep(); readBuf = vdcVram[R[1] & 0x7FFF]; return v; } return 0;
  }
  return 0;
}

void vdcWrite(uint8_t port, uint8_t v) {
  switch (port & 3) {
    case 0: sel = v & 0x1F; break;
    case 2:
      if (sel == 2) wLatch = v; else { R[sel] = (R[sel] & 0xFF00) | v; sideEffects(sel, false); }
      vdcUpdate(); break;
    case 3:
      if (sel == 2) { if (R[0] < 0x8000) vdcVram[R[0]] = wLatch | (v << 8); R[0] += incStep(); }
      else { R[sel] = (R[sel] & 0x00FF) | (v << 8); sideEffects(sel, true); }
      vdcUpdate(); break;
  }
}

uint8_t vceCR() { return vceCr; }
uint8_t vceRead(uint8_t port) {
  switch (port & 7) {
    case 4: return vcePal[vceCta] & 0xFF;
    case 5: { uint8_t v = (vcePal[vceCta] >> 8) | 0xFE; vceCta = (vceCta + 1) & 0x1FF; return v; }
  }
  return 0xFF;
}
void vceWrite(uint8_t port, uint8_t v) {
  switch (port & 7) {
    case 0: vceCr = v; break;
    case 2: vceCta = (vceCta & 0x100) | v; break;
    case 3: vceCta = (vceCta & 0xFF) | ((v & 1) << 8); break;
    case 4: vcePal[vceCta] = (vcePal[vceCta] & 0x100) | v; vceUpdate(vceCta); break;
    case 5: vcePal[vceCta] = (vcePal[vceCta] & 0xFF) | ((v & 1) << 8); vceUpdate(vceCta); vceCta = (vceCta + 1) & 0x1FF; break;
  }
}

static void computeTiming(int frameLines) {
  int vsw = R[12] & 0x1F, vds = R[12] >> 8;
  vdcT.frameLines = frameLines;
  vdcT.activeStart = vsw + vds + 2;
  int lines = (R[13] & 0x1FF) + 1;
  if (vdcT.activeStart + lines > frameLines - 1) lines = frameLines - 1 - vdcT.activeStart;
  if (lines < 0) lines = 0;
  vdcT.activeLines = lines;
  int w = ((R[11] & 0x7F) + 1) * 8; if (w > MAXW) w = MAXW;
  vdcT.width = w;
}

void vdcLineEvents(int line, int frameLines) {
  if (line == 0) computeTiming(frameLines);
  int dy = line - vdcT.activeStart;
  if (dy == 0) curY = ((R[8] & 0x1FF) + BYR_WRITE_ADJUST) & 0x1FF;
  if (dy >= 0 && dy < vdcT.activeLines) {
    int rc = R[6] & 0x3FF;
    if (rc >= 64 && dy == rc - 64 + RASTER_IRQ_OFFSET) { status |= 4; vdcUpdate(); }
  }
  if (dy == vdcT.activeLines) {
    status |= 0x20;
    if (satbPending || (R[15] & 0x10)) {
      uint16_t a = R[0x13];
      for (int i = 0; i < 256; i++) satb[i] = vdcVram[(a + i) & 0x7FFF];
      satbPending = false; status |= 8;
    }
    vdcUpdate();
  }
}

void vdcLineEnd(int line) {
  int dy = line - vdcT.activeStart;
  if (dy >= 0 && dy < vdcT.activeLines) curY = (curY + 1) & 0x1FF;
}

static void __not_in_flash_func(renderBG)(int W) {
  uint16_t* bg = bgLine();
  if (!(R[5] & 0x80)) { memset(bg, 0, (W + 8) * 2); return; }
  uint16_t mwr = R[9];
  int mapW = 32 << ((mwr >> 4) & 3); if (mapW > 128) mapW = 128;
  int mapH = (mwr & 0x40) ? 64 : 32;
  uint32_t y = curY & (mapH * 8 - 1);
  uint32_t row = y & 7, bat = (y >> 3) * mapW;
  uint32_t mx = (R[7] & 0x3FF) & (mapW * 8 - 1);
  int x = -(int)(mx & 7);
  uint32_t tile = mx >> 3;
  while (x < W) {
    uint16_t e = vdcVram[(bat + (tile & (mapW - 1))) & 0x7FFF];
    uint32_t addr = ((uint32_t)(e & 0xFFF) << 4) + row;
    uint16_t w0 = vdcVram[addr & 0x7FFF], w1 = vdcVram[(addr + 8) & 0x7FFF];
    uint32_t p = spread[w0 & 0xFF] | (spread[w0 >> 8] << 1) | (spread[w1 & 0xFF] << 2) | (spread[w1 >> 8] << 3);
    uint16_t pb = (uint16_t)((e >> 12) << 4);
    uint16_t* o = bg + x;
    for (int i = 0; i < 8; i++) { uint32_t c = (p >> ((7 - i) * 4)) & 0xF; o[i] = c ? (uint16_t)(pb | c) : 0; }
    x += 8; tile++;
  }
}

static bool __not_in_flash_func(renderSprites)(int dy, int W) {
  uint16_t* sp = spLine();
  bool any = false; int slots = 0;
  for (int i = 0; i < 64; i++) {
    const uint16_t* s = &satb[i * 4];
    int y = (s[0] & 0x3FF) - 64;
    uint16_t attr = s[3];
    int cgy = (attr >> 12) & 3;
    int h = (cgy == 0) ? 16 : (cgy == 3 ? 64 : 32);
    int row = dy - y;
    if (row < 0 || row >= h) continue;
    int cgx = (attr >> 8) & 1, nb = cgx ? 2 : 1;
    if (slots + nb > 16) { status |= 2; vdcUpdate(); break; }
    slots += nb;
    int x = (s[1] & 0x3FF) - 32;
    if (x >= W || x + nb * 16 <= 0) continue;
    uint32_t pat = (s[2] >> 1) & 0x3FF;
    if (cgx) pat &= ~1u;
    if (cgy == 1 || cgy == 2) pat &= ~2u; else if (cgy == 3) pat &= ~6u;
    if (attr & 0x8000) row = h - 1 - row;
    int by = row >> 4, ry = row & 15;
    bool xf = attr & 0x800, pri = attr & 0x80;
    uint16_t pb = (uint16_t)(0x100 | ((attr & 0xF) << 4));
    for (int bx = 0; bx < nb; bx++) {
      int blk = xf ? (nb - 1 - bx) : bx;
      uint32_t a = ((pat + blk + by * 2) << 6) + ry;
      uint16_t p0 = vdcVram[a & 0x7FFF], p1 = vdcVram[(a + 16) & 0x7FFF], p2 = vdcVram[(a + 32) & 0x7FFF], p3 = vdcVram[(a + 48) & 0x7FFF];
      int bxp = x + bx * 16;
      for (int k = 0; k < 16; k++) {
        int px = bxp + k; if (px < 0 || px >= W) continue;
        int bit = xf ? k : 15 - k;
        uint32_t c = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1) | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3);
        if (c && !sp[px]) { sp[px] = (uint16_t)(pb | c | (pri ? 0x8000 : 0)); any = true; }
      }
    }
  }
  return any;
}

void __not_in_flash_func(vdcRenderLine)(int dy, uint16_t* out) {
  int W = vdcT.width;
  renderBG(W);
  bool sp = false;
  if (R[5] & 0x40) { memset(spBuf, 0, (W + 64) * 2); sp = renderSprites(dy, W); }
  const uint16_t* bg = bgLine(); const uint16_t* spl = spLine();
  const DisplayLayout& L = vdcLay;
  if (!sp) {
    for (int x = 0, s = L.off; x < L.w; x++, s += L.step) out[x] = vcePal565[bg[s]];
  } else {
    for (int x = 0, s = L.off; x < L.w; x++, s += L.step) {
      uint16_t b = bg[s], c = spl[s];
      if (c) b = ((c & 0x8000) || !b) ? (c & 0x1FF) : b;
      out[x] = vcePal565[b];
    }
  }
}
