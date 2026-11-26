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
// Filesystem (SD con fallback LittleFS) + installazione ROM in flash XIP.
#define PCE_NAME_MAX   32
#define PCE_MAX_GAMES  200

bool           storageInit();
const char*    storageName();                                   // "SD" / "LittleFS"
int            storageList(char names[][PCE_NAME_MAX], int maxN);
// Apre /PiPCE/games/<name>, qualsiasi dimensione:
//  - i primi min(size, ROM_FLASH_SIZE) byte vengono copiati in flash e letti in XIP (settori identici saltati)
//  - l'eventuale resto resta nel file e viene letto a pagine da 8KB tramite una cache LRU in RAM
// Ritorna 0 = OK, altrimenti codice errore (vedi storageError).
int            storageOpenRom(const char* name, void (*progress)(int pct));
const char*    storageError(int code);
const uint8_t* storageRomPtr();             // zona residente (XIP)
uint32_t       storageRomResidentSize();
uint32_t       storageRomTotalSize();
const uint8_t* storageFetchPage(uint32_t filePage);   // per ROM non residenti (puo' bloccare ~ms su SD)
