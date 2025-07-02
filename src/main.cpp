//SAMOGONER AE 3000
//HW-364A 8266 NodeMCU + 0.96 OLED + 1Wire
//в планах логирование и отправка температур в базу sql для построения графиков
//21.06.25 поддержка олед экрана с температурами и временем разгонки, 2 датчика температуры по 1w, светодиоды, зуммер, кнопка, wifi, tg, ota, реле (на будущее) 
//25,06,25 изменена логика, проверка датчиков, сайт, логирование  
//27,06,25 изменена логика, датчик перелива, термостат, убрано реле
//03.07.25 добавлен функционал тестирования платы, через curl - задавая значение датчиков

// curl "http://192.168.1.42/test?mode=on&t1=65.0&t2=30.0&liq=200&thermo=0"
// curl "http://192.168.1.42/test?mode=off"


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
#include <ESP8266WebServer.h>  

#include "config.h"

#define ONE_WIRE_BUS D2     // DS18B20
#define RED_LED D1          // Красный
#define BLUE_LED D7         // Синий
#define BUZZER_PIN D8       // Зуммер
#define BUTTON_PIN D3       // Кнопка
#define OLED_SDA D5         // OLED SDA
#define OLED_SCL D6         // OLED SCL
#define THERMOSTAT_PIN D0        // TERMOSTAT
#define LIQUID_SENSOR_PIN A0     // Перелив 

#define LIQUID_THRESHOLD 300 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
ESP8266WebServer server(80);   

DeviceAddress sensor1 = { 0x28, 0xC0, 0x9A, 0x5A, 0x00, 0x00, 0x00, 0x48 }; //в кубе
DeviceAddress sensor2 = { 0x28, 0x0F, 0xF8, 0x5A, 0x00, 0x00, 0x00, 0x61 }; //вода

WiFiClientSecure client;
UniversalTelegramBot bot(tgBotToken, client);

unsigned long startMillis;
unsigned long lastBeepMillis = 0;
unsigned long nextHourlyMillis = 0; 

//bool alarmActive = false;  // сигнал
bool buttonAcknowledged = false;  //сброса сигнала

// Tg 
bool sent60=false, sent79=false, sent92=false, sent97=false, sent52_2=false, sent60_2=false;

bool buzzerActive = false;
int beepMode = 0;

bool sensor1ErrorSent = false;
bool sensor2ErrorSent = false;

//тестирование
bool testMode = false;      
float testT1 = 0.0;         
float testT2 = 0.0;         
int   testLiquid = 0;       
bool  testThermo = false;   


void sendLogEvent(const String &eventMessage) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClient client;
    http.begin(client, "http://192.168.1.123:54321/log");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String postData = "event=" + eventMessage;
    int httpCode = http.POST(postData);
    if (httpCode > 0) {
      Serial.println("Событие отправлено: " + eventMessage);
    } else {
      bot.sendMessage(chatId4, "Ошибка логирования", "");
    }
    http.end();
  } else {
    Serial.println("WiFi не подключен");
  }
}

void sendTelegramMessage(const String &message) {
  bot.sendMessage(chatId4, message, "");
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting ");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  client.setInsecure();
}

void setup() {
  Serial.begin(115200);

  pinMode(RED_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(THERMOSTAT_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);  //встроенный светодиод d4 

  // OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 failed"));
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
  nextHourlyMillis = startMillis + 3600000;  

  setupWiFi();

  //тест
  digitalWrite(RED_LED, HIGH);              
  digitalWrite(BLUE_LED, HIGH);             
  digitalWrite(BUZZER_PIN, HIGH);
  digitalWrite(LED_BUILTIN, LOW);
  delay(1000);                               
  digitalWrite(RED_LED, LOW);
  digitalWrite(BLUE_LED, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_BUILTIN, HIGH);

  sendLogEvent("Запуск Самогонера АЕ 3000 ");   
  delay(500);

  bool success = bot.sendMessage(chatId4, "Запуск Cамогонера AE 3000 ", "");
  if (success) {
    Serial.println("Сообщение отправлено ");
  } else {
    Serial.println("Ошибка отправки о запуске ");
  }

  ArduinoOTA.begin();  

  server.on("/", []() {
    sensors.requestTemperatures();
    float h1 = testMode ? testT1 : sensors.getTempC(sensor1);
    float h2 = testMode ? testT2 : sensors.getTempC(sensor2);
    unsigned long elapsed = millis() - startMillis;
    unsigned long minutes = elapsed / 60000;

    String dataHtml = "<h1>AE Automation</h1>"
                    "<p>Время работы: " + String(minutes) + " мин</p>"
                    "<p>В кубе: " + String(h1,1) + "°C</p>"
                    "<p>Охлаждение: " + String(h2,1) + "°C</p>";  

    String page =
      "<!DOCTYPE html>"
      "<html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>Данные Самогонера</title>"
        "<style>"
          "html, body { margin:0; padding:0; min-height:100vh; }"
          "body {"
            "background: url('https://cojo.ru/wp-content/uploads/2022/12/anime-fon-luna-1.webp') no-repeat center center fixed;"
            "background-size: cover;"
            "font-family: Arial, sans-serif;"
            "color: #fff;"
            "text-align: center;"
            "padding-top: 50px;"
          "}"
          "h1 { color: #FF0000; }"
        "</style>"
      "</head><body>" +
        dataHtml +
      "</body></html>";

    server.send(200, "text/html", page);
  });

  server.on("/test", HTTP_GET, [](){
    bool prevMode = testMode;

    if (server.hasArg("mode")) {
      testMode = (server.arg("mode") == "on");
    }

    if (testMode && !prevMode) {
      sent60   = false;
      sent79   = false;
      sent92   = false;
      sent97   = false;
      sent52_2 = false;
      sent60_2 = false;
    }

    if (testMode) {
      if (server.hasArg("t1")) testT1 = server.arg("t1").toFloat();
      if (server.hasArg("t2")) testT2 = server.arg("t2").toFloat();
      if (server.hasArg("liq")) testLiquid = server.arg("liq").toInt();
      if (server.hasArg("thermo")) testThermo = (server.arg("thermo") == "1");
    }

    if (testMode != prevMode) {
      sendTelegramMessage(testMode ? "Тестирование Включено" : "Тестирование Выключено");
    }

    String resp = String("Test mode ") + (testMode ? "ON" : "OFF");
    if (testMode) {
      resp += String("  t1=") + testT1
           + String("  t2=") + testT2
           + String("  liq=") + testLiquid
           + String("  thermo=") + (testThermo ? "1":"0");
    }

    server.send(200, "text/plain", resp);
  });

  server.begin();

}

void loop() {
  // мигает если с вайфаем
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, LOW);   
    delay(150);
    digitalWrite(LED_BUILTIN, HIGH);  
    delay(50);
  } else {
    digitalWrite(LED_BUILTIN, HIGH); 
  }

  oneWire.reset();  
  sensors.requestTemperatures();
//  float t1 = sensors.getTempC(sensor1);
//  float t2 = sensors.getTempC(sensor2);

  float t1 = testMode ? testT1 : sensors.getTempC(sensor1);
  float t2 = testMode ? testT2 : sensors.getTempC(sensor2);
  int   liquidValue = testMode ? testLiquid : analogRead(LIQUID_SENSOR_PIN);
  bool  thermoTriggered = testMode ? testThermo : (digitalRead(THERMOSTAT_PIN) == HIGH);


  // валидность температур
  if (t1 == DEVICE_DISCONNECTED_C || !sensors.isConnected(sensor1)) {
    if (!sensor1ErrorSent) {
      sendTelegramMessage("Ошибка: T1 отключён!");
      sendLogEvent("Sensor1 offline");
      sensor1ErrorSent = true;
    }
  } else {
    sensor1ErrorSent = false;
  }
  if (t2 == DEVICE_DISCONNECTED_C || !sensors.isConnected(sensor2)) {
    if (!sensor2ErrorSent) {
      sendTelegramMessage("Ошибка: T2 отключён!");
      sendLogEvent("Sensor2 offline");
      sensor2ErrorSent = true;
    }
  } else {
    sensor2ErrorSent = false;
  }

  if (t1 != DEVICE_DISCONNECTED_C && sensors.isConnected(sensor1)) {
  sensor1ErrorSent = false;
  }
  if (t2 != DEVICE_DISCONNECTED_C && sensors.isConnected(sensor2)) {
  sensor2ErrorSent = false;
  }

  //Время мин
  unsigned long elapsedMillis = millis() - startMillis;
  unsigned long minutes = elapsedMillis / 60000;

  // OLED
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

  //перелив

  static bool overflowActive = false;
 // int liquidValue = analogRead(LIQUID_SENSOR_PIN);                  
  bool overflow = liquidValue > LIQUID_THRESHOLD;                   
  
  if (overflow && !overflowActive) {
    overflowActive = true;
    digitalWrite(RED_LED, HIGH);                                     
    buzzerActive = true;                                             
    beepMode = 1;                                                    
    sendTelegramMessage("Перелив");                                  
    sendLogEvent("Перелив");
  } 
  else if (!overflow && overflowActive) {
    overflowActive = false;
    buzzerActive = false; 
    beepMode = 0;                                                 
  }


  // термостат
  
  static bool thermoActive = false;
//  bool thermoTriggered = digitalRead(THERMOSTAT_PIN) == HIGH;        
  

  if (thermoTriggered && !thermoActive) {
    thermoActive = true;
    digitalWrite(RED_LED, HIGH);
    buzzerActive = true;
    beepMode = 2;                                                    
    sendTelegramMessage("Пропало охлаждение!");
    sendLogEvent("Пропало охлаждение!");
  } 
  else if (!thermoTriggered && thermoActive) {
    thermoActive = false;
    buzzerActive = false; 
    beepMode = 0;                                                 
  }

// Индикация
  digitalWrite(BLUE_LED, (t1 >= 79.0 && t1 < 92.5) ? HIGH : LOW);
  if (!overflowActive && !thermoActive) {
    digitalWrite(RED_LED, (t1 >= 92.5 || t2 > 50.0) ? HIGH : LOW);
  }  

// Пороги, зуммер и тг

  if (t1 >= 60.0 && !sent60) {
    sendTelegramMessage("Включить охлаждение");
    sendLogEvent("Охлаждение");
    sent60 = true; buzzerActive = true; buttonAcknowledged = false; beepMode = 1; // 0.5с/5с
  }
  if (t1 < 59.0) sent60 = false;

  if (t1 >= 79.0 && !sent79) {
    sendTelegramMessage("Тело");
    sendLogEvent("Тело");
    sent79 = true; buzzerActive = true; buttonAcknowledged = false; beepMode = 1;   
  }
  if (t1 < 78.0) sent79 = false;

  if (t1 >= 92.5 && !sent92) {
    sendTelegramMessage("Хвосты!");
    sendLogEvent("Хвосты");
    sent92 = true; buzzerActive = true; buttonAcknowledged = false; beepMode = 1; 
  }
  if (t1 < 91.5) sent92 = false;

  if (t2 >= 52.0 && !sent52_2) {
    sendTelegramMessage("Перегрев");
    sendLogEvent("Перегрев");
    sent52_2 = true; buzzerActive = true; buttonAcknowledged = false; beepMode = 2; // 1с/2с
  }
  if (t2 < 49.0) sent52_2 = false;

  if (t2 >= 60.0 && !sent60_2) {
    sendTelegramMessage("Штанга");
    sendLogEvent("Штанга");
    sent60_2 = true; buzzerActive = true; buttonAcknowledged = false; beepMode = 2;
  }
  if (t2 < 59.0) sent60_2 = false;

  if (t1 >= 97.6 && !sent97) {
    sendTelegramMessage("Остановись!");
    sendLogEvent("Остановись");
    sent97 = true; buzzerActive = true; buttonAcknowledged = false; beepMode = 3; // 3с/1с
  }
  if (t1 < 96.9) sent97 = false;

  // Сброс зуммера кнопкой
  
  if (digitalRead(BUTTON_PIN) == LOW && buzzerActive && beepMode != 3) {
    buttonAcknowledged = true;
    digitalWrite(BUZZER_PIN, LOW);
  }

  if (buzzerActive && !buttonAcknowledged) {
    unsigned long now = millis();
    switch (beepMode) {
      case 0:  // выкл 
        digitalWrite(BUZZER_PIN, LOW);
        break;

      case 1: // 0.5с в 5сек
        if (now - lastBeepMillis >= 5000) {
          digitalWrite(BUZZER_PIN, HIGH);
          delay(500);
          digitalWrite(BUZZER_PIN, LOW);
          lastBeepMillis = now;
        }
        break;

      case 2: // 1/2
        if (now - lastBeepMillis >= 2000) {
          digitalWrite(BUZZER_PIN, HIGH);
          delay(1000);
          digitalWrite(BUZZER_PIN, LOW);
          lastBeepMillis = now;
        }
        break;
        
      case 3: // 3/1
        if (now - lastBeepMillis >= 4000) {
          digitalWrite(BUZZER_PIN, HIGH);
          delay(3000);
          digitalWrite(BUZZER_PIN, LOW);
          lastBeepMillis = now;
        }
        break;
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }

  if (millis() >= nextHourlyMillis) {
    unsigned long hours = (millis() - startMillis) / 3600000;
    sendTelegramMessage("Уже " + String(hours) + " ч с запуска");
    sendLogEvent("Часов работы: " + String(hours));
    nextHourlyMillis += 3600000;
  }  

  server.handleClient();  

  ArduinoOTA.handle();  

  delay(50);
}
