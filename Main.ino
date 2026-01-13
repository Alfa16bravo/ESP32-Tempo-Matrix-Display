#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <Wire.h>
#include <SensorQMI8658.hpp>

// --- CONFIGURATION WIFI ---
const char* ssid = "VOTRE_NOM_WIFI";          
const char* password = "VOTRE_MOT_DE_PASSE"; 

// --- CONFIGURATION MATRICE ---
#define LED_PIN     14      
#define NUM_LEDS    64
#define LED_TYPE    WS2812B
#define COLOR_ORDER RGB
#define BRIGHTNESS  1       

CRGB leds[NUM_LEDS];

// --- CONFIGURATION GYRO QMI8658 ---
SensorQMI8658 qmi;
#define I2C_SDA 11
#define I2C_SCL 12

// Seuil de détection (en g)
float SEUIL_SECOUSSE = 1.1; 
unsigned long dernierSecousse = 0;

// --- CONFIGURATION MINUTEUR AUTOMATIQUE ---
unsigned long dernierUpdateAuto = 0;
// 1 heure = 60 min * 60 sec * 1000 ms = 3600000
const unsigned long DELAI_AUTO = 3600000; 

const char* apiUrl = "https://www.api-couleur-tempo.fr/api/jourTempo/today";

void setup() {
  Serial.begin(115200);

  // 1. Init LEDs
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  // 2. Init WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connexion WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK !");

  // 3. Init Capteur QMI8658
  Serial.println("Initialisation QMI8658...");
  if (!qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, I2C_SDA, I2C_SCL)) {
    Serial.println("ERREUR: QMI8658 non trouvé !");
    fill_solid(leds, NUM_LEDS, CRGB::Purple);
    FastLED.show();
  } else {
    Serial.println("QMI8658 Prêt !");
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_125Hz, SensorQMI8658::LPF_MODE_0);
    qmi.enableAccelerometer();
  }

  // Premier appel API au démarrage
  animationChargement();
  verifierCouleurTempo();
  
  // On initialise le chronomètre de l'heure
  dernierUpdateAuto = millis();
}

void loop() {
  unsigned long tempsActuel = millis();

  // --- 1. GESTION DU TIMER AUTOMATIQUE (1H) ---
  if (tempsActuel - dernierUpdateAuto >= DELAI_AUTO) {
    Serial.println("Mise à jour automatique (1h écoulée)...");
    
    animationChargement();
    verifierCouleurTempo();
    
    // On remet le chronomètre à zéro
    dernierUpdateAuto = tempsActuel;
  }

  // --- 2. GESTION DE LA SECOUSSE (QMI8658) ---
  if (qmi.getDataReady()) {
    IMUdata acc;
    qmi.getAccelerometer(acc.x, acc.y, acc.z);
    float force = sqrt(acc.x * acc.x + acc.y * acc.y + acc.z * acc.z);

    if (force > SEUIL_SECOUSSE) {
      // Anti-rebond de 2 secondes
      if (tempsActuel - dernierSecousse > 2000) {
        Serial.print(">>> SECOUSSE VALIDÉE ! Force: ");
        Serial.println(force);
        
        animationChargement();
        verifierCouleurTempo();

        // IMPORTANT : Si on secoue manuellement, on reset aussi le timer auto
        // pour ne pas avoir une mise à jour auto juste après.
        dernierSecousse = tempsActuel;
        dernierUpdateAuto = tempsActuel; 
      }
    }
  }
  
  delay(50); // Petite pause processeur
}

void animationChargement() {
  FastLED.clear();
  CRGB couleurCharge = CRGB::Green;
  int vitesse = 150; 

  // ETAPE 1 : Le Coeur (Carré 2x2)
  int etape1[] = {27, 28, 35, 36}; 
  for(int i=0; i<4; i++) leds[etape1[i]] = couleurCharge;
  FastLED.show();
  delay(vitesse);

  // ETAPE 2 : Premier élargissement (Carré 4x4)
  int etape2[] = {18,19,20,21,  26,29,  34,37,  42,43,44,45};
  for(int i=0; i<12; i++) leds[etape2[i]] = couleurCharge;
  FastLED.show();
  delay(vitesse);

  // ETAPE 3 : Deuxième élargissement (Carré 6x6)
  int etape3[] = {9,10,11,12,13,14, 17,22, 25,30, 33,38, 41,46, 49,50,51,52,53,54};
  for(int i=0; i<20; i++) leds[etape3[i]] = couleurCharge;
  FastLED.show();
  delay(vitesse);

  // ETAPE 4 : Remplissage Total
  fill_solid(leds, NUM_LEDS, couleurCharge);
  FastLED.show();
  delay(vitesse);
}

void verifierCouleurTempo() {
  // On revérifie la connexion WiFi avant l'appel (au cas où elle aurait sauté en 1h)
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi perdu, reconnexion...");
    WiFi.disconnect();
    WiFi.reconnect();
    // On attend un peu que ça revienne (max 5 sec)
    for(int i=0; i<10; i++) {
        if(WiFi.status() == WL_CONNECTED) break;
        delay(500);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); 
    
    if (http.begin(client, apiUrl)) {
      int httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc; 
        deserializeJson(doc, payload);
        
        const char* libCouleur = doc["libCouleur"]; 
        int codeJour = doc["codeJour"];
        Serial.printf("API OK -> Couleur: %s\n", libCouleur);

        updateMatrixColor(libCouleur, codeJour);
      } else {
        Serial.println("Erreur HTTP API");
        fill_solid(leds, NUM_LEDS, CRGB::Orange); // Erreur API
        FastLED.show();
      }
      http.end();
    }
  } else {
    Serial.println("Pas de WiFi");
    fill_solid(leds, NUM_LEDS, CRGB::Red); // Erreur WiFi
    FastLED.show();
  }
}

void updateMatrixColor(String couleurTexte, int codeJour) {
  CRGB couleurFinale = CRGB::Black;
  
  if (couleurTexte == "Bleu" || codeJour == 1) couleurFinale = CRGB::Blue;
  else if (couleurTexte == "Blanc" || codeJour == 2) couleurFinale = CRGB::White; 
  else if (couleurTexte == "Rouge" || codeJour == 3) couleurFinale = CRGB::Red;
  else couleurFinale = CRGB::Purple;

  fill_solid(leds, NUM_LEDS, couleurFinale);
  FastLED.show();
}
