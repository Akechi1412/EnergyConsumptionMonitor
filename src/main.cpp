#include <Arduino.h>
#include <WiFi.h>
// #define FIREBASE_USE_PSRAM
#include <Firebase_ESP_Client.h>
// Provide the token generation process info.
#include <addons/TokenHelper.h>
// Provide the RTDB payload printing info and other helper functions.
#include <addons/RTDBHelper.h>
#include "EmonLib.h"

#define WIFI_SSID "WIFI_SSID"
#define WIFI_PASSWORD "WIFI_PASSWORD"

#define API_KEY "API_KEY"
#define DATABASE_URL "DATABASE_URL"

#define CURRENT_ADC_INPUT 32               // The GPIO pin is connect to ZMCP103C module
#define VOLTAGE_ADC_INPUT 33               // The GPIO pin is connect to ZMPT101B module
#define CURRENT_CALIBRATION 0.4670
#define VOLTAGE_CALIBRATION 280.80
#define PHASE_CALIBRATION 1.7

#define DEVICE1_OUTPUT 12
#define DEVICE2_OUTPUT 14
#define DEVICE3_OUTPUT 27

#define TIME_DELAY 1000
#define TIME_READY 10000

// Define Firebase object
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

bool signupOK = false;

// Database child nodes
String monitorPath = "/monitor";
String controllerPath = "/controller";

// Database monitor child nodes
String IrmsPath = "/Irms";
String VrmsPath = "/Vrms";
String apparentPowerPath = "/apparentPower";
String realPowerPath = "/realPower";
String powerFactorPath = "/powerFactor";
String WhPath = "/Wh";
String kWhPath = "/kWh";

FirebaseJson monitorJson;

EnergyMonitor emon1;                       // Create an instance
unsigned long long lastMillis = millis();
double Irms = 0;
double Vrms = 0;
double apparentPower = 0;
double realPower = 0;
double powerFactor = 0;
double Wh = 0;
double kWh = 0;

int device1Control = 0;
int device2Control = 0;
int device3Control =  0;

void initWiFi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.println();
    Serial.print("Connecting to WiFi ..");
    while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
    }
    Serial.println();
    Serial.print("Local IP: ");
    Serial.println(WiFi.localIP());
    Serial.println();
}

void initFirebase() {
    // Assign the api key (required)
    config.api_key = API_KEY;

    // Assign the RTDB URL (required)
    config.database_url = DATABASE_URL;

    Firebase.reconnectWiFi(true);
    fbdo.setResponseSize(8192);

    // Assign the callback function for the long running token generation task */
    config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h

    if (Firebase.signUp(&config, &auth, "", "")){
        Serial.println("Firebase signUp ok");
        signupOK = true;
    }
    else{
        Serial.printf("%s\n", config.signer.signupError.message.c_str());
    }

    // Assign the maximum retry of token generation
    config.max_token_generation_retry = 5;

    // Initialize the library with the Firebase authen and config
    Firebase.begin(&config, &auth);
}

void FirebaseSetMonitor() {
    monitorJson.set(IrmsPath.c_str(), Irms);
    monitorJson.set(VrmsPath.c_str(), Vrms);
    monitorJson.set(apparentPowerPath.c_str(), apparentPower);
    monitorJson.set(realPowerPath.c_str(), realPower);
    monitorJson.set(powerFactorPath.c_str(), powerFactor);
    monitorJson.set(WhPath.c_str(), Wh);
    monitorJson.set(kWhPath.c_str(), kWh);
    Serial.printf("Set json... %s\n\n", Firebase.RTDB.setJSON(&fbdo, monitorPath.c_str(), &monitorJson) ? "ok" : fbdo.errorReason().c_str());
}

void FirebaseGetController() {  
    if (!Firebase.ready() || !signupOK) return;

    String temp = "";

    if (Firebase.RTDB.getString(&fbdo, F("/controller/device1"), &temp)) {
        device1Control = temp.toInt();
    }

    if (Firebase.RTDB.getString(&fbdo, F("/controller/device2"), &temp)) {
        device2Control = temp.toInt();
    }

    if (Firebase.RTDB.getString(&fbdo, F("/controller/device3"), &temp)) {
        device3Control = temp.toInt();
    }
}

void setup() {
    Serial.begin(115200);

    initWiFi();

    initFirebase();

    pinMode(CURRENT_ADC_INPUT, INPUT);
    pinMode(VOLTAGE_ADC_INPUT, INPUT);
    analogSetPinAttenuation(CURRENT_ADC_INPUT, ADC_11db);
    analogSetPinAttenuation(VOLTAGE_ADC_INPUT, ADC_11db);
    analogReadResolution(ADC_BITS);
    emon1.current(CURRENT_ADC_INPUT, CURRENT_CALIBRATION);           // Current: input pin, calibration.
    emon1.voltage(VOLTAGE_ADC_INPUT, VOLTAGE_CALIBRATION, PHASE_CALIBRATION);     // Voltage: input pin, calibration, phase_shift

    pinMode(DEVICE1_OUTPUT, OUTPUT);
    pinMode(DEVICE2_OUTPUT, OUTPUT);
    pinMode(DEVICE3_OUTPUT, OUTPUT);
    digitalWrite(DEVICE1_OUTPUT, LOW);
    digitalWrite(DEVICE2_OUTPUT, LOW);
    digitalWrite(DEVICE3_OUTPUT, LOW);

    delay(TIME_READY);
}

void loop() {
    // Get data from firsebase to control divices
    FirebaseGetController();

    // Control device
    if (!device1Control) {
        digitalWrite(DEVICE1_OUTPUT, LOW);
    }
    else {
        digitalWrite(DEVICE1_OUTPUT, HIGH);
    }

    if (!device2Control) {
        digitalWrite(DEVICE2_OUTPUT, LOW);
    }
    else {
        digitalWrite(DEVICE2_OUTPUT, HIGH);
    }

    if (!device3Control) {
        digitalWrite(DEVICE3_OUTPUT, LOW);
    }
    else {
        digitalWrite(DEVICE3_OUTPUT, HIGH);
    }

    if (Firebase.ready() && signupOK && (millis() - lastMillis > TIME_DELAY)) {
        String temp = "";
        Serial.print("Get kWh... ");
        if (Firebase.RTDB.getString(&fbdo, F("/monitor/kWh"), &temp)) {
            Serial.print(fbdo.to<const char *>());
            kWh = temp.toDouble();
        }
        else {
            Serial.print(fbdo.errorReason().c_str());
        }
        Serial.println();

        double temp1 = 0;
        Serial.print("Get Wh... ");
        if (Firebase.RTDB.getDouble(&fbdo, F("/monitor/Wh"), &temp1)) {
            Serial.print(String(fbdo.to<double>()).c_str());
            Wh = temp1;
        }
        else {
            Serial.print(fbdo.errorReason().c_str());
        }
        Serial.println();

        emon1.calcVI(20,2000);         // Calculate all. No.of half wavelengths (crossings), time-out
        Irms = round(emon1.Irms * 10000.0) / 10000.0;
        Vrms = round(emon1.Vrms * 100.0) / 100.0;
        apparentPower = round(emon1.apparentPower * 100.0) / 100.0;
        realPower = round(emon1.realPower * 100.0) / 100.0;
        powerFactor = round(emon1.powerFactor * 100.0) / 100.0;

        Wh += emon1.realPower * 1.0 / 3600;
        Wh = round(Wh * 10000.0) / 10000.0;

        if (Wh >= 1) {
            kWh += Wh * 0.001;
            kWh = round(kWh * 1000.0) / 1000.0;
            Wh = 0;
        }

        // Print to serial monitor
        Serial.printf("Vrms = %.2f(V)\tIrms = %.4f(A)\n", Vrms, Irms);
        Serial.printf("Apparent Power: %.2f(VA)\t Real Power: %.2f(W)\n", apparentPower, realPower);
        Serial.printf("Power Factor: %.2f\tWh: %.4f\n", powerFactor, Wh);

        // Update data to firebase
        FirebaseSetMonitor();

        // Update current millis
        lastMillis = millis();
    }
}