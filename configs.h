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

// ---------------- Clock -------------------------------------------------
#define PICO_CLOCK_KHZ        300000

// ---------------- Display ILI9341 (pin in TFT_eSPI User_Setup) ----------
//  MISO 16 | CS 17 | SCK 18 | MOSI 19 | RST 20 | DC 21
#define TFT_ROTATION          1
#define TFT_W                 320
#define TFT_H                 240
#define STRIP_LINES           16          // righe per strip DMA
#define NUM_STRIPS            4           // strip nel ring video (4 x 10KB)

// ---------------- SD (SPI1) ---------------------------------
#define SD_SCLK               10
#define SD_MOSI               11
#define SD_MISO               12
#define SD_CS                 13
#define SD_SPI_HZ             10000000    // alza a 25000000 se la tua SD regge: meno stalli sulle ROM in streaming

// ---------------- Audio I2S (MAX98357A) --------------------
//  BCLK->26, LRC->27 (automatico), DIN->28, VIN 5V, GND comune
#define AUDIO_BCLK_PIN        26
#define AUDIO_DATA_PIN        28
#define AUDIO_SAMPLE_RATE     44100       // 32000 / 22050 per alleggerire il core0
#define AUDIO_RING_CAPACITY   512         // campioni stereo (potenza di 2)
#define AUDIO_I2S_BUFFERS     4
#define AUDIO_I2S_WORDS       128
#define AUDIO_STRIP_PUMP_CAP  48          // campioni pompati per strip
#define AUDIO_GUARDRAIL_LOW   96          // ring sotto questa soglia = emulazione in ritardo
#define PSG_VOLUME            600

// ---------------- Input (attivi bassi, pull-up -------------
#define PIN_UP                2
#define PIN_DOWN              3
#define PIN_LEFT              4
#define PIN_RIGHT             5
#define PIN_CENTER            22          // 5a via: tenuto premuto = torna al menu
#define PIN_BTN_I             6           // PiLynx: A
#define PIN_BTN_II            7           // PiLynx: B
#define PIN_BTN_SELECT        0           // PiLynx: C
#define PIN_BTN_RUN           1           // PiLynx: PAUSE
#define DEBOUNCE_MS           15
#define EXIT_HOLD_MS          800

// ---------------- Timing PC Engine ---------------------------------------
#define PCE_TICKS_PER_LINE    455         // clock 7.16MHz
#define PCE_LINE_RATE         15734
#define PCE_LINES_262         262
#define PACING_MAX_CONSECUTIVE_SKIP 3     // max frame video saltati di fila
#define RASTER_IRQ_OFFSET     0           // regola fine IRQ raster (+/- righe)
#define BYR_WRITE_ADJUST      0

// ---------------- ROM in flash (XIP) --------------------------------------
//  La ROM viene copiata (SD/LittleFS -> flash RAW) e letta in XIP.
//  Default Pico 2MB, Flash Size "2MB (Sketch: 1MB, FS: 1MB)":
//  ultimi 512KB della zona sketch (0x80000..0xFFFFF). Lo sketch deve stare < 512KB.
//  Pico 4MB (Sketch 2MB / FS 2MB): OFFSET 0x100000, SIZE 0x100000.
#define ROM_FLASH_OFFSET      0x080000u
#define ROM_FLASH_SIZE        0x080000u

// ROM piu' grandi della zona flash: il resto e' letto a pagine da 8KB dal file (SD/LittleFS)
// tramite cache LRU in RAM (allocata solo se serve). Ogni pagina = 8KB.
#define ROM_CACHE_PAGES       8
//  ROM > 1MB (es. 2,5MB tipo Street Fighter II): mapper a finestre da 512KB su $1FF0-$1FF3.

#define PCE_COLOR_SWAP        0           // 1 = scambia i byte dei colori (se i colori sono errati)

// ---------------- Cartelle -------------------------------------------------
#define GAME_DIR              "/PiPCE"
#define GAMES_SUBDIR          "/games"
#define GAME_EXT              ".pce"
