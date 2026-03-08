void maintientTemp() {
  if (Activemaintien == 1) {
    if ((tempSonde < Tmaintien) &&
        (enChauffe == 0) &&
        (strcmp(JourJ, "ROUGE") != 0)) {
      setPowerPermille(Pmaintien * 10);
      enChauffe = 2;
      notifyRouteur(2);
    }
    else if ((enChauffe == 2) &&
             (tempSonde < Tmaintien) &&
             (getAppliedPowerPermille() != (uint16_t)(Pmaintien * 10))) {
      setPowerPermille(Pmaintien * 10);
      notifyRouteur(2);
    }
    else if ((enChauffe == 5) && (tempSonde >= Tmaintien)) {
      setPowerPermille(0);
      enChauffe = 0;
      notifyRouteur(2);
    }
  }
  else {
    if (enChauffe == 2) {
      enChauffe = 0;
      setPowerPermille(0);
      notifyRouteur(2);
    }
  }
}


static void applyLedColorIfChanged(LedColor color, uint8_t power) {
  if ((color == g_lastLedColor) && (power == g_lastLedPower)) {
    return;
  }

  g_lastLedColor = color;
  g_lastLedPower = power;

  const uint8_t v = (uint8_t)(2U * power);

  switch (color) {
    case LED_GREEN:
      strip.setPixelColor(0, strip.Color(0, v, 0));
      break;

    case LED_BLUE:
      strip.setPixelColor(0, strip.Color(0, 0, v));
      break;

    case LED_RED:
      strip.setPixelColor(0, strip.Color(v, 0, 0));
      break;

    case LED_WHITE:
      strip.setPixelColor(0, strip.Color(v, v, v));
      break;

    case LED_OFF:
    default:
      strip.setPixelColor(0, strip.Color(0, 0, 0));
      break;
  }

  strip.show();
}



void EtatLed() {
  LedColor color = LED_OFF;
  const uint8_t power = 20;

  if (Modesaison == 1) {
    color = LED_OFF;       // priorité 1
  }
  else if (enChauffe == 2) {
    color = LED_RED;       // priorité 2
  }
  else if (enChauffe == 1) {
    color = LED_GREEN;     // priorité 3
  }
  else if (routeurIsConnected) {
    color = LED_BLUE;      // priorité 4
  }
  else {
    color = LED_WHITE;     // priorité 5 => Modesaison == 0
  }

  applyLedColorIfChanged(color, power);
}


