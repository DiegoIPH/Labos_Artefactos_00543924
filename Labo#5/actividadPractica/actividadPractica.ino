#include <WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

// ---------------------- CONFIGURACIÓN WI-FI ----------------------
#define WLAN_SSID   "ARTEFACTOS"
#define WLAN_PASS   "87654321"

// ---------------------- CONFIGURACIÓN ADAFRUIT IO ----------------------
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883
#define AIO_USERNAME    "DiegoIPH"   
#define AIO_KEY         ""

// ---------------------- PINES ----------------------
#define TRIG_PIN  18
#define ECHO_PIN  19
#define PIN_R     4
#define PIN_G     0
#define PIN_B     2

// ---------------------- AJUSTES ----------------------
#define RGB_ANODO_COMUN  false      
#define PWM_FREQ         5000
#define PWM_RES          8         
#define INTERVALO_PUBLICAR_MS 5000
#define DIST_CERCA_CM    10     
#define DIST_MEDIA_CM    20    

// ---------------------- CLIENTE MQTT Y FEEDS ----------------------
WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

Adafruit_MQTT_Publish feedDistancia = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/distancia");

Adafruit_MQTT_Subscribe feedLedToggle = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/apagar");

// ---------------------- ESTADO ----------------------
float ultimaDistancia = -1;             
unsigned long ultimoPublicar = 0;
unsigned long ultimoPing = 0;
bool ledEncendido = true; // Controla si el LED está activo (Toggle ON) o apagado (Toggle OFF)

// ---------------------- PROTOTIPOS ----------------------
void conectarWiFi();
void conectarMQTT();
float leerDistanciaCm();
float distanciaPromedio(int muestras);
void escribirRGB(uint8_t r, uint8_t g, uint8_t b);
void actualizarLED();

// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(10);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  ledcAttach(PIN_R, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_G, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_B, PWM_FREQ, PWM_RES);

  escribirRGB(255, 255, 255);   

  conectarWiFi();

  // PASO 2: Registrar la suscripción al feed en setup()
  mqtt.subscribe(&feedLedToggle);
}

// =====================================================================
void loop() {
  conectarMQTT(); // Mantiene la conexión con Adafruit IO

  // PASO 3: Revisar si llegaron mensajes del botón Toggle desde el Dashboard
  Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt.readSubscription(20))) { // Tiempo de espera breve de 20ms
    if (subscription == &feedLedToggle) {
      char *mensaje = (char *)feedLedToggle.lastread;
      Serial.print("Comando de LED recibido desde Adafruit: ");
      Serial.println(mensaje);

      // Comprobar la orden enviada por el Toggle (ON/OFF o 1/0)
      if (strcmp(mensaje, "ON") == 0 || strcmp(mensaje, "1") == 0) {
        ledEncendido = true;
      } else if (strcmp(mensaje, "OFF") == 0 || strcmp(mensaje, "0") == 0) {
        ledEncendido = false;
      }

      actualizarLED(); // Aplicar el cambio de estado de forma inmediata
    }
  }

  // Leer el ultrasónico y publicar la distancia periódicamente
  if (millis() - ultimoPublicar >= INTERVALO_PUBLICAR_MS) {
    ultimoPublicar = millis();

    float d = distanciaPromedio(5);
    if (d > 0) {
      ultimaDistancia = d;
      Serial.print("Distancia: "); Serial.print(d, 1); Serial.println(" cm");

      if (!feedDistancia.publish(d)) {
        Serial.println("Error al publicar la distancia");
      }
    } else {
      Serial.println("Lectura fuera de rango o sin eco");
    }

    actualizarLED(); // Refrescar el color según la distancia
  }

  // Mantener viva la conexión MQTT
  if (millis() - ultimoPing >= 30000) {
    ultimoPing = millis();
    mqtt.ping();
  }
}

// =====================================================================
//  ULTRASÓNICO HC-SR04
// =====================================================================
float leerDistanciaCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duracion = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duracion == 0) return -1;

  float distancia = (duracion * 0.0343) / 2;

  if (distancia < 2 || distancia > 100) return -1;
  return distancia;
}

float distanciaPromedio(int muestras) {
  float suma = 0;
  int validas = 0;
  for (int i = 0; i < muestras; i++) {
    float d = leerDistanciaCm();
    if (d > 0) { suma += d; validas++; }
    delay(40);
  }
  return (validas > 0) ? suma / validas : -1;
}

// =====================================================================
//  LED RGB
// =====================================================================
void escribirRGB(uint8_t r, uint8_t g, uint8_t b) {
  if (RGB_ANODO_COMUN) {          
    r = 255 - r;  g = 255 - g;  b = 255 - b;
  }
  ledcWrite(PIN_R, r);
  ledcWrite(PIN_G, g);
  ledcWrite(PIN_B, b);
}

void actualizarLED() {
  // Si el Toggle en Adafruit se desactivó, apagar completamente el LED
  if (!ledEncendido) {
    escribirRGB(0, 0, 0); 
    return;
  }

  // Si no hay lectura válida de distancia
  if (ultimaDistancia <= 0) {
    escribirRGB(255, 255, 255); 
    return;
  }

  // Control según rango de distancia
  if (ultimaDistancia < DIST_CERCA_CM) {
    escribirRGB(255, 0, 0);       // Rojo: Cerca
  } else if (ultimaDistancia < DIST_MEDIA_CM) {
    escribirRGB(255, 255, 0);     // Amarillo: Distancia media
  } else {
    escribirRGB(0, 255, 0);       // Verde: Lejos
  }
}

// =====================================================================
//  CONEXIONES
// =====================================================================
void conectarWiFi() {
  Serial.print("Conectando a "); Serial.println(WLAN_SSID);
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi conectado. IP: "); Serial.println(WiFi.localIP());
}

void conectarMQTT() {
  if (mqtt.connected()) return;

  Serial.print("Conectando a Adafruit IO... ");
  int8_t ret;
  uint8_t intentos = 3;
  while ((ret = mqtt.connect()) != 0) {         
    Serial.println(mqtt.connectErrorString(ret));
    Serial.println("Reintentando en 5 segundos...");
    mqtt.disconnect();
    delay(5000);
    if (--intentos == 0) {
      Serial.println("No se pudo conectar. Reiniciando la ESP32...");
      ESP.restart();
    }
  }
  Serial.println("¡Conectado a Adafruit IO!");
}