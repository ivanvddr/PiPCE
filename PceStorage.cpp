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
#include <SD.h>
#include <LittleFS.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include "configs.h"
#include "PceStorage.h"

extern "C" char __flash_binary_end;

enum { FS_NONE, FS_SD, FS_LFS };
static int      fsType = FS_NONE;
static uint32_t romSize = 0, romResident = 0, romSkip = 0;
static File     romFile;
static uint8_t* cacheBase = 0;
static uint32_t cachePg[ROM_CACHE_PAGES], cacheAge[ROM_CACHE_PAGES], cacheTick = 0;
static uint8_t  sectorBuf[FLASH_SECTOR_SIZE];

static File fsOpen(const char* path) {
  if (fsType == FS_SD)  return SD.open(path, "r");
  if (fsType == FS_LFS) return LittleFS.open(path, "r");
  return File();
}

bool storageInit() {
  if (fsType != FS_NONE) return true;
  SPI1.setRX(SD_MISO); SPI1.setTX(SD_MOSI); SPI1.setSCK(SD_SCLK); SPI1.setCS(SD_CS);
  SPI1.begin();
  delay(100);
  if (SD.begin(SD_CS, SD_SPI_HZ, SPI1)) { fsType = FS_SD; return true; }
  if (LittleFS.begin()) { fsType = FS_LFS; return true; }
  return false;
}
const char* storageName() { return fsType == FS_SD ? "SD" : fsType == FS_LFS ? "LittleFS" : "NONE"; }

static bool hasExt(const char* n) {
  size_t l = strlen(n), e = strlen(GAME_EXT);
  if (l <= e) return false;
  for (size_t i = 0; i < e; i++) if (tolower(n[l - e + i]) != GAME_EXT[i]) return false;
  return true;
}

int storageList(char names[][PCE_NAME_MAX], int maxN) {
  String dirPath = String(GAME_DIR) + GAMES_SUBDIR;
  File dir = fsOpen(dirPath.c_str());
  if (!dir || !dir.isDirectory()) return 0;
  int n = 0;
  File e = dir.openNextFile();
  while (e && n < maxN) {
    if (!e.isDirectory()) {
      const char* nm = e.name();
      if (hasExt(nm)) { strncpy(names[n], nm, PCE_NAME_MAX - 1); names[n][PCE_NAME_MAX - 1] = 0; n++; }
    }
    e.close();
    e = dir.openNextFile();
  }
  dir.close();
  for (int i = 1; i < n; i++) {                       // insertion sort (case-insensitive)
    char t[PCE_NAME_MAX]; memcpy(t, names[i], PCE_NAME_MAX);
    int j = i - 1;
    while (j >= 0 && strcasecmp(names[j], t) > 0) { memcpy(names[j + 1], names[j], PCE_NAME_MAX); j--; }
    memcpy(names[j + 1], t, PCE_NAME_MAX);
  }
  return n;
}

static bool sectorErased(const uint8_t* p) {
  const uint32_t* w = (const uint32_t*)p;
  for (int i = 0; i < FLASH_SECTOR_SIZE / 4; i++) if (w[i] != 0xFFFFFFFFu) return false;
  return true;
}

// scrive un settore solo se diverso; core1 messo in pausa (lockout) e IRQ disabilitati
static bool flashWriteSector(uint32_t off, const uint8_t* data) {
  const uint8_t* xip = (const uint8_t*)(XIP_BASE + off);
  if (memcmp(xip, data, FLASH_SECTOR_SIZE) == 0) return true;
  bool needErase = !sectorErased(xip);
  rp2040.idleOtherCore();
  uint32_t ints = save_and_disable_interrupts();
  if (needErase) flash_range_erase(off, FLASH_SECTOR_SIZE);
  flash_range_program(off, data, FLASH_SECTOR_SIZE);
  restore_interrupts(ints);
  rp2040.resumeOtherCore();
  return memcmp(xip, data, FLASH_SECTOR_SIZE) == 0;
}

int storageOpenRom(const char* name, void (*progress)(int pct)) {
  if ((uint32_t)(uintptr_t)&__flash_binary_end - XIP_BASE > ROM_FLASH_OFFSET) return 1;   // sketch troppo grande
  if (romFile) romFile.close();                                                          // gioco precedente
  if (cacheBase) { free(cacheBase); cacheBase = 0; }
  romSize = romResident = romSkip = 0;

  String path = String(GAME_DIR) + GAMES_SUBDIR + "/" + name;
  File f = fsOpen(path.c_str());
  if (!f) return 2;
  uint32_t fsz = f.size(), skip = 0;
  if (fsz & 0x200) { skip = 0x200; f.seek(skip); }                    // header copier da 512 byte
  uint32_t sz = fsz - skip;
  if (sz < 0x2000) { f.close(); return 3; }

  uint32_t res = sz < ROM_FLASH_SIZE ? sz : ROM_FLASH_SIZE;           // parte residente in flash
  uint32_t done = 0;
  while (done < res) {
    memset(sectorBuf, 0xFF, sizeof(sectorBuf));
    uint32_t want = min((uint32_t)FLASH_SECTOR_SIZE, res - done), got = 0;
    while (got < want) { int r = f.read(sectorBuf + got, want - got); if (r <= 0) break; got += r; }
    if (got != want) { f.close(); return 5; }
    if (!flashWriteSector(ROM_FLASH_OFFSET + done, sectorBuf)) { f.close(); return 6; }
    done += FLASH_SECTOR_SIZE;
    if (progress) progress((int)((uint64_t)min(done, res) * 100 / res));
  }

  if (sz > res) {                                                      // resto in streaming
    cacheBase = (uint8_t*)malloc((size_t)ROM_CACHE_PAGES * 8192);
    if (!cacheBase) { f.close(); return 7; }
    for (int i = 0; i < ROM_CACHE_PAGES; i++) { cachePg[i] = 0xFFFFFFFFu; cacheAge[i] = 0; }
    cacheTick = 0;
    romFile = f;                                                       // resta aperto per tutta la partita
  } else {
    f.close();
  }
  romSize = sz; romResident = res; romSkip = skip;
  return 0;
}

const uint8_t* storageFetchPage(uint32_t fp) {
  if (!cacheBase || !romFile) return 0;
  int victim = 0;
  for (int i = 0; i < ROM_CACHE_PAGES; i++) {
    if (cachePg[i] == fp) { cacheAge[i] = ++cacheTick; return cacheBase + (size_t)i * 8192; }
    if (cacheAge[i] < cacheAge[victim]) victim = i;
  }
  uint8_t* d = cacheBase + (size_t)victim * 8192;
  uint32_t got = 0;
  if (romFile.seek(romSkip + fp * 8192)) {
    while (got < 8192) { int r = romFile.read(d + got, 8192 - got); if (r <= 0) break; got += r; }
  }
  if (got < 8192) memset(d + got, 0xFF, 8192 - got);                   // ultima pagina parziale / errore lettura
  cachePg[victim] = fp; cacheAge[victim] = ++cacheTick;
  return d;
}

const char* storageError(int c) {
  switch (c) {
    case 1: return "SKETCH TOO BIG FOR ROM AREA";
    case 2: return "CANNOT OPEN FILE";
    case 3: return "ROM TOO SMALL";
    case 4: return "ROM TOO BIG";
    case 7: return "NO RAM FOR ROM CACHE";
    case 5: return "READ ERROR";
    case 6: return "FLASH VERIFY ERROR";
  }
  return "OK";
}
const uint8_t* storageRomPtr() { return (const uint8_t*)(XIP_BASE + ROM_FLASH_OFFSET); }
uint32_t storageRomResidentSize() { return romResident; }
uint32_t storageRomTotalSize() { return romSize; }
