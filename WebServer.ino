#include "types.h"

void initWebServeur() {

// Page

// Page de gestion
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    File file = LittleFS.open("/home.html", "r");

    request->sendChunked("text/html", [file](uint8_t* buffer, size_t maxLen, size_t index) mutable -> size_t {
      if (file.available()) {
        size_t len = file.readBytes((char*)buffer, maxLen);  // Lit un bloc de données jusqu'à maxLen octets
        return len;
      } else {
        file.close();  // Ferme le fichier lorsque tout est envoyé
        return 0;      // Indique la fin de la réponse
      }
    });
  });

// Page de gestion
  server.on("/gestion", HTTP_GET, [](AsyncWebServerRequest* request) {
    File file = LittleFS.open("/gestion.html", "r");

    request->sendChunked("text/html", [file](uint8_t* buffer, size_t maxLen, size_t index) mutable -> size_t {
      if (file.available()) {
        size_t len = file.readBytes((char*)buffer, maxLen);  // Lit un bloc de données jusqu'à maxLen octets
        return len;
      } else {
        file.close();  // Ferme le fichier lorsque tout est envoyé
        return 0;      // Indique la fin de la réponse
      }
    });
  });

// Page de redémarrage du controleur
  server.on("/redemar", HTTP_GET, [](AsyncWebServerRequest* request) {
    String html = "<!DOCTYPE html> <html lang='fr'> <head> <meta charset='UTF-8'><center><h1>Redémarrage du controleur ECS en cours</h1><h2>Vous allez être automatiquement redirigé vers la page d'accueil</h2><div id='circle'></div><style>#circle { width: 50px; height: 50px; border: 3px solid blue; border-radius: 50%; border-top-color: transparent; animation: spin 1s linear infinite; display: inline-block; } @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }</style></center><script>setTimeout(function(){ window.location = '/'; }, 10000);</script>";
    request->send(200, "text/html", html);
    delay(1000);
    ESP.restart();
  });


// Page de changement de saison du controleur
  server.on("/saison", HTTP_GET, [](AsyncWebServerRequest* request) {
    Modesaison = !Modesaison;
  });

  
server.on("/stats", HTTP_GET, [](AsyncWebServerRequest *req){
    // Collecte info mémoire
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);

    uint32_t freeHeap = info.total_free_bytes;
    uint32_t largest  = info.largest_free_block;
    float frag = 0.0f;
    if (freeHeap > 0 && largest > 0)
      frag = 100.0f * (1.0f - (float)largest / (float)freeHeap);
    int rssi = WiFi.RSSI(); 

  uint32_t sec = millis() / 1000;
  uint32_t days = sec / 86400; sec %= 86400;
  uint32_t hrs  = sec / 3600;  sec %= 3600;
  uint32_t mins = sec / 60;    sec %= 60;
  char uptime[32];
  snprintf(uptime, sizeof(uptime), "%ud %02u:%02u:%02u", days, hrs, mins, sec);

    // Page HTML toute simple
    char html[1024];
    snprintf(html, sizeof(html),
      "<!DOCTYPE html><html><head><meta charset='utf-8'>"
      "<title>ESP32 Stats</title>"
      "<style>"
      "body{font-family:monospace;background:#0b1020;color:#e8eefc;"
      "line-height:1.6;padding:20px;}"
      "h2{color:#9cf;} th,td{padding:4px 12px;text-align:left;}"
      "table{border-collapse:collapse;margin-top:10px;}"
      "td{border-bottom:1px solid #334;}"
      "</style></head><body>"
      "<h2>ESP32 /stats</h2>"
      "<table>"
      "<tr><th>Uptime</th><td>%s</td></tr>"
      "<tr><th>Free Heap</th><td>%u octets</td></tr>"
      "<tr><th>Largest Block</th><td>%u octets</td></tr>"
      "<tr><th>Fragmentation</th><td>%.2f %%</td></tr>"
      "<tr><th>RSSI Wi-Fi</th><td>%d dBm</td></tr>"
      "</table>"
      "</body></html>",
      uptime, freeHeap, largest, frag, rssi
    );

    req->send(200, "text/html", html);
  });

	server.serveStatic("/", LittleFS, "/");
	// démarrage du serveur web et websocket
	server.addHandler(&ws);
	server.begin();

}
