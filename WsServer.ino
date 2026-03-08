
#include "types.h"

// ---------- Helpers ----------
static PageWeb pageFromText(const char* s) {
  if (s == nullptr) return PAGE_UNKNOWN;
  if (strcmp(s, "Home") == 0) return PAGE_HOME;
  if (strcmp(s, "Gestion") == 0) return PAGE_GESTION;
  if (strcmp(s, "ModifGestion") == 0) return PAGE_MODIF_GESTION;
  return PAGE_UNKNOWN;
}

static const char* pageToText(PageWeb page) {
  switch (page) {
    case PAGE_HOME:          return "Home";
    case PAGE_GESTION:       return "Gestion";
    case PAGE_MODIF_GESTION: return "ModifGestion";
    default:                 return "Unknown";
  }
}

static void formatHHMM(int totalMinutes, char* out, size_t outSize) {
  if (out == nullptr || outSize < 6) return;
  int hh = totalMinutes / 60;
  int mm = totalMinutes % 60;
  snprintf(out, outSize, "%02d:%02d", hh, mm);
}

static bool jsonChanged(const char* current, const char* previous) {
  if (current == nullptr || previous == nullptr) return true;
  return strcmp(current, previous) != 0;
}

void initWsServeur() {
  ws.onEvent(onWebSocketEvent);
  Serial.println("Serveur WebSocket actif sur le port 80 !");
}

void onWebSocketEvent(AsyncWebSocket* server,
                      AsyncWebSocketClient* client,
                      AwsEventType type,
                      void* arg,
                      uint8_t* data,
                      size_t len) {
  (void)server;

  switch (type) {
    case WS_EVT_CONNECT:
      Serial.print("Page Web connectee, client #");
      Serial.println(client->id());
      break;

    case WS_EVT_DISCONNECT: {
      Serial.print("Page Web deconnectee, client #");
      Serial.println(client->id());

      auto it = clientPages.find(client->id());
      if (it != clientPages.end()) {
        clientPages.erase(it);
      }
      break;
    }

    case WS_EVT_PONG:
      break;

    case WS_EVT_DATA:
      handleWebSocketMessage(client, arg, data, len);
      break;

    case WS_EVT_ERROR:
    default:
      break;
  }
}


void handleWebSocketMessage(AsyncWebSocketClient* client,
                            void* arg,
                            uint8_t* data,
                            size_t len) {
  AwsFrameInfo* info = reinterpret_cast<AwsFrameInfo*>(arg);

  if (info == nullptr) return;

  if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) {
    return;
  }

  if (len == 0 || len >= WS_RX_BUFFER_SIZE) {
    Serial.println("WS RX ignore: taille invalide");
    return;
  }

  char payload[WS_RX_BUFFER_SIZE];
  memcpy(payload, data, len);
  payload[len] = '\0';

  StaticJsonDocument<JSON_DOC_RX_SIZE> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("JSON invalide: ");
    Serial.println(err.c_str());
    return;
  }

  // -------- Identification page en cours --------
  const char* pageWebEnCours = doc["pageWebEnCours"] | nullptr;
  if (pageWebEnCours != nullptr) {
    const PageWeb page = pageFromText(pageWebEnCours);
    clientPages[client->id()] = page;

    IPAddress ip = client->remoteIP();
    Serial.printf("%u.%u.%u.%u : %s\n", ip[0], ip[1], ip[2], ip[3], pageToText(page));

    char tx[JSON_NOTIFY_GESTION_SIZE];

    if (page == PAGE_HOME) {
      if (buildNotifyHome(tx, sizeof(tx), true)) {
        client->text(tx);
      } else {
        client->text(tx); // premier envoi forcé ou cache identique, on envoie quand même
      }
      return;
    }

    if (page == PAGE_GESTION) {
      if (buildNotifyGestion(tx, sizeof(tx), true)) {
        client->text(tx);
      } else {
        client->text(tx);
      }
      return;
    }

    if (page == PAGE_MODIF_GESTION) {
      Serial.println("RetourGestion");
      Serial.println(payload);

      const int oldActiveRouteur = ActiveRouteur;
      const int newActiveRouteur = doc["ActiveRouteur"] | ActiveRouteur;

      if (oldActiveRouteur == 0 && newActiveRouteur == 1) {
        connectRouteurHard();
      }

      Modesaison                 = doc["modeSaison"]                 | Modesaison;
      modeSaisonAuto             = doc["modeSaisonAuto"]             | modeSaisonAuto;
      ActiveRouteur              = newActiveRouteur;
      ConsigneHiverMax           = doc["ConsigneHiverMax"]           | ConsigneHiverMax;
      ConsigneHiverP4            = doc["ConsigneHiverP4"]            | ConsigneHiverP4;
      ConsigneHiverP3            = doc["ConsigneHiverP3"]            | ConsigneHiverP3;
      ConsigneHiverP2            = doc["ConsigneHiverP2"]            | ConsigneHiverP2;
      Activemaintien             = doc["Activemaintien"]             | Activemaintien;
      Tmaintien                  = doc["Tmaintien"]                  | Tmaintien;
      Pmaintien                  = doc["Pmaintien"]                  | Pmaintien;

      // Réponse immédiate au client courant
      char reply[JSON_NOTIFY_GESTION_SIZE];
      buildNotifyGestion(reply, sizeof(reply), true);
      client->text(reply);

      return;
    }
  }
}


bool buildNotifyGestion(char* out, size_t outSize, bool force) {
  if (out == nullptr || outSize == 0) return false;

  StaticJsonDocument<JSON_NOTIFY_GESTION_SIZE> doc;

  doc["modeSaison"]               = Modesaison;
  doc["modeSaisonAuto"]           = modeSaisonAuto;
  doc["ActiveRouteur"]            = ActiveRouteur;
  doc["ConsigneHiverMax"]         = ConsigneHiverMax;
  doc["ConsigneHiverP4"]          = ConsigneHiverP4;
  doc["ConsigneHiverP3"]          = ConsigneHiverP3;
  doc["ConsigneHiverP2"]          = ConsigneHiverP2;
  doc["Activemaintien"]           = Activemaintien;
  doc["Tmaintien"]                = Tmaintien;
  doc["Pmaintien"]                = Pmaintien;
  doc["SoftVersion"]              = SoftVersion;

  const size_t written = serializeJson(doc, out, outSize);
  if (written == 0) {
    out[0] = '\0';
    return false;
  }

  if (force || jsonChanged(out, g_notifyGestionLast)) {
    strncpy(g_notifyGestionLast, out, sizeof(g_notifyGestionLast) - 1);
    g_notifyGestionLast[sizeof(g_notifyGestionLast) - 1] = '\0';
    return true;
  }

  return false;
}


bool buildNotifyHome(char* out, size_t outSize, bool force) {
  if (out == nullptr || outSize == 0) return false;

  char datetmp[40];
  char heuremin[8];
  char dateheure[72];

  snprintf(datetmp, sizeof(datetmp), "%02d %s %d", day, MoislettreWeb(month), year);
  snprintf(heuremin, sizeof(heuremin), "%02d:%02d", hour, minute);
  snprintf(dateheure, sizeof(dateheure), "%s %s - %s", JourSemaine(JourS), datetmp, heuremin);

  StaticJsonDocument<JSON_NOTIFY_HOME_SIZE> doc;

  doc["enChauffe"]         = enChauffe;
  doc["dateheure"]         = dateheure;
  doc["powerLevel"]        = (float)getAppliedPowerPermille() / 10.0f;
  if (lastTempValid)   doc["lastTempC"] = lastTempC;
  else doc["lastTempC"] = -100;
  doc["tempAbiant"]        = tempAbiant;
  doc["routeurIsConnected"]= routeurIsConnected;

  const size_t written = serializeJson(doc, out, outSize);
  if (written == 0) {
    out[0] = '\0';
    return false;
  }

  if (force || jsonChanged(out, g_notifyHomeLast)) {
    strncpy(g_notifyHomeLast, out, sizeof(g_notifyHomeLast) - 1);
    g_notifyHomeLast[sizeof(g_notifyHomeLast) - 1] = '\0';
    return true;
  }

  return false;
}


void Notify() {
  lastNotify = millis();

  char homeJson[JSON_NOTIFY_HOME_SIZE];
  char gestionJson[JSON_NOTIFY_GESTION_SIZE];

  const bool homeChanged    = buildNotifyHome(homeJson, sizeof(homeJson), false);
  const bool gestionChanged = buildNotifyGestion(gestionJson, sizeof(gestionJson), false);

  for (const auto& clientPage : clientPages) {
    const uint32_t clientId = clientPage.first;
    const PageWeb page = clientPage.second;

    AsyncWebSocketClient* client = ws.client(clientId);
    if (client == nullptr) continue;
    if (!client->canSend()) continue;

    if (page == PAGE_HOME && homeChanged) {
      client->text(homeJson);
    } else if (page == PAGE_GESTION && gestionChanged) {
      client->text(gestionJson);
    }
  }
}
