#define BLYNK_TEMPLATE_ID "TMPL3H6aR4481"
#define BLYNK_TEMPLATE_NAME "HYDROGUARD"

#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <BlynkSimpleEsp32.h>

// WiFi Credentials
const char* ssid = "AYANOKOJI";
const char* password = "KARUIZAWA";

// HiveMQ MQTT Broker
const char* mqtt_server = "797b39718192449bbfe32bbe03652c57.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "SIDDMQTT";
const char* mqtt_pass = "SIDDmqtt@123";

WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

// Blynk Auth Token
char auth[] = "TBq35TGzti1SuxhqWHZ3HbG2XeNKlSAv";

// Node ID
const char* nodeID = "node-2";

// MQTT Topics
String mqtt_topic_pub = String("esp32/") + nodeID + "/sensor";
String mqtt_topic_sub = String("esp32/") + nodeID + "/command";

// Sensor Pins
#define FLOW_SENSOR 32
#define PRESSURE_SENSOR 35
#define TAMPER_SENSOR 25  // Tamper Detection GPIO Pin

// LED Pins
#define POWER_LED 2
#define WIFI_LED 4
#define LEAK_LED 5

// Flow Sensor Variables
volatile int flow_pulse_count = 0;

// Alarm Logic Variables
bool alarmActive = false;
unsigned long alarmStartTime = 0;
unsigned long leakStartTime = 0;  // Timer for 10s delay
const unsigned long ALARM_DURATION = 3600000;  // 1 hour
const unsigned long LEAK_DETECTION_TIME = 10000;  // 10 seconds

// Interrupt Service Routine (ISR)
void IRAM_ATTR pulseCounter() { flow_pulse_count++; }

void setup() {
    Serial.begin(115200);
    pinMode(POWER_LED, OUTPUT);
    pinMode(WIFI_LED, OUTPUT);
    pinMode(LEAK_LED, OUTPUT);
    digitalWrite(POWER_LED, HIGH);  // Keep power LED always ON
    digitalWrite(WIFI_LED, LOW);
    digitalWrite(LEAK_LED, LOW);

    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n✅ WiFi Connected!");

    // MQTT Setup
    espClient.setInsecure();
    mqttClient.setServer(mqtt_server, mqtt_port);
    reconnectMQTT();

    // Start Blynk
    Blynk.begin(auth, ssid, password);

    // Setup Sensors
    pinMode(FLOW_SENSOR, INPUT);
    pinMode(PRESSURE_SENSOR, INPUT);
    pinMode(TAMPER_SENSOR, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR), pulseCounter, RISING);
}

void reconnectMQTT() {
    while (!mqttClient.connected()) {
        Serial.print("🔄 Connecting to HiveMQ...");
        if (mqttClient.connect(nodeID, mqtt_user, mqtt_pass)) {
            Serial.println("✅ MQTT Connected!");
            digitalWrite(WIFI_LED, HIGH);
            mqttClient.subscribe(mqtt_topic_sub.c_str());
        } else {
            Serial.println("❌ Failed, retrying in 5s...");
            digitalWrite(WIFI_LED, LOW);
            delay(5000);
        }
    }
}

void checkWiFiStatus() {
    if (WiFi.status() == WL_CONNECTED) {
        digitalWrite(WIFI_LED, HIGH);
    } else {
        digitalWrite(WIFI_LED, LOW);
        digitalWrite(LEAK_LED, LOW);
        Serial.println("❌ WiFi Disconnected!");
        WiFi.reconnect();
    }
}

void loop() {
    Blynk.run();
    if (!mqttClient.connected()) reconnectMQTT();
    mqttClient.loop();
    checkWiFiStatus();

    static unsigned long lastPublish = 0;
    unsigned long now = millis();

    if (now - lastPublish >= 1000) {
        float flowRate = (flow_pulse_count / 7.5);
        flow_pulse_count = 0;

        int rawPressure = analogRead(PRESSURE_SENSOR);
        float voltage = rawPressure * (3.3 / 4095.0);
        float pressureMPa = ((voltage - 0.5) / (4.0)) * 1.2;
        float pressureBar = pressureMPa * 10;

        // Leak detection logic with 10s delay
        if (flowRate > 3) {
            if (leakStartTime == 0) {
                leakStartTime = now;  // Start timer if flow rate > 3
            } else if (now - leakStartTime >= LEAK_DETECTION_TIME) {
                if (!alarmActive) {
                    alarmActive = true;
                    alarmStartTime = now;
                    digitalWrite(LEAK_LED, HIGH);
                    mqttClient.publish("esp32/alarm", "🚨 Leak Detected!");
                    Serial.println("🚨 Leak Detected!");
                }
            }
        } else {
            leakStartTime = 0;  // Reset timer if flow rate drops below 3
        }

        // Reset alarm after 1 hour
        if (alarmActive && (now - alarmStartTime >= ALARM_DURATION)) {
            alarmActive = false;
            digitalWrite(LEAK_LED, LOW);
            Serial.println("✅ Leak Alarm Reset");
        }

        char payload[100];
        sprintf(payload, "Flow: %.2f L/min, Pressure: %.2f bar", flowRate, pressureBar);
        mqttClient.publish(mqtt_topic_pub.c_str(), payload);
        Serial.println("📤 Published: " + String(payload));

        lastPublish = now;
    }
}