#pragma once
#include <stdint.h>

enum LedColor : uint8_t {
  LED_OFF = 0,
  LED_GREEN,
  LED_BLUE,
  LED_RED,
  LED_WHITE
};

enum PageWeb : uint8_t {
  PAGE_UNKNOWN = 0,
  PAGE_HOME,
  PAGE_GESTION,
  PAGE_MODIF_GESTION
};