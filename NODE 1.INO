#define BLYNK_TEMPLATE_ID "TMPL3w-o5HZik"
#define BLYNK_TEMPLATE_NAME "HYDRO HACKERS"

#include <WiFi.h>
#include <PubSubClient.h>
#include <BlynkSimpleEsp32.h>

// WiFi credentials
const char* ssid = "AYANOKOJI";
const char* password = "KARUIZAWA";

// MQTT broker
const char* mqtt_server = "192.168.1.22";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Shared Blynk auth token for the project
char auth[] = "v-UDDmVfIPMLSaLBs9R3q7SjW9_h7a9X";

// Unique identifier for Node 1
const char* nodeID = "node1";

// MQTT topics: publish sensor data and subscribe to commands
String mqtt_topic_pub = String("esp32/") + nodeID + "/sensor";
String mqtt_topic_sub = String("esp32/") + nodeID + "/command";

// Sensor Pins
#define FLOW_SENSOR_1 14
#define FLOW_SENSOR_2 27
#define PRESSURE_SENSOR 34 // ADC pin for pressure sensor

volatile int flow1_pulse_count = 0;
volatile int flow2_pulse_count = 0;
float calibrationFactor = 4.5; // Adjust based on your sensor's datasheet

// Thresholds
const float FLOW_THRESHOLD = 20.0; // L/min
const float PRESSURE_THRESHOLD = 8.0; // bar

bool alarmActive = false;
bool alarmEnabled = true; // Controlled via Blynk switch

// Interrupt Service Routine (ISR) for flow sensors
void IRAM_ATTR pulseCounter1() {
  flow1_pulse_count++;
}
void IRAM_ATTR pulseCounter2() {
  flow2_pulse_count++;
}

void setup_wifi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi connected");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println("📩 MQTT Received on " + String(topic) + ": " + message);
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("🔄 Attempting MQTT connection...");
    if (mqttClient.connect(nodeID)) {
      Serial.println("✅ Connected!");
      mqttClient.subscribe(mqtt_topic_sub.c_str());
    } else {
      Serial.print("❌ Failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" Retrying in 5 seconds...");
      delay(5000);
    }
  }
}

BLYNK_WRITE(V7) { // Alarm enable/disable switch
  alarmEnabled = param.asInt();
  if (!alarmEnabled) {
    alarmActive = false;
    Blynk.virtualWrite(V8, 0); // Turn off alarm indicator
    Serial.println("🚨 Alarm disabled");
  } else {
    Serial.println("🚨 Alarm enabled");
  }
}

void setup() {
  Serial.begin(115200);
  setup_wifi();

  Blynk.begin(auth, ssid, password);
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  pinMode(FLOW_SENSOR_1, INPUT);
  pinMode(FLOW_SENSOR_2, INPUT);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_1), pulseCounter1, RISING);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_2), pulseCounter2, RISING);
}

void loop() {
  Blynk.run();

  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  // Publish sensor data every 10 seconds
  static unsigned long lastPublish = 0;
  if (millis() - lastPublish > 10000) {
    float flowRate1 = (flow1_pulse_count / 7.5); // Convert to L/min
    float flowRate2 = (flow2_pulse_count / 7.5);
    flow1_pulse_count = 0;
    flow2_pulse_count = 0;

    int rawPressure = analogRead(PRESSURE_SENSOR);
    float voltage = rawPressure * (3.3 / 4095.0); // Convert ADC value to voltage (ESP32 12-bit ADC)
    float pressure = (voltage - 0.5) * (1.2 / 4.0) * 10; // Convert voltage to pressure (MPa to bar)

    char payload[100];
    sprintf(payload, "Flow1: %.2f L/min, Flow2: %.2f L/min, Pressure: %.2f bar", flowRate1, flowRate2, pressure);
    mqttClient.publish(mqtt_topic_pub.c_str(), payload);

    Serial.println("📤 Published sensor values: " + String(payload));

    // Send values to Blynk
    Blynk.virtualWrite(V1, flowRate1);
    Blynk.virtualWrite(V2, flowRate2);
    Blynk.virtualWrite(V3, pressure);

    // Check thresholds and trigger alarm
    if (alarmEnabled && (flowRate1 > FLOW_THRESHOLD || flowRate2 > FLOW_THRESHOLD || pressure > PRESSURE_THRESHOLD)) {
      if (!alarmActive) {
        alarmActive = true;
        Blynk.virtualWrite(V8, 1); // Turn on alarm indicator
        Blynk.logEvent("alarm_triggered", "🚨 Threshold exceeded on Node 1!");
        Serial.println("🚨 Alarm triggered!");
      }
    } else if (alarmActive) {
      alarmActive = false;
      Blynk.virtualWrite(V8, 0); // Turn off alarm indicator
      Serial.println("✅ Alarm cleared");
    }

    lastPublish = millis();
  }
}