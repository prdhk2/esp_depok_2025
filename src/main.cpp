#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <U8g2lib.h>
#include "time.h"

// NTP Server Configuration
const char* ntpServer       = "pool.ntp.org";
const long gmtOffset_sec    = 25200; // GMT+7
struct tm p_tm;
//======================================network configuration======================================//
const char *ssid        = "OPTIMASI SISTEM DIGITAL";
const char *password    = "osd191067";
const char *serverUrl   = "#";

//======================================display configuration======================================//
U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, 18, 23, 5, 22); // SPI = E, R/W, RS, RST

//======================================real time (NTP) configuration======================================//
WiFiClient client;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, gmtOffset_sec, 60000);

//======================================Millis (time interval) configuration======================================//
unsigned long lastSendTime          = 0;
const unsigned long sendInterval    = 3600000; // 1 hour
unsigned long lastDebugTime         = 0;
const unsigned long debugInterval   = 1000; // 1 second

unsigned long lastUpdateTime        = 0; // Waktu terakhir fungsi diperbarui
unsigned long lastRefreshTime       = 0; // Waktu terakhir tampilan diperbarui
const unsigned long updateInterval  = 30000; // Interval 30 detik untuk bergantian fungsi
const unsigned long refreshInterval = 5000; // Interval 5 detik untuk refresh tampilan
bool showBalloonLevel               = false; // Flag untuk menentukan fungsi mana yang akan ditampilkan

//======================================variable declarations======================================//
float phValues[11], gasValues[2], pressureValues[2], temperatureValues[2];
float cm_1 = 0;
float cm_2 = 0;

bool dataSend = false;

//======================================Lcd Configuration======================================//
int currentScreen = 0; // 0 = Gas Data, 1 = Balloon Level, 2 = PH Meter

//======================================pins declarations======================================//
const int lcdButtonPin              = 2;
bool lcdButtonPressed               = false;
int lcdButtonState                  = LOW;
unsigned long debounceDelay         = 100; //100ms
unsigned long lastDebounceTime      = 0;

//======================================function declaration======================================//
void connectToWiFi();
void loading_lcd();
void sendDataToServer();
void receiveDataFromArduino(String dataReceived, int source);
void debugReceivedData();
void checkWiFiConnection();
void ensureTimeSync();
void displayGasData();
void displayBalloonLevel();
void printHttpResponse(HTTPClient& http, int httpResponseCode);
void handleButtonPressed();
void updateScreen();

void setup() {
    Serial.begin(9600); // master 1 Serial for Debugging
    Serial2.begin(9600, SERIAL_8N1, 16, 17); // master 2 serial (gas meter)
    connectToWiFi();
    timeClient.begin();
    u8g2.begin();
    loading_lcd();
}

void loop() {
    ensureTimeSync();

    timeClient.update();
    time_t now = timeClient.getEpochTime();
    localtime_r(&now, &p_tm);

    unsigned long currentMillis = millis();

    // Manage data sending every hour
    if (!dataSend && p_tm.tm_min == 1) {
        sendDataToServer();
        dataSend = true;
    }

    if (p_tm.tm_min == 2) {
        dataSend = false;
    }

    // Check and read data from Arduino Nano
    if (Serial.available() > 0) {
        String dataReceived = Serial.readStringUntil('\n');
        receiveDataFromArduino(dataReceived, 1); // Read from Arduino Nano
    }

    // Check and read data from Arduino Uno
    if (Serial2.available() > 0) {
        String dataReceived = Serial2.readStringUntil('\n');
        receiveDataFromArduino(dataReceived, 2); // Read from Arduino Uno
    }

    // Check Wi-Fi connection
    checkWiFiConnection();

    // Debug received data every second
    if (millis() - lastDebugTime >= debugInterval) {
        debugReceivedData();
        lastDebugTime = millis();
    }

    // Bergantian antara displayGasData(), displayBalloonLevel(), displayPhMeter() setiap 30 detik
    if (currentMillis - lastUpdateTime >= updateInterval) {
        currentScreen = (currentScreen + 1) % 3; // Looping screens 0, 1, 2
        lastUpdateTime = currentMillis;
        lastRefreshTime = currentMillis;
    }

    // Refresh screen setiap 5 detik saat fungsi aktif
    if (currentMillis - lastRefreshTime >= refreshInterval) {
        updateScreen();
        lastRefreshTime = currentMillis;
    }

    handleButtonPressed();
}

void connectToWiFi() {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.println("Connecting to WiFi...");
        delay(1000);
    }
    Serial.println("Connected to WiFi");
}

void ensureTimeSync() {
    if (!timeClient.update()) {
        // Serial.println("Failed to synchronize time.");
    }
}

void checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected. Reconnecting...");
        connectToWiFi();
    }
}

void receiveDataFromArduino(String dataReceived, int source) {
    const int maxUnoValues  = 13;
    const int maxNanoValues = 6;
    float values[13]    = {0};
    int index           = 0;

    // Split the data by comma and store it in the values array
    int startIndex = 0, commaIndex;
    while ((commaIndex = dataReceived.indexOf(',', startIndex)) != -1 && index < maxUnoValues) {
        values[index++] = dataReceived.substring(startIndex, commaIndex).toFloat();
        startIndex = commaIndex + 1;
    }
    // Capture the last value if there's remaining data
    if (startIndex < dataReceived.length() && index < maxUnoValues) {
        values[index++] = dataReceived.substring(startIndex).toFloat();
    }

    // Process data based on the source
    if (source == 1 && index == maxUnoValues) { // Data from Arduino Uno
        memcpy(phValues, values, sizeof(float) * 11); // Copy pH values
        cm_1 = values[11];
        cm_2 = values[12];
    } else if (source == 2 && index == maxNanoValues) { // Data from Arduino Nano
        memcpy(gasValues, values, sizeof(float) * 2);          // Copy gas values
        memcpy(pressureValues, values + 2, sizeof(float) * 2); // Copy pressure values
        memcpy(temperatureValues, values + 4, sizeof(float) * 2); // Copy temperature values
    }
}

void sendDataToServer() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
            String postData = "ph_1=" + String(phValues[0], 2) +
                            "&ph_2=" + String(phValues[1], 2) +
                            "&ph_3=" + String(phValues[2], 2) +
                            "&ph_4=" + String(phValues[3], 2) +
                            "&ph_5=" + String(phValues[4], 2) +
                            "&ph_6=" + String(phValues[5], 2) +
                            "&ph_7=" + String(phValues[6], 2) +
                            "&ph_8=" + String(phValues[7], 2) +
                            "&ph_9=" + String(phValues[8], 2) +
                            "&ph_10=" + String(phValues[9], 2) +
                            "&ph_11=" + String(phValues[10], 2) +
                            "&gas_value_1=" + String(gasValues[0], 3) +
                            "&pressureGas_1=" + String(pressureValues[0], 3) +
                            "&TemperatureGas_1=" + String(temperatureValues[0], 3) +
                            "&gas_value_2=" + String(gasValues[1], 3) +
                            "&pressureGas_2=" + String(pressureValues[1], 3) +
                            "&TemperatureGas_2=" + String(temperatureValues[1], 3) +
                            "&distance1=" + String(cm_1, 2) +
                            "&distance2=" + String(cm_2, 2);

        Serial.println("Sending data to server...");
        Serial.println("POST data: " + postData);

        http.begin(serverUrl);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");

        int httpResponseCode = http.POST(postData);
        printHttpResponse(http, httpResponseCode);
        http.end();
    } else {
        Serial.println("WiFi not connected. Cannot send data.");
    }
}

void printHttpResponse(HTTPClient& http, int httpResponseCode) {
    if (httpResponseCode > 0) {
        // Server gives response
        Serial.println("HTTP Response code: " + String(httpResponseCode));
        String response = http.getString();
        Serial.println("Server response: " + response);
    } else {
        // Error occurred
        Serial.println("Error in sending POST: " + String(httpResponseCode));
        if (httpResponseCode == -1) {
            Serial.println("Network error. Please check the connection.");
        } else {
            Serial.println("HTTP error code: " + String(httpResponseCode));
        }
    }
}

void debugReceivedData() {
    Serial.print("pH 1: ");
    Serial.println(phValues[0], 2);
    
    Serial.print("pH 2: ");
    Serial.println(phValues[1], 2);
    
    Serial.print("pH 3: ");
    Serial.println(phValues[2], 2);
    
    Serial.print("pH 4: ");
    Serial.println(phValues[3], 2);
    
    Serial.print("pH 5: ");
    Serial.println(phValues[4], 2);
    
    Serial.print("pH 6: ");
    Serial.println(phValues[5], 2);
    
    Serial.print("pH 7: ");
    Serial.println(phValues[6], 2);
    
    Serial.print("pH 8: ");
    Serial.println(phValues[7], 2);
    
    Serial.print("pH 9: ");
    Serial.println(phValues[8], 2);
    
    Serial.print("pH 10: ");
    Serial.println(phValues[9], 2);
    
    Serial.print("pH 11: ");
    Serial.println(phValues[10], 2);

    Serial.print("Gas 1: ");
    Serial.println(gasValues[0], 3);

    Serial.print("Gas 2: ");
    Serial.println(gasValues[1], 3);

    Serial.print("Pressure 1: ");
    Serial.println(pressureValues[0], 2);

    Serial.print("Pressure 2: ");
    Serial.println(pressureValues[1], 2);

    Serial.print("Temperature Gas 1: ");
    Serial.println(temperatureValues[0], 2);

    Serial.print("Temperature Gas 2: ");
    Serial.println(temperatureValues[1], 2);

    Serial.print("Baloon 1: ");
    Serial.println(cm_1, 2);

    Serial.print("Baloon 2: ");
    Serial.println(cm_2, 2);

    Serial.print("Local time now: ");
    Serial.print(p_tm.tm_hour);
    Serial.print(":");
    Serial.print(p_tm.tm_min);
    Serial.print(":");
    Serial.print(p_tm.tm_sec);
    Serial.println();
}

void loading_lcd() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setFontMode(1); // Bold 
    u8g2.drawStr(8, 15, "... Loading data ...");
    u8g2.drawStr(45, 25, "");
    u8g2.setFontMode(0); // reset Bold

    int barWidth    = 100;
    int barHeight   = 10;
    int progress    = 0;

    for (int i = 0; i < barWidth; i++) {
        u8g2.drawBox(14 + i, 40, 1, barHeight);
        u8g2.sendBuffer();
        delay(3);

        if (i % 10 == 0) {
            progress += 10;
        }
    }
}

void displayBalloonLevel() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawFrame(0, 0, u8g2.getDisplayWidth(), 64);
    
    /* ===================== Balloon 1 ===================== */
    u8g2.drawStr(2, 10, "Balloon 1: ");
    
    float nilai_relatif_1   = cm_1 - 20;
    int persentase_1        = 100 - nilai_relatif_1;
    persentase_1            = constrain(persentase_1, 0, 100);

    char BlnLvl1[10];
    dtostrf(persentase_1, 4, 0, BlnLvl1);

    if (persentase_1 == 100) {
        u8g2.drawStr(55, 10, "is Full !");
    } else if(persentase_1 <= 10) {
        u8g2.drawStr(55, 10, "is Empty !");
    } else {    
        u8g2.drawStr(55, 10, BlnLvl1);
        u8g2.drawStr(103, 10, "%");
    }

    int barHeight   = 10;
    int barWidth    = (u8g2.getDisplayWidth() - 4) * persentase_1 / 100;
    u8g2.drawBox(2, 15, barWidth, barHeight); 

    u8g2.drawHLine(0, 30, u8g2.getDisplayWidth());

    /* ===================== Balloon 2 ===================== */
    u8g2.drawStr(2, 40, "Balloon 2: ");
    
    float nilai_relatif_2   = cm_2 - 20;
    int persentase_2        = 100 - nilai_relatif_2;
    persentase_2            = constrain(persentase_2, 0, 100);

    char BlnLvl2[10];
    dtostrf(persentase_2, 4, 0, BlnLvl2);

    if (persentase_2 == 100) {
        u8g2.drawStr(55, 40, "is Full !");
    } else if(persentase_2 <= 10) {
        u8g2.drawStr(55, 10, "is Empty !");
    } else {    
        u8g2.drawStr(55, 40, BlnLvl2);
        u8g2.drawStr(103, 40, "%");
    }

    barWidth = (u8g2.getDisplayWidth() - 4) * persentase_2 / 100;
    u8g2.drawBox(2, 45, barWidth, barHeight);

    /* ===================== Send Buffer ===================== */
    u8g2.sendBuffer();
}

void displayGasData() { 
    //untuk menampilkan data gas meter
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawFrame(0, 0, u8g2.getDisplayWidth(), 60);
    /* ===================== Space ===================== */

    u8g2.drawStr(2, 8, "Gas Prod");

    char GfStr[10];
    dtostrf(gasValues[0], 4, 3, GfStr);
    u8g2.drawStr(47, 8, GfStr);
    u8g2.drawStr(103, 8, " /m3");

    u8g2.drawHLine(0, 9, u8g2.getDisplayWidth());

    /* ===================== Space ===================== */

    u8g2.drawStr(2, 18, "Gas Used");

    char Gf2Str[10];
    dtostrf(gasValues[1], 4, 3, Gf2Str);
    u8g2.drawStr(47, 18, Gf2Str);
    u8g2.drawStr(103, 18, " /m3");

    u8g2.drawHLine(0, 19, u8g2.getDisplayWidth());

    /* ===================== Space ===================== */

    u8g2.drawStr(2, 28, "T Gas 1");

    char TempGasStr[10];
    dtostrf(temperatureValues[0], 4, 2, TempGasStr);
    u8g2.drawStr(47, 28, TempGasStr);
    u8g2.drawStr(106, 28, " 'C");

    u8g2.drawHLine(0, 29, u8g2.getDisplayWidth());


    u8g2.drawStr(2, 38, "T Gas 2");

    dtostrf(temperatureValues[1], 4, 2, TempGasStr);
    u8g2.drawStr(47, 38, TempGasStr);

    u8g2.drawStr(106, 38, " 'C");

    u8g2.drawHLine(0, 39, u8g2.getDisplayWidth());

    /* ===================== Space ===================== */

    u8g2.drawStr(2, 48, "P Gas 1");

    char pressGasStr[10];
    dtostrf(pressureValues[0], 4, 2, pressGasStr);
    u8g2.drawStr(47, 48, pressGasStr); //26

    u8g2.drawHLine(0, 49, u8g2.getDisplayWidth());

    u8g2.drawStr(2, 58, "P Gas 2");

    dtostrf(pressureValues[1], 4, 2, pressGasStr);
    u8g2.drawStr(47, 58, pressGasStr);

    u8g2.drawVLine(45, 0, 60); //garis vertikal

    u8g2.sendBuffer();
}

void displayPhMeter() { 
    //screen 2 untuk menampilkan data Ph meter

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);

    u8g2.drawFrame(0, 0, 63, 60); //frame kiri

    /* ===================== Setup space kiri ===================== */

    u8g2.drawStr(2, 8, "Ph 1");

    char phStr[10];
    dtostrf(phValues[0], 4, 2, phStr);
    u8g2.drawStr(33, 8, phStr); //26

    u8g2.drawHLine(0, 10, 62);

    /* ===================== Setup space kiri ===================== */

    u8g2.drawStr(2, 18, "Ph 2");

    dtostrf(phValues[1], 4, 2, phStr);
    u8g2.drawStr(33, 18, phStr); //26

    u8g2.drawHLine(0, 20, 62);

    /* ===================== Setup space kiri ===================== */

    u8g2.drawStr(2, 28, "Ph 3");

    dtostrf(phValues[2], 4, 2, phStr);
    u8g2.drawStr(33, 28, phStr); //26

    u8g2.drawHLine(0, 30, 62);

    /* ===================== Setup space kiri ===================== */

    u8g2.drawStr(2, 38, "Ph 4");

    dtostrf(phValues[3], 4, 2, phStr);
    u8g2.drawStr(33, 38, phStr); //26

    u8g2.drawHLine(0, 40, 62);

    /* ===================== Setup space kiri ===================== */

    u8g2.drawStr(2, 48, "Ph 5");

    dtostrf(phValues[4], 4, 2, phStr);
    u8g2.drawStr(33, 48, phStr); //26

    u8g2.drawHLine(0, 50, 62);

    /* ===================== Setup space kiri ===================== */

    u8g2.drawStr(2, 58, "Ph 6");

    dtostrf(phValues[5], 4, 2, phStr);
    u8g2.drawStr(33, 58, phStr); //26

/* ===================== End Setup space Kiri ===================== */

/* =================== Start Setup space kanan ==================== */

    u8g2.drawFrame(65, 0, 63, 50); //frame kanan

    u8g2.drawStr(67, 8, "Ph 7");

    dtostrf(phValues[6], 4, 2, phStr);
    u8g2.drawStr(100, 8, phStr); //26

    u8g2.drawHLine(65, 10, 63);

    /* ===================== Setup space kanan ===================== */

    u8g2.drawStr(67, 18, "Ph 8");

    dtostrf(phValues[7], 4, 2, phStr);
    u8g2.drawStr(100, 18, phStr); //26

    u8g2.drawHLine(65, 20, 63);

    /* ===================== Setup space kanan ===================== */

    u8g2.drawStr(67, 28, "Ph 9");

    dtostrf(phValues[8], 4, 2, phStr);
    u8g2.drawStr(100, 28, phStr); //26

    u8g2.drawHLine(65, 30, 63);

    /* ===================== Setup space kanan ===================== */

    u8g2.drawStr(67, 38, "Ph 10");

    dtostrf(phValues[9], 4, 2, phStr);
    u8g2.drawStr(100, 38, phStr); //26

    u8g2.drawHLine(65, 40, 63);

    /* ===================== Setup space kanan ===================== */

    u8g2.drawStr(67, 48, "Ph 11");

    dtostrf(phValues[10], 4, 2, phStr);
    u8g2.drawStr(100, 48, phStr); //26

    //   u8g2.drawHLine(65, 50, 63);

    /* ===================== Setup space kanan ===================== */

    u8g2.drawVLine(28, 0, 60); //garis vertikal kiri
    u8g2.drawVLine(95, 0, 50); //garis vertikal kanan

    u8g2.sendBuffer();

}

void handleButtonPressed() {
    int buttonState = digitalRead(lcdButtonPin);

        if (buttonState == HIGH ) { // LOW = tombol ditekan
            // if (millis() - lastDebounceTime > debounceDelay) {
                // Ganti layar
                currentScreen = (currentScreen + 1) % 3;
                // lastDebounceTime = millis();

                Serial.println("button pressed");
            // }
        }
        lcdButtonState = buttonState;
}

void updateScreen() {
    switch (currentScreen) {
        case 0:
            displayGasData();
            break;
        case 1:
            displayBalloonLevel();
            break;
        case 2:
            displayPhMeter();
            break;
    }
}
