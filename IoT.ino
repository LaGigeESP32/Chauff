
// Initialize LittleFS
void initLittleFS() {
  if (!LittleFS.begin(true)) { }
}

// Write file to LittleFS
void writeFilelfs(fs::FS &fs, const char * path, const char * message){
  File file = fs.open(path, FILE_WRITE);
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.println(message)){ 
    Serial.println("fichier log crée"); 
    }
  file.close();
  }

   // append file to LittleFS
void appendFilelfs(fs::FS &fs, const char * path, const char * message){
  File file = fs.open(path, FILE_APPEND);
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.println(message)){ }
  file.close();
  }
  
void createDir(fs::FS &fs, const char * path){
	fs.mkdir(path);
}



//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// Résolution mDNS
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

static void tryResolveLazy(HostCache &h) {
  if (h.ipValid) return;
  if (WiFi.status() != WL_CONNECTED) return;

  uint32_t now = millis();
  if (now < h.nextResolveMs) return;

  IPAddress ip;
  if (WiFi.hostByName(h.mdns, ip)) {
    h.ip = ip;
    h.ipValid = true;
    h.backoffMs = RESOLVE_BACKOFF_MIN;
    h.nextResolveMs = 0;
  } else {
    // échec -> backoff
    h.nextResolveMs = now + h.backoffMs;
    uint32_t nb = h.backoffMs * 2;
    h.backoffMs = (nb <= RESOLVE_BACKOFF_MAX) ? nb : RESOLVE_BACKOFF_MAX;
  }
}


static void buildUrl(char *out, size_t outSz, const HostCache &h, const char *path) {
  if (h.ipValid) {
    snprintf(out, outSz, "http://%u.%u.%u.%u%s", h.ip[0], h.ip[1], h.ip[2], h.ip[3], path);
  } else {
    snprintf(out, outSz, "http://%s%s", h.mdns, path);
  }
}


static void buildWsHost(char *out, size_t outSz, const HostCache &h) {
  if (h.ipValid) {
    snprintf(out, outSz, "%u.%u.%u.%u", h.ip[0], h.ip[1], h.ip[2], h.ip[3]);
  } else {
    snprintf(out, outSz, "%s", h.mdns);
  }
}


static void invalidateHostIp(HostCache &h) {
  h.ipValid = false;
  h.nextResolveMs = millis() + RESOLVE_BACKOFF_MIN;
  h.backoffMs = RESOLVE_BACKOFF_MIN;
}



//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// Tempo connect (askTemp)
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
void tempoConnectTemp() { // connexion au boîtier tempo pour récupérer la température seule

  // 1) Résolution lazy mDNS -> IP (si possible)
  tryResolveLazy(gTempo);

  HTTPClient http;
  http.setTimeout(1500);   // 1.5s max (local LAN, plus robuste que 5s)

  char url[96];
  buildUrl(url, sizeof(url), gTempo, "/askTemp");

  if (!http.begin(url)) {
    // begin a échoué (rare mais possible)
    invalidateHostIp(gTempo);
    return;
  }

  int httpCode = http.GET();

  if (httpCode != 200) {
    // erreur réseau ou HTTP -> invalide l'IP pour retenter plus tard
    invalidateHostIp(gTempo);
    http.end();
    return;
  }

  // 2) Lecture courte depuis le stream (sans String)
  WiFiClient& s = http.getStream();

  char buf[24]; // suffisant pour "-123.4567" + \0
  size_t i = 0;

  // petite fenêtre de lecture (LAN)
  uint32_t start = millis();
  while (s.connected() && (uint32_t)(millis() - start) < 300) {
    while (s.available() && i < sizeof(buf) - 1) {
      char c = (char)s.read();
      if (c == '\n' || c == '\r') { // fin de ligne
        s.stop(); // optionnel: coupe le stream vite
        break;
      }
      buf[i++] = c;
    }
    if (i > 0) break;
    vTaskDelay(pdMS_TO_TICKS(5)); // pas de delay()
  }
  buf[i] = '\0';

  http.end(); // ferme HTTP le plus tôt possible

  if (i == 0) return; // rien reçu

  // 3) Conversion robuste en float
  char* endp = nullptr;
  float t = strtof(buf, &endp);
  if (endp == buf) {
    // pas de conversion possible
    return;
  }

  // 4) Validations (évite l'ancien t > 0 qui casse les négatifs)
  if (!(t > -40.0f && t < 125.0f)) return;

  // 5) Stockage
  portENTER_CRITICAL(&stateMux);
  shared.Tempo.temp = t;
  shared.Tempo.version++;
  portEXIT_CRITICAL(&stateMux);
}


void LectureTime() {
	const int MAX_SIZE = 80;
	time_t timestamp = time( NULL );
  char buffer[MAX_SIZE];
  struct tm *pTime = localtime(&timestamp );
  strftime(buffer, MAX_SIZE, "%Y/%m/%d %H:%M:%S", pTime);

	hour = (pTime->tm_hour);
	minute = (pTime->tm_min);
	year = (pTime->tm_year+1900);
	month = (pTime->tm_mon +1);
	day = (pTime->tm_mday);
	second = (pTime->tm_sec);
  JourS = weekdaycalc();
}


int weekdaycalc() {

int monthc = month;
int yearc = year;

  if (month < 3) {
    monthc = month + 12;
    yearc = year - 1;
  }
  int K = yearc % 100;
  int J = yearc / 100;
  int f = day + 13 * (monthc + 1) / 5 + K + K / 4 + J / 4 + 5 * J;
  int weekday = f % 7;

  return weekday; // Retourne le jour de la semaine

}
const char* JourSemaine(int dow) {
  switch (dow) {
    case 2: return "Lundi";
    case 3: return "Mardi";
    case 4: return "Mercredi";
    case 5: return "Jeudi";
    case 6: return "Vendredi";
    case 0: return "Samedi";
    case 1: return "Dimanche";
    default: return "Inconnu";
  }
}

const char* MoislettreWeb(int mois) {
  static const char* const moisNoms[] = {
    "Inconnu",
    "Janvier",
    "Février",
    "Mars",
    "Avril",
    "Mai",
    "Juin",
    "Juillet",
    "Août",
    "Septembre",
    "Octobre",
    "Novembre",
    "Décembre"
  };

  if (mois >= 1 && mois <= 12) {
    return moisNoms[mois];
  }
  return moisNoms[0];
}

