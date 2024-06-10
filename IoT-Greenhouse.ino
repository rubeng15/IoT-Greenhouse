#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h> // Include the WiFi library for ESP32
#include <WiFiClient.h>
#include <Wire.h> // Include the Wire library for I2C communication
#include <LiquidCrystal_I2C.h> // Include the LiquidCrystal_I2C library for I2C LCD screen
#include <PubSubClient.h> // Include the MQTT library
#include <MFRC522.h> // Include the MFRC522 library for RFID

const char* ssid = "GOOVAERTSBUREAU";
const char* password = "0475876973";
const char* mqtt_server = "192.168.0.146"; // MQTT broker address
const int mqtt_port = 1883; // MQTT broker port

const int soilMoisturePin = 34; // Digital pin used for soil moisture sensor
const int dhtPin = 4;
const int relayPin = 2; // Pin for the relay and fan
const int heaterPin = 15; // Pin for the heater
const int waterPumpPin = 27; // Pin for the water pump
const int ledPin = 26; // Pin for the LED
const int ds18b20Pin = 14; // Pin for DS18B20 sensor

// RFID pins
#define SS_PIN 5
#define RST_PIN 13

DHT dht(dhtPin, DHT11);
OneWire oneWire(ds18b20Pin);
DallasTemperature sensors(&oneWire);
LiquidCrystal_I2C lcd(0x27, 16, 2); // Address of I2C LCD, 16 columns and 2 rows
WiFiClient espClient;
PubSubClient client(espClient);
MFRC522 rfid(SS_PIN, RST_PIN); // Create an instance of the RFID library

unsigned long previousMillis = 0; // Variable to store the last time LED was updated
const long interval = 60000; // Interval in milliseconds (1 minute)

float fanThreshold = 25.0; // Default temperature threshold for fan to turn on

// Define RFID card UIDs
byte card1UID[] = {0xA3, 0x81, 0x54, 0x28}; // Replace with the UID of card 1
byte card2UID[] = {0xF3, 0xAE, 0x3C, 0x28}; // Replace with the UID of card 2

enum DisplayMode {
  TEMPERATURE_HUMIDITY,
  RFID_MODE
};

DisplayMode currentDisplay = TEMPERATURE_HUMIDITY;
unsigned long displayChangeTime = 0;
const unsigned long displayDuration = 5000; // 5 seconds

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP32Client")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

bool isSameUID(byte *uid1, byte *uid2) {
  for (byte i = 0; i < 4; i++) {
    if (uid1[i] != uid2[i]) {
      return false;
    }
  }
  return true;
}

void setup() {
  Serial.begin(115200); // Increase the baud rate for faster communication
  delay(10);

  setup_wifi(); // Connect to WiFi

  client.setServer(mqtt_server, mqtt_port);

  dht.begin();
  sensors.begin();
  SPI.begin(); // Initialize SPI bus
  rfid.PCD_Init(); // Initialize the RFID reader

  pinMode(relayPin, OUTPUT);
  pinMode(heaterPin, OUTPUT);
  pinMode(waterPumpPin, OUTPUT);
  pinMode(ledPin, OUTPUT); // Set LED pin as output
  pinMode(relayPin, INPUT_PULLUP); // Set pin for the fan as input with internal pull-up resistor

  lcd.init(); // Initialize the LCD
  lcd.backlight(); // Turn on the backlight

  Serial.println("Setup completed.");
}

void loop() {
  if (!client.connected()) {
    reconnect(); // Reconnect to MQTT broker if connection is lost
  }
  client.loop();

  // Look for new RFID cards
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    byte *uid = rfid.uid.uidByte;

    if (isSameUID(uid, card1UID)) {
      // Card 1 detected
      fanThreshold = 25.0;
      Serial.println("Card 1 detected");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Peper-Mode");
      currentDisplay = RFID_MODE;
      displayChangeTime = millis() + displayDuration;
    } else if (isSameUID(uid, card2UID)) {
      // Card 2 detected
      fanThreshold = 28.0;
      Serial.println("Card 2 detected");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Tomaten-Mode");
      currentDisplay = RFID_MODE;
      displayChangeTime = millis() + displayDuration;
    } else {
      // Invalid card detected
      Serial.println("Ongeldige Kaart");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Ongeldige Kaart");
    }

    rfid.PICC_HaltA(); // Halt PICC
    rfid.PCD_StopCrypto1(); // Stop encryption on PCD
  }

  unsigned long currentMillis = millis(); // Get the current time

  // Toggle LED every minute
  if (currentMillis - previousMillis >= interval) {
    // Save the last time LED was updated
    previousMillis = currentMillis;

    // Toggle LED
    digitalWrite(ledPin, !digitalRead(ledPin));

    // Print LED state
    if (digitalRead(ledPin) == HIGH) {
      Serial.println("LED turned on");
      client.publish("esp32/led", "on");
    } else {
      Serial.println("LED turned off");
      client.publish("esp32/led", "off");
    }
  }

  if (currentDisplay == RFID_MODE && currentMillis >= displayChangeTime) {
    currentDisplay = TEMPERATURE_HUMIDITY;
  }

  // Read sensor values
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int soilMoisture = analogRead(soilMoisturePin); // Read soil moisture
  int fanState = digitalRead(relayPin); // Read the fan state

  // Publish sensor data to MQTT topics
  client.publish("esp32/temperature", String(temperature).c_str());
  client.publish("esp32/humidity", String(humidity).c_str());
  client.publish("esp32/soil_moisture", String(soilMoisture).c_str());
  client.publish("esp32/fan", fanState == HIGH ? "on" : "off");

  // Print sensor data to Serial Monitor
  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" °C");
  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");
  Serial.print("Soil Moisture: ");
  Serial.println(soilMoisture);
  Serial.print("Fan State: ");
  Serial.println(fanState);

  // Display sensor values on LCD
  lcd.setCursor(0, 0);

  switch (currentDisplay) {
    case TEMPERATURE_HUMIDITY:
      lcd.print("Temp: ");
      lcd.print(temperature);
      lcd.print("C");
      lcd.setCursor(0, 1);
      lcd.print("Humidity: ");
      lcd.print(humidity);
      lcd.print("%");
      break;
    case RFID_MODE:
      // Already handled when detecting RFID card
      break;
  }

  // Control heater, fan, and water pump based on thresholds
  if (!isnan(temperature) && !isnan(humidity)) {
    if (temperature < 21.0) { // Adjusted threshold for heater
      digitalWrite(heaterPin, HIGH);
      digitalWrite(relayPin, LOW);
      Serial.println("Heater turned on");
      client.publish("esp32/heater", "on");
    } else if (temperature >= fanThreshold) {
      digitalWrite(relayPin, HIGH);
      digitalWrite(heaterPin, LOW);
      Serial.println("Fan turned on");
      client.publish("esp32/fan", "on");
    } else {
      digitalWrite(heaterPin, LOW);
      digitalWrite(relayPin, LOW);
      Serial.println("Heater turned off");
      Serial.println("Fan turned off");
      client.publish("esp32/heater", "off");
      client.publish("esp32/fan", "off");
    }

    if (soilMoisture < 50) {
      digitalWrite(waterPumpPin, HIGH);
      delay(1000); // Run water pump for 1 second
      digitalWrite(waterPumpPin, LOW);
      Serial.println("Water pump turned on for 1 second");
      client.publish("esp32/water_pump", "on");
      delay(5000); // Delay for stability
    } else if (soilMoisture > 70) {
      digitalWrite(waterPumpPin, LOW);
      Serial.println("Water pump turned off");
      client.publish("esp32/water_pump", "off");
    }
  } else {
    Serial.println("Failed to read from DHT sensor!");
  }

  delay(5000); // Delay before next sensor reading
}
