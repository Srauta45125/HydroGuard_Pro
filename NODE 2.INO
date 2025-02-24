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

// Blynk auth token
char auth[] = "v-UDDmVfIPMLSaLBs9R3q7SjW9_h7a9X";

// Unique identifier for Node 2
const char* nodeID = "node2";

// MQTT topics
String mqtt_topic_pub = String("esp32/") + nodeID + "/sensor";
String mqtt_topic_sub = String("esp32/") + nodeID + "/command";

// Sensor Pins
#define FLOW_SENSOR_3 32
#define FLOW_SENSOR_4 33
#define PRESSURE_SENSOR 39

volatile int flow3_pulse_count = 0;
volatile int flow4_pulse_count = 0;
float calibrationFactor = 4.5; // Adjust based on your sensor's datasheet

// Threshold values
#define FLOW_THRESHOLD 20.0  // L/min
#define PRESSURE_THRESHOLD 8.0 // bar

bool alarmEnabled = true;  // Default: alarm is enabled

// Interrupt Service Routine (ISR) for flow sensors
void IRAM_ATTR pulseCounter3() {
  flow3_pulse_count++;
}
void IRAM_ATTR pulseCounter4() {
  flow4_pulse_count++;
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

// Blynk function to toggle alarm on/off
BLYNK_WRITE(V9) {
  alarmEnabled = param.asInt();
  Serial.println(alarmEnabled ? "🚨 Alarm Enabled" : "🔕 Alarm Disabled");
}

void setup() {
  Serial.begin(115200);
  setup_wifi();

  Blynk.begin(auth, ssid, password);
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  pinMode(FLOW_SENSOR_3, INPUT);
  pinMode(FLOW_SENSOR_4, INPUT);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_3), pulseCounter3, RISING);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_4), pulseCounter4, RISING);
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
    float flowRate3 = (flow3_pulse_count / 7.5); // Convert to L/min
    float flowRate4 = (flow4_pulse_count / 7.5);
    flow3_pulse_count = 0;
    flow4_pulse_count = 0;

    int rawPressure = analogRead(PRESSURE_SENSOR);
    float voltage = rawPressure * (3.3 / 4095.0); // Convert ADC value to voltage (ESP32 12-bit ADC)
    float pressure = (voltage - 0.5) * (1.2 / 4.0) * 10; // Convert voltage to pressure (MPa to bar)

    char payload[100];
    sprintf(payload, "Flow3: %.2f L/min, Flow4: %.2f L/min, Pressure: %.2f bar", flowRate3, flowRate4, pressure);
    mqttClient.publish(mqtt_topic_pub.c_str(), payload);
    
    Serial.println("📤 Published sensor values: " + String(payload));

    // Send values to Blynk
    Blynk.virtualWrite(V4, flowRate3);
    Blynk.virtualWrite(V5, flowRate4);
    Blynk.virtualWrite(V6, pressure);

    // Check for threshold violations
    if (alarmEnabled && (flowRate3 > FLOW_THRESHOLD || flowRate4 > FLOW_THRESHOLD || pressure > PRESSURE_THRESHOLD)) {
      Serial.println("🚨 ALERT! Threshold exceeded!");
      Blynk.virtualWrite(V10, 1); // Turn ON alarm indicator
      Blynk.logEvent("sensor_alarm_node2", "🚨 Node 2 Warning! Sensor values exceeded safe limits!");
    } else {
      Blynk.virtualWrite(V10, 0); // Turn OFF alarm indicator
    }

    lastPublish = millis();
  }
}