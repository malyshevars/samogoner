//SAMOGONER AE 3000
//HW-364A 8266 NodeMCU + 0.96 OLED 
//в планах отправка в базу sql для построения графиков
//21.06.25 поддержка олед экрана с температурами и временем разгонки, 2 датчика температуры по 1w, светодиоды, зуммер, кнопка, wifi, tg, ota, реле (на будущее) 


#include <Arduino.h>

#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPClient.h>

#include "config.h"

#define ONE_WIRE_BUS D2     // DS18B20
#define RED_LED D1          // Красный
#define BLUE_LED D7         // Синий
#define BUZZER_PIN D8       // Зуммер
#define BUTTON_PIN D3       // Кнопка
#define RELAY_PIN D0        // Реле
#define OLED_SDA D5         // OLED SDA
#define OLED_SCL D6         // OLED SCL

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

DeviceAddress sensor1 = { 0x28, 0xC0, 0x9A, 0x5A, 0x00, 0x00, 0x00, 0x48 }; //в кубе
DeviceAddress sensor2 = { 0x28, 0x0F, 0xF8, 0x5A, 0x00, 0x00, 0x00, 0x61 }; //вода

WiFiClientSecure client;
UniversalTelegramBot bot(tgBotToken, client);

unsigned long startMillis;
unsigned long lastBeepMillis = 0;
bool alarmActive = false;  // сигнал
bool buttonAcknowledged = false;  //сброса сигнала

// Tg 
bool sent60 = false;
bool sent79 = false;
bool sent92 = false;
bool sent50_2 = false;
bool sent60_2 = false;

bool buzzerActive = false;

void sendTelegramMessage(const String &message) {
  bot.sendMessage(chatId4, message, "");
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());

  client.setInsecure();
}

void setup() {
  Serial.begin(115200);

  // Пины
  pinMode(RED_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);  // светодиод d4

  // OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("SAMOGONER AE 3000");
  display.display();

  // 1wire
  sensors.begin();

  startMillis = millis();

  setupWiFi();

  delay(1000);
  bool success = bot.sendMessage(chatId4, "Запуск Cамогонера AE 3000", "");
  if (success) {
    Serial.println("Сообщение о запуске отправлено.");
  } else {
    Serial.println("Ошибка отправки сообщения о запуске.");
  }

  ArduinoOTA.begin();  

}

void loop() {
  // мигает
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
  digitalWrite(LED_BUILTIN, HIGH);

  sensors.requestTemperatures();
  float t1 = sensors.getTempC(sensor1);
  float t2 = sensors.getTempC(sensor2);

  // Время в минутах
  unsigned long elapsedMillis = millis() - startMillis;
  unsigned long minutes = elapsedMillis / 60000;

  // OLED вывод
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.printf("Time:%lu", minutes);

  display.setTextSize(2);
  display.setCursor(0, 16);
  display.printf("T1:%.1f", t1);
  display.setCursor(0, 40);
  display.printf("T2:%.1f", t2);
  display.display();

// Индикация
  if (t1 >= 79 && t1 < 92) {
    digitalWrite(BLUE_LED, HIGH);
  } else {
    digitalWrite(BLUE_LED, LOW);
  }

  if (t1 >= 92 || t2 > 50) {
    digitalWrite(RED_LED, HIGH);
  } else {
    digitalWrite(RED_LED, LOW);
  }

// Пороги, зуммер + тг

  if (t1 >= 60 && !sent60) {
    sendTelegramMessage("Включить охлаждение");
    sent60 = true;
    buzzerActive = true;
    buttonAcknowledged = false;
  }
  if (t1 >= 79 && !sent79) {
    sendTelegramMessage("Тело");
    sent79 = true;
    buzzerActive = true;
    buttonAcknowledged = false;
  }
  if (t1 >= 92 && !sent92) {
    sendTelegramMessage("Хвосты!");
    sent92 = true;
    buzzerActive = true;
    buttonAcknowledged = false;
  }
  if (t2 >= 50 && !sent50_2) {
    sendTelegramMessage("Перегрев");
    sent50_2 = true;
    buzzerActive = true;
    buttonAcknowledged = false;
  }
  if (t2 >= 60 && !sent60_2) {
    sendTelegramMessage("Штанга");
    sent60_2 = true;
    buzzerActive = true;
    buttonAcknowledged = false;
  }

  // Обработка кнопки
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (buzzerActive) {
      buttonAcknowledged = true;
      digitalWrite(BUZZER_PIN, LOW);
    }
  }

  if (t1 < 60) sent60 = false;
  if (t1 < 79) sent79 = false;
  if (t1 < 92) sent92 = false;
  if (t2 < 50) sent50_2 = false;
  if (t2 < 60) sent60_2 = false;

  // Зуммер
  if (buzzerActive && !buttonAcknowledged) {
    if (millis() - lastBeepMillis >= 5000) {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(450);
      digitalWrite(BUZZER_PIN, LOW);
      lastBeepMillis = millis();
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }

  digitalWrite(RELAY_PIN, LOW);

  ArduinoOTA.handle();  

  delay(50);
}
