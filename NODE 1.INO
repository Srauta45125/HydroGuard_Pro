#define BLYNK_TEMPLATE_ID "TMPL3H6aR4481"
#define BLYNK_TEMPLATE_NAME "HYDROGUARD"

#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <BlynkSimpleEsp32.h>
#include <ArduinoJson.h>

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

// Node Configuration
const char* nodeID = "node-1";
const char* otherNodeID = "node-2";

// MQTT Topics
String mqtt_topic_pub = "esp32/" + String(nodeID) + "/sensor";
String mqtt_topic_flow_pub = "esp32/" + String(nodeID) + "/flow";
String mqtt_topic_flow_sub = "esp32/" + String(otherNodeID) + "/flow";

// Sensor Pins
#define FLOW_SENSOR 14 
#define PRESSURE_SENSOR 34
#define TAMPER_SENSOR 25

// LED Pins
#define POWER_LED 2
#define WIFI_LED 4
#define LEAK_LED 5

// Flow Sensor Variables
volatile int flow_pulse_count = 0;
float otherNodeFlowRate = 0.0;

// Alarm Logic
bool alarmActive = false;
unsigned long alarmStartTime = 0;
unsigned long leakStartTime = 0;
const unsigned long ALARM_DURATION = 3600000;
const unsigned long LEAK_DETECTION_TIME = 10000;

// ISR
void IRAM_ATTR pulseCounter() { flow_pulse_count++; }

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    payload[length] = '\0';
    String message = String((char*)payload);

    StaticJsonDocument<100> doc;
    deserializeJson(doc, message);

    if (String(topic) == mqtt_topic_flow_sub) {
        otherNodeFlowRate = doc["flow"];
        Serial.print("📩 Received flow from ");
        Serial.print(otherNodeID);
        Serial.print(": ");
        Serial.println(otherNodeFlowRate);
    }

    if (String(topic) == "esp32/alarm/reset") {
        alarmActive = false;
        digitalWrite(LEAK_LED, LOW);
        Serial.println("🟢 Alarm manually reset via MQTT");
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(POWER_LED, OUTPUT);
    pinMode(WIFI_LED, OUTPUT);
    pinMode(LEAK_LED, OUTPUT);
    digitalWrite(POWER_LED, HIGH);
    digitalWrite(WIFI_LED, LOW);
    digitalWrite(LEAK_LED, LOW);

    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n✅ WiFi Connected!");

    espClient.setInsecure();
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCallback(mqttCallback);
    reconnectMQTT();

    Blynk.begin(auth, ssid, password);

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
            mqttClient.subscribe(mqtt_topic_flow_sub.c_str());  // Subscribe to Node 2's flow
            mqttClient.subscribe("esp32/alarm/reset");
        } else {
            Serial.println("❌ Failed, retrying...");
            digitalWrite(WIFI_LED, LOW);
            delay(5000);
        }
    }
}

void checkWiFiStatus() {
    if (WiFi.status() != WL_CONNECTED) {
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
        float pressureMPa = ((voltage - 0.5) / 4.0) * 1.2;
        float pressureBar = pressureMPa * 10;

        // Publish flow to Node 2
        StaticJsonDocument<100> flowDoc;
        flowDoc["flow"] = flowRate;
        char flowBuffer[100];
        serializeJson(flowDoc, flowBuffer);
        mqttClient.publish(mqtt_topic_flow_pub.c_str(), flowBuffer);
        Serial.print("📤 Published flow: ");
        Serial.println(flowBuffer);

        // Leak Detection: Compare both node flows
        if (flowRate > 3 && otherNodeFlowRate > 3) {
            if (leakStartTime == 0) {
                leakStartTime = now;
            } else if (now - leakStartTime >= LEAK_DETECTION_TIME) {
                if (!alarmActive) {
                    alarmActive = true;
                    alarmStartTime = now;
                    digitalWrite(LEAK_LED, HIGH);
                    mqttClient.publish("esp32/alarm", "{\"status\": \"Leak Detected!\"}");
                    Serial.println("🚨 Leak Detected!");
                }
            }
        } else {
            leakStartTime = 0;
        }

        // Auto reset alarm after 1 hour
        if (alarmActive && (now - alarmStartTime >= ALARM_DURATION)) {
            alarmActive = false;
            digitalWrite(LEAK_LED, LOW);
            Serial.println("✅ Leak Alarm Reset");
        }

        // Publish full data
        StaticJsonDocument<200> doc;
        doc["nodeID"] = nodeID;
        doc["flow"] = flowRate;
        doc["pressure"] = pressureBar;
        doc["alarm"] = alarmActive ? "ON" : "OFF";

        char jsonBuffer[256];
        serializeJson(doc, jsonBuffer);
        mqttClient.publish(mqtt_topic_pub.c_str(), jsonBuffer);
        Serial.print("📤 Published sensor data: ");
        Serial.println(jsonBuffer);

        lastPublish = now;
    }
}