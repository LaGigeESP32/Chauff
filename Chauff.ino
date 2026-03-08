#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>
#include "LittleFS.h"
#include <FS.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <HTTPClient.h>
#include <WebSocketsClient_Generic.h>
#include <WiFiManagerESP32.h>
#include <Adafruit_NeoPixel.h>
#include <soc/gpio_struct.h>
#include "types.h"

// =======================
// Pins
// =======================
#define PIN_1WIRE     5
#define PIN_MOSFET    4     // P-MOS: LOW=ON, HIGH=OFF
#define LED_PIN       13
#define LED_COUNT     1

#define ZC_PIN        27    // Zero-cross input
#define TRIAC_PIN     26    // Optotriac gate pulse output


// =======================
// Varaible Globales
// =======================
// Version logiciel
const char SoftVersion[] = "0.1";

float tempSonde;  // température de la sonde
float tempAbiant; // température de tempoconnect


// mdns Resolve
struct HostCache {
  const char *mdns;          // ex: "routeur.local"
  IPAddress ip;
  bool ipValid;
  uint32_t nextResolveMs;    // backoff: prochaine tentative de résolution
  uint32_t backoffMs;        // backoff actuel
};

static HostCache gRouteur = { "routeur.local",      IPAddress(), false, 0, 1000 };
static HostCache gTempo   = { "tempoconnect.local", IPAddress(), false, 0, 1000 };

static const uint32_t RESOLVE_BACKOFF_MIN = 1000;    // 1s
static const uint32_t RESOLVE_BACKOFF_MAX = 60000;   // 60s

// Routeur Solaire
bool routeurIsConnected;
bool WSrouteurIsConnected;
unsigned long lastNotifiy;
int ordreRouteur;
unsigned long lastNotify;

// Varible de Gestion
bool Modesaison = 0;
bool modeSaisonAuto = 1;
bool ActiveRouteur = 1;
uint8_t ConsigneHiverMax = 25;
uint8_t ConsigneHiverP4 = 20;
uint8_t ConsigneHiverP3 = 18;
uint8_t ConsigneHiverP2 = 16;
bool Activemaintien = 0;
uint8_t Tmaintien = 18;
uint8_t Pmaintien = 50;

int enChauffe = 0;

char JourJ[12]  = "NON DEFINI";
char JourJ1[12] = "NON DEFINI";

// ================= STRUCTURE PARTAGÉE DES TASK =================
struct SharedData {
  struct {
    float temp;
    uint32_t version;
} Tempo;

};

SharedData shared; // instance globale partagée

struct GestionConfig {
  bool Modesaison;
  bool modeSaisonAuto;
  bool ActiveRouteur;
  uint8_t ConsigneHiverMax;
  uint8_t ConsigneHiverP4;
  uint8_t ConsigneHiverP3;
  uint8_t ConsigneHiverP2;
  bool Activemaintien;
  uint8_t Tmaintien;
  uint8_t Pmaintien;
};

// heure
  int day;
  int month;
  int year;
  int hour;
  int minute;
  int second;
  int JourS;

const char* JourSemaine(int dow);
const char* MoislettreWeb(int mois);

// =======================
// WS2812
// =======================
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// =======================
// DS18B20
// =======================
OneWire oneWire(PIN_1WIRE);
DallasTemperature sensors(&oneWire);

static constexpr uint32_t TEMP_PERIOD_MS             = 2000;
static constexpr uint32_t TEMP_CONV_MS               = 800;  // 12-bit
static constexpr uint32_t SENSOR_POWER_SETTLE_MS     = 30;
static constexpr uint32_t SENSOR_RECOVERY_OFF_MS     = 150;
static constexpr uint32_t SENSOR_RECOVERY_RESTART_MS = 30;

bool sensorEnabled = true;
float lastTempC = DEVICE_DISCONNECTED_C;
bool lastTempValid = false;


// =======================
// Instances Web 
// =======================
WiFiManagerESP32 wifiMgr;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
WebSocketsClient webSocket;


// =======================
// TRIAC / Phase control
// =======================
hw_timer_t *timer = nullptr;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;

static constexpr uint32_t TRIAC_HALF_CYCLE_US       = 10000; // 50Hz => 10ms
static constexpr uint32_t TRIAC_DELAY_MIN_US         = 200;
static constexpr uint32_t TRIAC_PULSE_US             = 50;
static constexpr uint32_t TRIAC_ZC_DEBOUNCE_US       = 2000;
static constexpr uint32_t TRIAC_DELAY_OFF_US         = TRIAC_HALF_CYCLE_US + 1U;
static constexpr uint32_t TRIAC_DELAY_MAX_FIRE_US    = TRIAC_HALF_CYCLE_US - TRIAC_PULSE_US;

volatile uint32_t triggerDelayUs = TRIAC_DELAY_OFF_US; // >= HALF_CYCLE_US => OFF
volatile bool triacEnabled = false;
volatile uint16_t requestedPowerPermille = 0;
volatile uint16_t appliedPowerPermille   = 0;

enum PhaseState : uint8_t {
  IDLE = 0,
  WAIT_FIRE = 1,
  WAIT_OFF = 2
};

volatile PhaseState phaseState = IDLE;
volatile uint32_t lastZcMicros = 0;

// =======================
// Sensor state machine
// =======================
enum SensorState : uint8_t {
  SENSOR_DISABLED = 0,
  SENSOR_POWER_ON_WAIT,
  SENSOR_READY,
  SENSOR_CONVERTING,
  SENSOR_RECOVERY_POWER_OFF_WAIT,
  SENSOR_RECOVERY_POWER_ON_WAIT
};

SensorState sensorState = SENSOR_DISABLED;
uint32_t sensorStateDeadlineMs = 0;
uint32_t tempKickMs = 0;
uint32_t tempStartMs = 0;

// =======================
// Time helper
// =======================
static inline bool timeReached(uint32_t now, uint32_t deadline) {
  return (int32_t)(now - deadline) >= 0;
}


// LED
static LedColor g_lastLedColor = LED_OFF;
static uint8_t g_lastLedPower = 255;   // valeur impossible au démarrage

// =======================
// Page client
// =======================
struct ClientPageEntry {
  uint32_t id;
  PageWeb page;
  bool used;
};

static constexpr size_t MAX_WS_CLIENTS = 8;
ClientPageEntry clientPages[MAX_WS_CLIENTS] = {};

// ---------- Buffers cache notify ----------
// ---------- Configuration ----------
static constexpr size_t WS_RX_BUFFER_SIZE         = 512;
static constexpr size_t JSON_DOC_RX_SIZE          = 512;
static constexpr size_t JSON_NOTIFY_GESTION_SIZE  = 512;
static constexpr size_t JSON_NOTIFY_HOME_SIZE     = 512;
static constexpr size_t ROUTEUR_RX_BUFFER_SIZE    = 256;

static char g_notifyGestionLast[JSON_NOTIFY_GESTION_SIZE] = {0};
static char g_notifyHomeLast[JSON_NOTIFY_HOME_SIZE] = {0};


static inline void IRAM_ATTR triacGateOn() {
  GPIO.out_w1ts = (1UL << TRIAC_PIN);
}

static inline void IRAM_ATTR triacGateOff() {
  GPIO.out_w1tc = (1UL << TRIAC_PIN);
}

// =======================
// TRIAC timer ISR
// =======================
void IRAM_ATTR timerISR() {
  portENTER_CRITICAL_ISR(&timerMux);

  if (!triacEnabled) {
    triacGateOff();
    phaseState = IDLE;
    timerAlarmDisable(timer);
    portEXIT_CRITICAL_ISR(&timerMux);
    return;
  }

  if (phaseState == WAIT_FIRE) {
    triacGateOn();
    phaseState = WAIT_OFF;

    timerWrite(timer, 0);
    timerAlarmWrite(timer, TRIAC_PULSE_US, false);
    timerAlarmEnable(timer);
  }
  else if (phaseState == WAIT_OFF) {
    triacGateOff();
    phaseState = IDLE;
    timerAlarmDisable(timer);
  }

  portEXIT_CRITICAL_ISR(&timerMux);
}

// =======================
// Zero-cross ISR
// =======================
void IRAM_ATTR zeroCrossISR() {
  uint32_t now = micros();
  uint32_t prevZc = lastZcMicros;

  if (prevZc != 0 && (now - prevZc) < TRIAC_ZC_DEBOUNCE_US) {
    return;
  }

  lastZcMicros = now;

  if (!triacEnabled) return;
  if (triggerDelayUs >= TRIAC_HALF_CYCLE_US) return;

  portENTER_CRITICAL_ISR(&timerMux);
  phaseState = WAIT_FIRE;
  timerWrite(timer, 0);
  timerAlarmWrite(timer, triggerDelayUs, false);
  timerAlarmEnable(timer);
  portEXIT_CRITICAL_ISR(&timerMux);
}

// =======================
// Power mapping
// =======================
// =======================
// TRIAC power control
// Charge résistive -> mapping quasi linéaire en puissance réelle
// API en permille : 0..1000
// =======================

static inline float powerRatioFromAlpha(float alphaRad) {
  // Charge résistive pure :
  // P/Pmax = 1 - alpha/pi + sin(2*alpha)/(2*pi)
  return 1.0f - (alphaRad / PI) + (sinf(2.0f * alphaRad) / (2.0f * PI));
}

static uint16_t powerPermilleFromDelayUs(uint32_t delayUs) {
  if (delayUs >= TRIAC_HALF_CYCLE_US) {
    return 0;
  }

  float alpha = ((float)delayUs * PI) / (float)TRIAC_HALF_CYCLE_US;
  float ratio = powerRatioFromAlpha(alpha);

  if (ratio <= 0.0f) {
    return 0;
  }

  if (ratio >= 1.0f) {
    return 1000;
  }

  return (uint16_t)(ratio * 1000.0f + 0.5f);
}

static uint32_t delayUsFromPowerPermille(uint16_t permille) {
  if (permille == 0) {
    return TRIAC_DELAY_OFF_US; // OFF logique
  }

  if (permille >= 1000) {
    return TRIAC_DELAY_MIN_US;
  }

  const float target = (float)permille / 1000.0f;

  // Bornes d’angle correspondant aux limites matérielles
  float alphaLo = ((float)TRIAC_DELAY_MIN_US * PI) / (float)TRIAC_HALF_CYCLE_US;
  float alphaHi = ((float)TRIAC_DELAY_MAX_FIRE_US * PI) / (float)TRIAC_HALF_CYCLE_US;

  // Clamp si la demande dépasse ce que les bornes physiques permettent
  float pMax = powerRatioFromAlpha(alphaLo);
  float pMin = powerRatioFromAlpha(alphaHi);

  if (target >= pMax) {
    return TRIAC_DELAY_MIN_US;
  }

  if (target <= pMin) {
    return TRIAC_DELAY_MAX_FIRE_US;
  }

  // Recherche dichotomique de l’angle donnant la bonne puissance
  for (uint8_t i = 0; i < 24; i++) {
    float alphaMid = 0.5f * (alphaLo + alphaHi);
    float pMid = powerRatioFromAlpha(alphaMid);

    if (pMid > target) {
      // Trop de puissance -> il faut tirer plus tard
      alphaLo = alphaMid;
    } else {
      // Pas assez de puissance -> il faut tirer plus tôt
      alphaHi = alphaMid;
    }
  }

  float alpha = 0.5f * (alphaLo + alphaHi);
  uint32_t delayUs = (uint32_t)((alpha * (float)TRIAC_HALF_CYCLE_US) / PI + 0.5f);

  if (delayUs < TRIAC_DELAY_MIN_US) {
    delayUs = TRIAC_DELAY_MIN_US;
  }

  if (delayUs > TRIAC_DELAY_MAX_FIRE_US) {
    delayUs = TRIAC_DELAY_MAX_FIRE_US;
  }

  return delayUs;
}

void setPowerPermille(uint16_t p) {
  bool enable;
  uint32_t delayUs;
  uint16_t applied;

  if (p == 0U) {
    enable = false;
    delayUs = TRIAC_DELAY_OFF_US;
    applied = 0U;
  } else {
    if (p > 1000U) {
      p = 1000U;
    }

    enable = true;
    delayUs = delayUsFromPowerPermille(p);
    applied = powerPermilleFromDelayUs(delayUs);
  }

  portENTER_CRITICAL(&timerMux);
  requestedPowerPermille = p;
  appliedPowerPermille = applied;
  triacEnabled = enable;
  triggerDelayUs = delayUs;
  portEXIT_CRITICAL(&timerMux);

  if (!enable) {
    triacGateOff();
  }
}

static inline uint8_t clampU8(uint8_t value, uint8_t minV, uint8_t maxV) {
  if (value < minV) return minV;
  if (value > maxV) return maxV;
  return value;
}

void getGestionConfigSnapshot(GestionConfig& cfg) {
  portENTER_CRITICAL(&stateMux);
  cfg.Modesaison = Modesaison;
  cfg.modeSaisonAuto = modeSaisonAuto;
  cfg.ActiveRouteur = ActiveRouteur;
  cfg.ConsigneHiverMax = ConsigneHiverMax;
  cfg.ConsigneHiverP4 = ConsigneHiverP4;
  cfg.ConsigneHiverP3 = ConsigneHiverP3;
  cfg.ConsigneHiverP2 = ConsigneHiverP2;
  cfg.Activemaintien = Activemaintien;
  cfg.Tmaintien = Tmaintien;
  cfg.Pmaintien = Pmaintien;
  portEXIT_CRITICAL(&stateMux);
}

bool applyGestionConfigFromJson(JsonVariantConst doc) {
  GestionConfig next;
  getGestionConfigSnapshot(next);

  next.Modesaison      = doc["modeSaison"]      | next.Modesaison;
  next.modeSaisonAuto  = doc["modeSaisonAuto"]  | next.modeSaisonAuto;
  next.ActiveRouteur   = doc["ActiveRouteur"]   | next.ActiveRouteur;
  next.Activemaintien  = doc["Activemaintien"]  | next.Activemaintien;

  next.ConsigneHiverMax = clampU8((uint8_t)(doc["ConsigneHiverMax"] | next.ConsigneHiverMax), 5U, 75U);
  next.ConsigneHiverP4  = clampU8((uint8_t)(doc["ConsigneHiverP4"]  | next.ConsigneHiverP4), 5U, 75U);
  next.ConsigneHiverP3  = clampU8((uint8_t)(doc["ConsigneHiverP3"]  | next.ConsigneHiverP3), 5U, 75U);
  next.ConsigneHiverP2  = clampU8((uint8_t)(doc["ConsigneHiverP2"]  | next.ConsigneHiverP2), 5U, 75U);

  if (next.ConsigneHiverP2 > next.ConsigneHiverP3) {
    next.ConsigneHiverP3 = next.ConsigneHiverP2;
  }
  if (next.ConsigneHiverP3 > next.ConsigneHiverP4) {
    next.ConsigneHiverP4 = next.ConsigneHiverP3;
  }
  if (next.ConsigneHiverP4 > next.ConsigneHiverMax) {
    next.ConsigneHiverMax = next.ConsigneHiverP4;
  }

  next.Tmaintien = clampU8((uint8_t)(doc["Tmaintien"] | next.Tmaintien), 5U, 75U);
  next.Pmaintien = clampU8((uint8_t)(doc["Pmaintien"] | next.Pmaintien), 0U, 100U);

  bool shouldReconnect = false;
  portENTER_CRITICAL(&stateMux);
  shouldReconnect = (!ActiveRouteur && next.ActiveRouteur);
  Modesaison = next.Modesaison;
  modeSaisonAuto = next.modeSaisonAuto;
  ActiveRouteur = next.ActiveRouteur;
  ConsigneHiverMax = next.ConsigneHiverMax;
  ConsigneHiverP4 = next.ConsigneHiverP4;
  ConsigneHiverP3 = next.ConsigneHiverP3;
  ConsigneHiverP2 = next.ConsigneHiverP2;
  Activemaintien = next.Activemaintien;
  Tmaintien = next.Tmaintien;
  Pmaintien = next.Pmaintien;
  portEXIT_CRITICAL(&stateMux);

  return shouldReconnect;
}

uint16_t getRequestedPowerPermille() {
  uint16_t p;

  portENTER_CRITICAL(&timerMux);
  p = requestedPowerPermille;
  portEXIT_CRITICAL(&timerMux);

  return p;
}

uint16_t getAppliedPowerPermille() {
  uint16_t p;

  portENTER_CRITICAL(&timerMux);
  p = appliedPowerPermille;
  portEXIT_CRITICAL(&timerMux);

  return p;
}

uint32_t getTriggerDelayUs() {
  uint32_t d;

  portENTER_CRITICAL(&timerMux);
  d = triggerDelayUs;
  portEXIT_CRITICAL(&timerMux);

  return d;
}

 void tempoConnectTemp();
 void notifyRouteur(int type);
 void LectureTime();
 bool applyGestionConfigFromJson(JsonVariantConst doc);
 void getGestionConfigSnapshot(GestionConfig& cfg);

void taskTempo(void *pv) {
bool doTempo = true;  // true = tempo, false = connectRouteur
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(30000);
  for (;;) {
	  
    if (doTempo) {
      // Phase 1 : tempo
      tempoConnectTemp();
    } 
    else if (ActiveRouteur) {
      // Phase 2 : routeur (seulement si nécessaire)
      if (!routeurIsConnected ) connectRouteurHard();
      else notifyRouteur(2);
    }

    // alterne pour la prochaine fois
    doTempo = !doTempo;
	
    vTaskDelayUntil(&lastWake, period);
    }
  }


void taskNRJ(void *pv) {
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(2000);
  for (;;) {
   if (WSrouteurIsConnected && ActiveRouteur) {
      notifyRouteur(0);
    }
    Notify();
    vTaskDelayUntil(&lastWake, period);
  }
}

TaskHandle_t taskTempoHandle;
TaskHandle_t taskNRJHandle;

// =======================
// Setup / Loop
// =======================
void setup() {

  Serial.begin(115200);

  initLittleFS();

  strip.begin();
  strip.show();

  pinMode(PIN_MOSFET, OUTPUT);
  pinMode(TRIAC_PIN, OUTPUT);
  pinMode(ZC_PIN, INPUT);

  digitalWrite(TRIAC_PIN, LOW);

  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &timerISR, true);
  timerAlarmDisable(timer);

  attachInterrupt(digitalPinToInterrupt(ZC_PIN), zeroCrossISR, RISING);

  //Wifi
  wifiMgr.Debug(true);              // Mode verbose
  wifiMgr.Name("chauffage");   // Nom du projet (pour AP + page web + mDNS)
  wifiMgr.begin(true, true, 3, 10);
  wifiMgr.ActiveMDNS();
  wifiMgr.WiFiWebConfig(server);
  wifiMgr.localUpdate(server,"Admin", "Coucou12");

  // webServer
  initWebServeur(),
  initWsServeur();

  // Task
  xTaskCreatePinnedToCore(taskTempo, "taskTempo", 4096, NULL, 1, &taskTempoHandle, 1);
  xTaskCreatePinnedToCore(taskNRJ, "taskNRJ", 4096, NULL, 1, &taskNRJHandle, 1);

  setPowerPermille(0);
  sensorRequestOn();
}

void loop() {

  sensorService();

// Récupération et transformation des variable en global des task
  static uint32_t lastTempover = 0;

  uint32_t tempoVersion;
  float tempoTemp;
  portENTER_CRITICAL(&stateMux);
  tempoVersion = shared.Tempo.version;
  tempoTemp = shared.Tempo.temp;
  portEXIT_CRITICAL(&stateMux);

  if (tempoVersion != lastTempover) {
    tempAbiant = tempoTemp;
    lastTempover = tempoVersion;
  }

  if (lastTempValid) tempSonde = lastTempC;
  else if (tempAbiant > 0) tempSonde = tempAbiant;
  else tempSonde = -100;

  // polling websocket
  webSocket.loop();

  LectureTime();
  maintientTemp();
  EtatLed();

}
