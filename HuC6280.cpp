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
#include "HuC6280.h"

HuC6280 cpu;

#ifdef CPU_TEST
uint8_t testMem[65536];
static inline uint8_t RD(uint16_t a) { return testMem[a]; }
static inline void    WR(uint16_t a, uint8_t v) { testMem[a] = v; }
#define ZPB 0x0000
#define STB 0x0100
#define BRKVEC 0xFFFE
#else
static uint8_t busRdSlow(uint16_t a) {
  uint8_t m = cpu.mpr[a >> 13];
  if (m == 0xFF) return pceIoRead(a & 0x1FFF);
  if (m <= 0x7F) return pceRomSlowRead(m, a & 0x1FFF);     // ROM non residente (streaming) o fuori mappa
  return 0xFF;
}
static void busWrSlow(uint16_t a, uint8_t v) {
  uint8_t m = cpu.mpr[a >> 13];
  if (m == 0xFF) pceIoWrite(a & 0x1FFF, v);
  else if (m <= 0x7F) pceRomSlowWrite(m, a & 0x1FFF, v);   // RAM extra su card (es. Populous)
}
static inline uint8_t RD(uint16_t a) { const uint8_t* p = cpu.rd[a >> 13]; return p ? p[a & 0x1FFF] : busRdSlow(a); }
static inline void    WR(uint16_t a, uint8_t v) { uint8_t* p = cpu.wr[a >> 13]; if (p) p[a & 0x1FFF] = v; else busWrSlow(a, v); }
#define ZPB 0x2000
#define STB 0x2100
#define BRKVEC 0xFFF6
#endif

static inline uint16_t zpRd16(uint8_t z) { return RD(ZPB | z) | (RD(ZPB | (uint8_t)(z + 1)) << 8); }

#ifndef CPU_TEST
void cpuSetMPR(int i, uint8_t v) {
  cpu.mpr[i] = v;
  cpu.rd[i] = pceMapRead(v);
  cpu.wr[i] = pceMapWrite(v);
}
#endif

// cicli base per opcode (HuC6280)
static const uint8_t cycTbl[256] = {
 /*00*/ 8,7,3,4,6,4,6,7,3,2,2,2,7,5,7,6,
 /*10*/ 2,7,7,4,6,4,6,7,2,5,2,2,7,5,7,6,
 /*20*/ 7,7,3,4,4,4,6,7,4,2,2,2,5,5,7,6,
 /*30*/ 2,7,7,2,4,4,6,7,2,5,2,2,5,5,7,6,
 /*40*/ 7,7,3,4,8,4,6,7,3,2,2,2,4,5,7,6,
 /*50*/ 2,7,7,5,3,4,6,7,2,5,3,2,2,5,7,6,
 /*60*/ 7,7,2,2,4,4,6,7,4,2,2,2,7,5,7,6,
 /*70*/ 2,7,7,17,4,4,6,7,2,5,4,2,7,5,7,6,
 /*80*/ 4,7,2,7,4,4,4,7,2,2,2,2,5,5,5,6,
 /*90*/ 2,7,7,8,4,4,4,7,2,5,2,2,5,5,5,6,
 /*A0*/ 2,7,2,7,4,4,4,7,2,2,2,2,5,5,5,6,
 /*B0*/ 2,7,7,8,4,4,4,7,2,5,2,2,5,5,5,6,
 /*C0*/ 2,7,2,17,4,4,6,7,2,2,2,2,5,5,7,6,
 /*D0*/ 2,7,7,17,3,4,6,7,2,5,3,2,2,5,7,6,
 /*E0*/ 2,7,2,17,4,4,6,7,2,2,2,2,5,5,7,6,
 /*F0*/ 2,7,7,17,2,4,6,7,2,5,4,2,2,5,7,6 };

void cpuReset() {
  cpu.A = cpu.X = cpu.Y = 0; cpu.S = 0xFF;
  cpu.fN = 0; cpu.fZ = 1; cpu.fC = cpu.fV = cpu.fD = cpu.fT = 0; cpu.fI = 1;
  cpu.shift = 0; cpu.irq = 0;
#ifndef CPU_TEST
  for (int i = 0; i < 8; i++) cpuSetMPR(i, 0xF8);
  cpuSetMPR(7, 0x00);
#endif
  cpu.PC = RD(0xFFFE) | (RD(0xFFFF) << 8);
}

#define SETNZ(v)   (fN = fZ = (uint8_t)(v))
#define PUSH(v)    do { WR(STB | S, (v)); S--; } while (0)
#define PULL()     (S++, RD(STB | S))
#ifdef CPU_TEST
#define TPACK 1
#define TUNP(p) 0
#else
#define TPACK fT
#define TUNP(p) (((p) >> 5) & 1)
#endif
#define PACK(b)    ((uint8_t)((fN & 0x80) | (fV << 6) | (TPACK << 5) | ((b) << 4) | (fD << 3) | (fI << 2) | ((fZ == 0) << 1) | fC))
#define UNPACK(p)  do { uint8_t _p = (p); fN = _p; fV = (_p >> 6) & 1; fT = TUNP(_p); fD = (_p >> 3) & 1; fI = (_p >> 2) & 1; fZ = (_p & 2) ? 0 : 1; fC = _p & 1; } while (0)

#define EA_ZP    ((uint16_t)(ZPB | RD(PC++)))
#define EA_ZPX   ((uint16_t)(ZPB | (uint8_t)(RD(PC++) + X)))
#define EA_ZPY   ((uint16_t)(ZPB | (uint8_t)(RD(PC++) + Y)))
#define EA_ABS   (ea = RD(PC) | (RD((uint16_t)(PC + 1)) << 8), PC += 2, ea)
#define EA_ABSX  ((uint16_t)(EA_ABS + X))
#define EA_ABSY  ((uint16_t)(EA_ABS + Y))
#define EA_INDX  zpRd16((uint8_t)(RD(PC++) + X))
#define EA_INDY  ((uint16_t)(zpRd16(RD(PC++)) + Y))
#define EA_IND   zpRd16(RD(PC++))

#define ADC_ON(ac, m) do { unsigned _s = (unsigned)(ac) + (m) + fC; \
  fV = (((~((ac) ^ (m))) & ((ac) ^ _s)) >> 7) & 1; \
  if (fD) { unsigned lo = ((ac) & 0xF) + ((m) & 0xF) + fC; if (lo > 9) lo += 6; \
            unsigned hi = ((ac) >> 4) + ((m) >> 4) + (lo > 0xF); if (hi > 9) hi += 6; \
            fC = hi > 0xF; (ac) = (uint8_t)((hi << 4) | (lo & 0xF)); cyc++; } \
  else { fC = _s >> 8; (ac) = (uint8_t)_s; } \
  SETNZ(ac); } while (0)

#define OP_ADC(m) do { uint8_t _m = (m); if (tNow) { uint8_t t = RD(ZPB | X); ADC_ON(t, _m); WR(ZPB | X, t); cyc += 3; } else { ADC_ON(A, _m); } } while (0)
#define OP_SBC(m) do { uint8_t _m = (m); \
  if (fD) { int lo = (A & 0xF) - (_m & 0xF) - (1 - fC); int hi = (A >> 4) - (_m >> 4); int d = (int)A - _m - (1 - fC); \
            fV = ((A ^ _m) & (A ^ d) & 0x80) ? 1 : 0; if (lo < 0) { lo -= 6; hi--; } if (hi < 0) hi -= 6; \
            fC = (d >= 0); A = (uint8_t)((hi << 4) | (lo & 0xF)); SETNZ(A); cyc++; } \
  else { uint8_t _n = (uint8_t)~_m; ADC_ON(A, _n); } } while (0)
#define OP_ORA(m) do { uint8_t _m = (m); if (tNow) { uint8_t t = RD(ZPB | X) | _m; WR(ZPB | X, t); SETNZ(t); cyc += 3; } else { A |= _m; SETNZ(A); } } while (0)
#define OP_AND(m) do { uint8_t _m = (m); if (tNow) { uint8_t t = RD(ZPB | X) & _m; WR(ZPB | X, t); SETNZ(t); cyc += 3; } else { A &= _m; SETNZ(A); } } while (0)
#define OP_EOR(m) do { uint8_t _m = (m); if (tNow) { uint8_t t = RD(ZPB | X) ^ _m; WR(ZPB | X, t); SETNZ(t); cyc += 3; } else { A ^= _m; SETNZ(A); } } while (0)
#define OP_CMP(m) do { uint8_t _m = (m); fC = A >= _m; SETNZ((uint8_t)(A - _m)); } while (0)
#define OP_LDA(m) do { A = (m); SETNZ(A); } while (0)
#define OP_CPX(m) do { uint8_t _m = (m); fC = X >= _m; SETNZ((uint8_t)(X - _m)); } while (0)
#define OP_CPY(m) do { uint8_t _m = (m); fC = Y >= _m; SETNZ((uint8_t)(Y - _m)); } while (0)
#define OP_LDX(m) do { X = (m); SETNZ(X); } while (0)
#define OP_LDY(m) do { Y = (m); SETNZ(Y); } while (0)
#define OP_BIT(m) do { uint8_t _m = (m); fZ = A & _m; fN = _m; fV = (_m >> 6) & 1; } while (0)

#define ASL(v) do { fC = (v) >> 7; (v) <<= 1; SETNZ(v); } while (0)
#define LSR(v) do { fC = (v) & 1; (v) >>= 1; SETNZ(v); } while (0)
#define ROL(v) do { uint8_t _c = fC; fC = (v) >> 7; (v) = (uint8_t)(((v) << 1) | _c); SETNZ(v); } while (0)
#define ROR(v) do { uint8_t _c = fC; fC = (v) & 1; (v) = (uint8_t)(((v) >> 1) | (_c << 7)); SETNZ(v); } while (0)
#define INCV(v) do { (v)++; SETNZ(v); } while (0)
#define DECV(v) do { (v)--; SETNZ(v); } while (0)

#define RMW(eaexpr, OP) do { uint16_t _a = (eaexpr); uint8_t _v = RD(_a); OP(_v); WR(_a, _v); } while (0)
#define RMW_CASES(b, OP) \
  case (b):        RMW(EA_ZP, OP);   break; \
  case (b) + 0x10: RMW(EA_ZPX, OP);  break; \
  case (b) + 0x08: RMW(EA_ABS, OP);  break; \
  case (b) + 0x18: RMW(EA_ABSX, OP); break;

#define ALU_CASES(b, OP) \
  case (b) + 0x05: OP(RD(EA_ZP));   break; \
  case (b) + 0x15: OP(RD(EA_ZPX));  break; \
  case (b) + 0x0D: OP(RD(EA_ABS));  break; \
  case (b) + 0x1D: OP(RD(EA_ABSX)); break; \
  case (b) + 0x19: OP(RD(EA_ABSY)); break; \
  case (b) + 0x01: OP(RD(EA_INDX)); break; \
  case (b) + 0x11: OP(RD(EA_INDY)); break; \
  case (b) + 0x12: OP(RD(EA_IND));  break;
#define ALU_GROUP(b, OP) ALU_CASES(b, OP) case (b) + 0x09: OP(RD(PC++)); break;

#define BRANCH(cond) do { int8_t _o = (int8_t)RD(PC++); if (cond) { PC += _o; cyc += 2; } } while (0)

int __not_in_flash_func(cpuRun)(int budget) {
  uint8_t A = cpu.A, X = cpu.X, Y = cpu.Y, S = cpu.S;
  uint16_t PC = cpu.PC, ea;
  uint8_t fN = cpu.fN, fZ = cpu.fZ, fC = cpu.fC, fV = cpu.fV, fD = cpu.fD, fI = cpu.fI, fT = cpu.fT;
  uint8_t sh = cpu.shift;
  int used = 0;

  while (used < budget) {
    if (cpu.irq && !fI) {
      uint8_t pend = cpu.irq;
      uint16_t vec = (pend & 1) ? 0xFFF6 : (pend & 2) ? 0xFFF8 : 0xFFFA;
      PUSH(PC >> 8); PUSH(PC & 0xFF); PUSH(PACK(0));
      fI = 1; fD = 0; fT = 0;
      PC = RD(vec) | (RD(vec + 1) << 8);
      used += 8 << sh;
      continue;
    }
    uint8_t op = RD(PC++);
    int cyc = cycTbl[op];
    uint8_t tNow = fT; fT = 0;

    switch (op) {
      ALU_GROUP(0x00, OP_ORA)
      ALU_GROUP(0x20, OP_AND)
      ALU_GROUP(0x40, OP_EOR)
      ALU_GROUP(0x60, OP_ADC)
      ALU_GROUP(0xA0, OP_LDA)
      ALU_GROUP(0xC0, OP_CMP)
      ALU_GROUP(0xE0, OP_SBC)
      // STA
      case 0x85: WR(EA_ZP, A);   break;
      case 0x95: WR(EA_ZPX, A);  break;
      case 0x8D: WR(EA_ABS, A);  break;
      case 0x9D: WR(EA_ABSX, A); break;
      case 0x99: WR(EA_ABSY, A); break;
      case 0x81: WR(EA_INDX, A); break;
      case 0x91: WR(EA_INDY, A); break;
      case 0x92: WR(EA_IND, A);  break;
      // STX / STY / STZ
      case 0x86: WR(EA_ZP, X);   break;
      case 0x96: WR(EA_ZPY, X);  break;
      case 0x8E: WR(EA_ABS, X);  break;
      case 0x84: WR(EA_ZP, Y);   break;
      case 0x94: WR(EA_ZPX, Y);  break;
      case 0x8C: WR(EA_ABS, Y);  break;
      case 0x64: WR(EA_ZP, 0);   break;
      case 0x74: WR(EA_ZPX, 0);  break;
      case 0x9C: WR(EA_ABS, 0);  break;
      case 0x9E: WR(EA_ABSX, 0); break;
      // LDX / LDY / CPX / CPY
      case 0xA2: OP_LDX(RD(PC++)); break;
      case 0xA6: OP_LDX(RD(EA_ZP)); break;
      case 0xB6: OP_LDX(RD(EA_ZPY)); break;
      case 0xAE: OP_LDX(RD(EA_ABS)); break;
      case 0xBE: OP_LDX(RD(EA_ABSY)); break;
      case 0xA0: OP_LDY(RD(PC++)); break;
      default: goto more;
    }
    goto done;
more:
    switch (op) {
      RMW_CASES(0x06, ASL)
      RMW_CASES(0x26, ROL)
      RMW_CASES(0x46, LSR)
      RMW_CASES(0x66, ROR)
      RMW_CASES(0xE6, INCV)
      RMW_CASES(0xC6, DECV)
      case 0x0A: ASL(A); break;
      case 0x2A: ROL(A); break;
      case 0x4A: LSR(A); break;
      case 0x6A: ROR(A); break;
      case 0x1A: INCV(A); break;
      case 0x3A: DECV(A); break;
      case 0xE8: INCV(X); break;
      case 0xC8: INCV(Y); break;
      case 0xCA: DECV(X); break;
      case 0x88: DECV(Y); break;
      // BIT
      case 0x89: fZ = A & RD(PC++); break;
      case 0x24: OP_BIT(RD(EA_ZP)); break;
      case 0x34: OP_BIT(RD(EA_ZPX)); break;
      case 0x2C: OP_BIT(RD(EA_ABS)); break;
      case 0x3C: OP_BIT(RD(EA_ABSX)); break;
      // LDY / CPX / CPY
      case 0xA4: OP_LDY(RD(EA_ZP)); break;
      case 0xB4: OP_LDY(RD(EA_ZPX)); break;
      case 0xAC: OP_LDY(RD(EA_ABS)); break;
      case 0xBC: OP_LDY(RD(EA_ABSX)); break;
      case 0xE0: OP_CPX(RD(PC++)); break;
      case 0xE4: OP_CPX(RD(EA_ZP)); break;
      case 0xEC: OP_CPX(RD(EA_ABS)); break;
      case 0xC0: OP_CPY(RD(PC++)); break;
      case 0xC4: OP_CPY(RD(EA_ZP)); break;
      case 0xCC: OP_CPY(RD(EA_ABS)); break;
      // trasferimenti
      case 0xAA: X = A; SETNZ(X); break;
      case 0xA8: Y = A; SETNZ(Y); break;
      case 0x8A: A = X; SETNZ(A); break;
      case 0x98: A = Y; SETNZ(A); break;
      case 0xBA: X = S; SETNZ(X); break;
      case 0x9A: S = X; break;
      case 0x62: A = 0; break;            // CLA
      case 0x82: X = 0; break;            // CLX
      case 0xC2: Y = 0; break;            // CLY
      case 0x22: { uint8_t t = A; A = X; X = t; } break;   // SAX
      case 0x42: { uint8_t t = A; A = Y; Y = t; } break;   // SAY
      case 0x02: { uint8_t t = X; X = Y; Y = t; } break;   // SXY
      // flag
      case 0x18: fC = 0; break;
      case 0x38: fC = 1; break;
      case 0x58: fI = 0; break;
      case 0x78: fI = 1; break;
      case 0xB8: fV = 0; break;
      case 0xD8: fD = 0; break;
      case 0xF8: fD = 1; break;
      case 0xF4: fT = 1; break;           // SET
      case 0x54: sh = cpu.shift = 2; break;  // CSL
      case 0xD4: sh = cpu.shift = 0; break;  // CSH
      // stack
      case 0x48: PUSH(A); break;
      case 0x5A: PUSH(Y); break;
      case 0xDA: PUSH(X); break;
      case 0x08: PUSH(PACK(1)); break;
      case 0x68: A = PULL(); SETNZ(A); break;
      case 0x7A: Y = PULL(); SETNZ(Y); break;
      case 0xFA: X = PULL(); SETNZ(X); break;
      case 0x28: { uint8_t p = PULL(); UNPACK(p); } break;
      // salti
      case 0x4C: PC = EA_ABS; break;
      case 0x6C: { uint16_t a = EA_ABS; PC = RD(a) | (RD((uint16_t)(a + 1)) << 8); } break;
      case 0x7C: { uint16_t a = (uint16_t)(EA_ABS + X); PC = RD(a) | (RD((uint16_t)(a + 1)) << 8); } break;
      case 0x20: { uint16_t a = EA_ABS; uint16_t r = PC - 1; PUSH(r >> 8); PUSH(r & 0xFF); PC = a; } break;
      case 0x44: { int8_t o = (int8_t)RD(PC++); uint16_t r = PC - 1; PUSH(r >> 8); PUSH(r & 0xFF); PC += o; } break;
      case 0x60: { uint8_t lo = PULL(); uint8_t hi = PULL(); PC = (uint16_t)(((hi << 8) | lo) + 1); } break;
      case 0x40: { uint8_t p = PULL(); UNPACK(p); uint8_t lo = PULL(); uint8_t hi = PULL(); PC = (hi << 8) | lo; } break;
      case 0x00: { PC++; PUSH(PC >> 8); PUSH(PC & 0xFF); PUSH(PACK(1)); fI = 1; fD = 0;
                   PC = RD(BRKVEC) | (RD(BRKVEC + 1) << 8); } break;
      // branch
      case 0x10: BRANCH(!(fN & 0x80)); break;
      case 0x30: BRANCH(fN & 0x80); break;
      case 0x50: BRANCH(!fV); break;
      case 0x70: BRANCH(fV); break;
      case 0x90: BRANCH(!fC); break;
      case 0xB0: BRANCH(fC); break;
      case 0xD0: BRANCH(fZ != 0); break;
      case 0xF0: BRANCH(fZ == 0); break;
      case 0x80: { int8_t o = (int8_t)RD(PC++); PC += o; } break;
      // TSB / TRB
      case 0x04: { uint16_t a = EA_ZP;  uint8_t v = RD(a); fN = v; fV = (v >> 6) & 1; fZ = v & A; WR(a, v | A); } break;
      case 0x0C: { uint16_t a = EA_ABS; uint8_t v = RD(a); fN = v; fV = (v >> 6) & 1; fZ = v & A; WR(a, v | A); } break;
      case 0x14: { uint16_t a = EA_ZP;  uint8_t v = RD(a); fN = v; fV = (v >> 6) & 1; fZ = v & A; WR(a, v & ~A); } break;
      case 0x1C: { uint16_t a = EA_ABS; uint8_t v = RD(a); fN = v; fV = (v >> 6) & 1; fZ = v & A; WR(a, v & ~A); } break;
      // TST #imm, mem
      case 0x83: { uint8_t m = RD(PC++); uint8_t v = RD(EA_ZP);   fN = v; fV = (v >> 6) & 1; fZ = m & v; } break;
      case 0xA3: { uint8_t m = RD(PC++); uint8_t v = RD(EA_ZPX);  fN = v; fV = (v >> 6) & 1; fZ = m & v; } break;
      case 0x93: { uint8_t m = RD(PC++); uint8_t v = RD(EA_ABS);  fN = v; fV = (v >> 6) & 1; fZ = m & v; } break;
      case 0xB3: { uint8_t m = RD(PC++); uint8_t v = RD(EA_ABSX); fN = v; fV = (v >> 6) & 1; fZ = m & v; } break;
      // VDC diretto
      case 0x03: { uint8_t v = RD(PC++); pceIoWrite(0x0000, v); } break;
      case 0x13: { uint8_t v = RD(PC++); pceIoWrite(0x0002, v); } break;
      case 0x23: { uint8_t v = RD(PC++); pceIoWrite(0x0003, v); } break;
      // mapper
      case 0x53: { uint8_t m = RD(PC++); for (int i = 0; i < 8; i++) if (m & (1 << i)) cpuSetMPR(i, A); } break;
      case 0x43: { uint8_t m = RD(PC++); for (int i = 0; i < 8; i++) if (m & (1 << i)) { A = cpu.mpr[i]; break; } } break;
      // block transfer
      case 0x73: case 0xC3: case 0xD3: case 0xE3: case 0xF3: {
        uint16_t s = RD(PC) | (RD((uint16_t)(PC + 1)) << 8);
        uint16_t d = RD((uint16_t)(PC + 2)) | (RD((uint16_t)(PC + 3)) << 8);
        uint32_t n = RD((uint16_t)(PC + 4)) | (RD((uint16_t)(PC + 5)) << 8);
        PC += 6; if (!n) n = 65536;
        cyc += 6 * (int)n;
        for (uint32_t i = 0; i < n; i++) {
          switch (op) {
            case 0x73: WR(d, RD(s)); s++; d++; break;                    // TII
            case 0xC3: WR(d, RD(s)); s--; d--; break;                    // TDD
            case 0xD3: WR(d, RD(s)); s++; break;                         // TIN
            case 0xE3: WR((uint16_t)(d + (i & 1)), RD(s)); s++; break;   // TIA
            default:   WR(d, RD((uint16_t)(s + (i & 1)))); d++; break;   // TAI
          }
        }
      } break;
      default:
        if ((op & 0x0F) == 0x07) {                     // RMBn / SMBn
          uint16_t a = EA_ZP; uint8_t bit = (op >> 4) & 7; uint8_t v = RD(a);
          if (op & 0x80) v |= (1 << bit); else v &= ~(1 << bit);
          WR(a, v);
        } else if ((op & 0x0F) == 0x0F) {              // BBRn / BBSn
          uint8_t z = RD(PC++); int8_t o = (int8_t)RD(PC++); uint8_t bit = (op >> 4) & 7;
          uint8_t v = RD(ZPB | z);
          if (((v >> bit) & 1) == ((op & 0x80) ? 1 : 0)) { PC += o; cyc += 2; }
        }
        break;                                         // altri = NOP
    }
done:
    used += cyc << sh;
  }
  cpu.A = A; cpu.X = X; cpu.Y = Y; cpu.S = S; cpu.PC = PC;
  cpu.fN = fN; cpu.fZ = fZ; cpu.fC = fC; cpu.fV = fV; cpu.fD = fD; cpu.fI = fI; cpu.fT = fT;
  return used - budget;
}
