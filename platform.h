#pragma once
// Piccolo strato di portabilita': permette di compilare il core (CPU/VDC/PSG/PCE)
// anche su PC per i test, senza Arduino.
#include <stdint.h>
#include <string.h>
#ifdef ARDUINO
  #include <Arduino.h>
#endif
#ifndef __not_in_flash_func
  #define __not_in_flash_func(f) f
#endif
