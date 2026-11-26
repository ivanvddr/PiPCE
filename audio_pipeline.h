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
// Pipeline audio:
//   CORE0 produce campioni (PSG, guidati dai cicli emulati) nel ring SPSC
//   CORE1 li consuma verso la libreria I2S (write16 bloccante = clock/backpressure)
#include <stdint.h>
#include "configs.h"
#include "ring_buffer_spsc.h"

typedef RingBufferSPSC<uint32_t, AUDIO_RING_CAPACITY> AudioRing;   // L | (R << 16)

static inline uint32_t audioPack(int16_t l, int16_t r) { return (uint16_t)l | ((uint32_t)(uint16_t)r << 16); }
