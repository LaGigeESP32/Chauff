
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// Gestionnaire NRJ
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

void demandeRouteur() {
	
  if (besoinRouteur() > 10) { // si supérieur a 1 %
  setPowerPermille(ordreRouteur);
	}
  else {
    setPowerPermille(0);
  }
	notifyRouteur(1); // notif de l'ack
}


uint8_t besoinRouteur() {
  uint8_t p;

	// Définition du besoin routeur

  if (!Modesaison){
    if (tempSonde >= ConsigneHiverMax) p = 0;
    else if (tempSonde >= ConsigneHiverP4) p = 4;
    else if (tempSonde >= ConsigneHiverP3) p = 3; 
    else if (tempSonde >= ConsigneHiverP2) p = 2;
    else if (tempSonde >= 0) p = 1;
    else p = 0; // si t -100 alors on stop la demande de routage
  }
    else p = 0;
    return p;
}


void connectRouteurHard() {

  // 1) Résolution lazy mDNS -> IP (si possible)
  tryResolveLazy(gRouteur);

  // 2) Test HTTP (routeur joignable)
  HTTPClient http;
  http.setTimeout(5000);

  char httpUrl[96];
  buildUrl(httpUrl, sizeof(httpUrl), gRouteur, "/");

  http.begin(httpUrl);
  int httpCode = http.GET();
  http.end();

  if (httpCode > 0) {
    routeurIsConnected = true;

    // 3) WebSocket : démarre seulement si pas déjà connecté
    if (!WSrouteurIsConnected) {

      char wsHost[32];
      buildWsHost(wsHost, sizeof(wsHost), gRouteur); // "192.168.x.x" ou "routeur.local"

      // (Optionnel mais souvent utile) repartir clean
      webSocket.disconnect();

      webSocket.begin(wsHost, 81, "/ws");
      webSocket.onEvent(webSocketEvent);

      Serial.printf("Client WebSocket actif sur %s:81/ws\n", wsHost);
    }

  } else {
    // HTTP KO : routeur non joignable / pb réseau
    if (routeurIsConnected) {
      webSocket.disconnect();
      routeurIsConnected = false;
      WSrouteurIsConnected = false; // sécurité (l'event devrait le faire)
    }

    // Invalidation IP uniquement si erreur réseau (httpCode <= 0)
    invalidateHostIp(gRouteur);
  }
}


// Déclancheur Ws client
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
		WSrouteurIsConnected = false;
		routeurIsConnected = false; // force une reco
        if (enChauffe == 1) {
        setPowerPermille(0);
        enChauffe = 0;
      }
      break;
    case WStype_CONNECTED:
      notifyRouteur(2);
      WSrouteurIsConnected = true;
      routeurIsConnected = true;
      break;
    case WStype_TEXT: {
      // Reception du message du routeur
      char message[length + 1];
      memcpy(message, payload, length);
      message[length] = '\0'; // Ajouter un caractère de fin

      // Désérialisation du JSON
      StaticJsonDocument<512> rec;
      DeserializationError err = deserializeJson(rec, message);
      if (err) {
        return;
      }

      if (rec.containsKey("Routeur")) {
        float chauffage = rec["Chauffage"] | 0.0f;
        ordreRouteur = (int16_t)roundf(chauffage * 10.0f);
        if (enChauffe <= 1) demandeRouteur();
      }
      break;
    } 
    default:
      break;
  }
}

void notifyRouteur(int type) {

    unsigned long now = millis();

    // --- Anti-spam / Anti-flood ---
    if (type == 0) {
        if (now - lastNotifiy < 2000) {
            return; // bloque type 0 trop rapproché
        }
    }
    else if (type == 2) {
        if (now - lastNotifiy < 1000) {
            return; // bloque type 2 trop rapproché
        }
    }

    lastNotifiy = now;

    StaticJsonDocument<256> doc;
    char out[256];

    doc["name"] = "Chauffage";

    if (type == 0) {
        doc["type"] = 0;
    }
    else if (type == 1) {
        doc["type"] = 1;
        doc["power"] = (float)getAppliedPowerPermille() / 10.0f;
    }
    else if (type == 2) {

        doc["type"] = 999;
        doc["prio"] = besoinRouteur();

        // puissance (power)
        doc["power"] = (float)getAppliedPowerPermille() / 10.0f;

        // température du distributeur
        char buf[32];
        dtostrf(tempSonde, 0, 2, buf);
        doc["Température_ambiante"] = buf;

    }

    // JSON COMPACT (même format, juste sans espaces)
    size_t len = serializeJson(doc, out, sizeof(out));

    // Envoi WebSocket 
        webSocket.sendTXT(out);
    }

