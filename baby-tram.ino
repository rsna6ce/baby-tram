#include "esp_system.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "wps_example.h"
#include "esp_wps.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
#include <stdint.h>

// Arduino debug print
#define TOSTR(x) #x
#define TOSTRX(x) TOSTR(x)
#define DEBUG_PRINT_VARIABLE(x) Serial.print(TOSTRX(x));Serial.print(":");Serial.println(x);

//static ssid and pass (if not use set "")
static char* ssid = "";
static char* password = "";

const int ssid_pass_buff_len = 64;
char ssid_buff[ssid_pass_buff_len]={};
char pass_buff[ssid_pass_buff_len]={};

static int wifi_status = WL_DISCONNECTED;

const int pinSW = 0;
const int pinLED = 2;
const int pin_f = 26;
const int pin_r = 25;
const int pwm_freq = 30;
const int pwm_bit = 8;
const int pwm_max = (1 << pwm_bit);

const int pinLedR = 16;
const int pinLedG = 18;
const int pinLedB = 27;
const int pinLedY = 33;
const int pinLedP = 21;
const int DRAIN_LED_ON = 0;
const int DRAIN_LED_OFF = 1;
const int pinMagL = 36;
const int pinMagR = 39;
const int threshold_n = 2050;
const int threshold_s = 1800;

WebServer server(80);
String current_ipaddr = "";

#define WIFI_TIMEOUT_SEC 10
#define WPS_TIMEOUT 120

StaticJsonDocument<1024> jsondoc_temp;  //for decode
StaticJsonDocument<1024> jsondoc_table; //for table

// Serial.readStringUntil do now work in ESP32...
String SerialReasStringUntilCRLF() {
    Serial.setTimeout(100);
    String ret = "";
    String temp_str = "";
    while (true) {
        if (Serial.available() > 0) {
            temp_str = Serial.readString();
            if (temp_str.endsWith("\n") || temp_str.endsWith("\r")) {
                temp_str.replace("\n", "");
                temp_str.replace("\r", "");
                ret += temp_str;
                break;
            } else {
                ret += temp_str;
            }
        }
    }
    return ret;
}

void input_ssid_pass(char* ssid, char* pass) {
    while(true) {
        Serial.println("input SSID and press Enter.");
        String temp_ssid = SerialReasStringUntilCRLF();

        Serial.println("input password and press Enter.");
        String temp_pass = SerialReasStringUntilCRLF();

        Serial.println("SSID: " + temp_ssid + "\r\n" +
                       "pass: " + temp_pass + "\r\n" +
                       "OK? input yes or no and Enter key.");
        String temp_ret = SerialReasStringUntilCRLF();
        if (temp_ret == "yes") {
            temp_ssid.toCharArray(ssid, ssid_pass_buff_len);
            temp_pass.toCharArray(pass, ssid_pass_buff_len);
            break;
        }
    }
    return;
}

void setup()
{
    Serial.begin(115200);
    Serial.println("hello. baby  tram.");

    // setup pins
    pinMode(pinSW, INPUT_PULLUP);
    pinMode(pinLED, OUTPUT);
    pinMode(pin_f, OUTPUT);
    pinMode(pin_r, OUTPUT);
    digitalWrite(pin_f, LOW);
    digitalWrite(pin_r, LOW);
    pinMode(pinLedR, OUTPUT_OPEN_DRAIN);
    pinMode(pinLedG, OUTPUT_OPEN_DRAIN);
    pinMode(pinLedB, OUTPUT_OPEN_DRAIN);
    pinMode(pinLedY, OUTPUT_OPEN_DRAIN);
    pinMode(pinLedP, OUTPUT_OPEN_DRAIN);
    digitalWrite(pinLedR, DRAIN_LED_OFF);
    digitalWrite(pinLedG, DRAIN_LED_OFF);
    digitalWrite(pinLedB, DRAIN_LED_OFF);
    digitalWrite(pinLedY, DRAIN_LED_OFF);
    digitalWrite(pinLedP, DRAIN_LED_OFF);
    pinMode(pinMagL, ANALOG);
    pinMode(pinMagR, ANALOG);
    // setup pwm
    ledcAttach(pin_f, pwm_freq, pwm_bit);
    ledcAttach(pin_r, pwm_freq, pwm_bit);
    ledcWrite(pin_f, 0);
    ledcWrite(pin_r, 0);

    // wifi setting
    Serial.println("To specify the SSID, press the y key within 3 seconds.");
    delay(3*1000);
    if (Serial.available() > 0) {
        int inmyte = Serial.read();
        if (inmyte == 'y') {
            String frush_str = Serial.readString(); // flush buffer
            input_ssid_pass(ssid_buff, pass_buff);
            ssid = ssid_buff;
            password = pass_buff;
        }
    }

    // try Wifi connection
    Serial.println("WiFi.begin");
    Serial.println("WIFI....");
    if (strlen(ssid)>0 && strlen(password)>0) {
        // specific ssid
        WiFi.begin(ssid, password);
    } else {
        // ssid last connected
        WiFi.begin();
    }
    int led_blink=0;
    for (int i=0; (i<WIFI_TIMEOUT_SEC*2)&&(wifi_status!= WL_CONNECTED); i++) {
        Serial.println(".");
        wifi_status = WiFi.status();
        digitalWrite(pinLED, led_blink);
        led_blink ^= 1;
        delay(500);
    }
    digitalWrite(pinLED, LOW);

    if (wifi_status == WL_CONNECTED) {
        Serial.println("");
        Serial.print("Connected to ");
        Serial.println(WiFi.SSID());
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        Serial.println("SUCCESS.");
        delay(1*1000);
    } else {
        // try WPS connection
        Serial.println("Failed to connect");
        Serial.println("Starting WPS");
        Serial.println("WPS.....");
        WiFi.disconnect();
        WiFi.onEvent(WiFiEvent);
        WiFi.mode(WIFI_MODE_STA);
        wpsInitConfig();
        wpsStart();
        for (int i=0; (i<WPS_TIMEOUT)&&(!wps_success); i++) {
            Serial.println(".");
            digitalWrite(pinLED, led_blink);
            led_blink ^= 1;
            // wps_success is updated in callback
            delay(1000);
        }
        digitalWrite(pinLED, LOW);
        if (!wps_success) {
            Serial.println("FAILED..");
        } else {
            Serial.println("SUCCESS.");
        }
        delay(1*1000);
        Serial.println("RESTART.");
        delay(1*1000);
        esp_restart();
    }

    SPIFFS.begin(true);

    server.on("/", handleRoot);
    server.on("/api/run/start", handleApiRunStart);
    server.on("/api/run/stop", handleApiRunStop);
    server.on("/api/table/load", handleApiTableLoad);
    server.on("/api/table/save", handleApiTableSave);
    server.onNotFound(handleNotFound);
    server.begin();

    xTaskCreatePinnedToCore(loop2, "loop2", 4096, NULL, 1, NULL, 0);
}

static uint32_t status_running = 0;
static uint32_t status_power = 0;
static uint32_t status_section_id = 0;
static uint32_t status_section_count = 0;
static uint32_t status_section_elapsedtime = 0;
void handleRoot() {
    digitalWrite(pinLED, HIGH);
    String status_running_str = status_running!=0 ? "RUN" : "STOP";
    String index_html =
#include "html/template_root.html.h"
;

    server.send(200, "text/HTML", index_html);
    digitalWrite(pinLED, LOW);
}
void handleApiTableLoad() {
    if (server.hasArg("plain")) {
        String body = server.arg("plain");
        DeserializationError error = deserializeJson(jsondoc_temp, body);
        String filenumber = jsondoc_temp["filenumber"];

        String file_name = "/" + filenumber + ".json";
        if (SPIFFS.exists(file_name)){
            File fp = SPIFFS.open(file_name,"r");
            if (fp) {
                String json_str = fp.readString();
                fp.close();
                Serial.println("read file sccess.");
                Serial.println(json_str);
                
                DeserializationError error = deserializeJson(jsondoc_temp, json_str);
                jsondoc_temp["result"] = "OK";
                String response;
                serializeJson(jsondoc_temp, response);
                server.send(200, "application/json", response);
            } else {
                Serial.println("INFO:read file failed. create new.");
                server.send(200, "application/json", "{\"result\":\"OK\", \"description\":\"\", \"table\":[]}");
            }
        } else {
            Serial.println("INFO:file not found. create new.");
            server.send(200, "application/json", "{\"result\":\"OK\", \"description\":\"\", \"table\":[]}");
        }
    } else {
        Serial.println("ERROR: server.hasArg(\"plain\") = false");
        server.send(200, "application/json", "{\"result\":\"ERROR\"}");
    }
}
static const int TABLE_COL_LED = 0;
static const int TABLE_COL_BRAKE = 1;
static const int TABLE_COL_POWER = 2;

void handleApiTableSave() {
    if (server.hasArg("plain")) {
        String body = server.arg("plain");
        Serial.println(body);
        DeserializationError error = deserializeJson(jsondoc_temp, body);
        String filenumber = jsondoc_temp["filenumber"];
        String description = jsondoc_temp["description"];
        Serial.println(filenumber);
        Serial.println(description);
        for (int i=0; i< jsondoc_temp["table"].size(); i++) {
            String led = jsondoc_temp["table"][i][TABLE_COL_LED];
            Serial.println(led);
            String brake = jsondoc_temp["table"][i][TABLE_COL_BRAKE];
            Serial.println(brake);
            String power = jsondoc_temp["table"][i][TABLE_COL_POWER];
            Serial.println(power);
        }
        // write to spiffs file
        File fp = SPIFFS.open("/" + filenumber + ".json","w");
        if (fp) {
            fp.print(body + "\n");
            fp.close();
            Serial.println("write file sccess.");
        } else {
            Serial.println("ERROR: write_highscore failed.");
            server.send(200, "application/json", "{\"result\":\"ERROR\"}");
        }
        server.send(200, "application/json", "{\"result\":\"OK\"}");
    } else {
        Serial.println("ERROR: server.hasArg(\"plain\") = false");
        server.send(200, "application/json", "{\"result\":\"ERROR\"}");
    }
}

void handleApiRunStart() {
    if (server.hasArg("plain")) {
        String body = server.arg("plain");
        Serial.println(body);
        DeserializationError error = deserializeJson(jsondoc_table, body);
        /*
        String filenumber = jsondoc_table["filenumber"];
        String description = jsondoc_table["description"];
        Serial.println(filenumber);
        Serial.println(description);
        for (int i=0; i< jsondoc_table["table"].size(); i++) {
            String led = jsondoc_table["table"][i][TABLE_COL_LED];
            Serial.println(led);
            String brake = jsondoc_table["table"][i][TABLE_COL_BRAKE];
            Serial.println(brake);
            String power = jsondoc_table["table"][i][TABLE_COL_POWER];
            Serial.println(power);
        }
        */
        status_section_id = 0;
        status_section_count = jsondoc_table["table"].size();
        set_section(status_section_id);
        status_running = 1;
        server.send(200, "application/json", "{\"result\":\"OK\"}");
    } else {
        Serial.println("ERROR: server.hasArg(\"plain\") = false");
        server.send(200, "application/json", "{\"result\":\"ERROR\"}");
    }
}
void handleApiRunStop() {
    status_running = 0;
    String res = "{\"result\":\"OK\"}";
    server.send(200, "application/json", res);
}

void handleNotFound() {
  digitalWrite(pinLED, HIGH);
  server.send(404, "text/plain", "404 page not found.");
  digitalWrite(pinLED, LOW);
}

static int target_power = 0;
static int current_power = 0;
static int brake_time = 0;
void set_section(int section_id) {
    // led
    String led_str = jsondoc_table["table"][section_id][TABLE_COL_LED];
    digitalWrite(pinLedR, !(led_str=="r" || led_str == "R")/*OPEN_DRAIN*/ );
    digitalWrite(pinLedG, !(led_str=="g" || led_str == "G")/*OPEN_DRAIN*/ );
    digitalWrite(pinLedB, !(led_str=="b" || led_str == "B")/*OPEN_DRAIN*/ );
    digitalWrite(pinLedY, !(led_str=="y" || led_str == "Y")/*OPEN_DRAIN*/ );
    // power
    String power_str = jsondoc_table["table"][section_id][TABLE_COL_POWER];
    target_power = min( max((int)power_str.toInt(),0), 100); //%
    // brake
    String brake_str = jsondoc_table["table"][section_id][TABLE_COL_BRAKE];
    brake_time = brake_str.toInt();
    // debug print
    Serial.println("section change!!");
    DEBUG_PRINT_VARIABLE(section_id);
    DEBUG_PRINT_VARIABLE(led_str);
    DEBUG_PRINT_VARIABLE(target_power);
    DEBUG_PRINT_VARIABLE(brake_time);
}

static bool marker_detected_prev = false;
static uint32_t latest_section_change_millis = 0;
static const uint32_t section_change_threshold = 100;
void loop2(void * params) {
    while (true) {
        if (status_running==0) {
            current_power = 0;
            ledcWrite(pin_f, 0);
            ledcWrite(pin_r, 0);
            delay(10);
            continue;
        }
        // any marker detected
        int mgl = analogRead(pinMagL);
        int mgr = analogRead(pinMagR);
        // poleN only
        bool marker_detected_curr = (threshold_n < mgl) | (threshold_n < mgr);
        uint32_t curr_millis = millis();
        if (marker_detected_prev != marker_detected_curr) {
            marker_detected_prev = marker_detected_curr;
            // section change filter timer
            uint32_t past_millis = curr_millis - latest_section_change_millis;
            if (marker_detected_curr && (section_change_threshold < past_millis)) {
                // section change
                //DEBUG_PRINT_VARIABLE(mgl);
                //DEBUG_PRINT_VARIABLE(mgr);
                latest_section_change_millis = curr_millis;
                status_section_id = (status_section_id + 1) % status_section_count;
                set_section(status_section_id);
            }
        }

        if (curr_millis - latest_section_change_millis < brake_time) {
            // brake on
            ledcWrite(pin_f, pwm_max);
            ledcWrite(pin_r, pwm_max);
            digitalWrite(pinLedP, DRAIN_LED_ON);
        } else {
            // brake off
            ledcWrite(pin_r, 0);
            digitalWrite(pinLedP, DRAIN_LED_OFF);

            // drive
            if (current_power == target_power) {
            // do nothing
            } else if (target_power==0){
                current_power = 0;
                ledcWrite(pin_f, 0);
            } else if (current_power < target_power) {
                current_power = target_power; //direct acceleration 
                //current_power++;
                //DEBUG_PRINT_VARIABLE(current_power);
                ledcWrite(pin_f, (pwm_max * current_power) / 100);
            } else if (target_power < current_power) {
                current_power = target_power; //direct deceleration
                //current_power--;
                //DEBUG_PRINT_VARIABLE(current_power);
                ledcWrite(pin_f, (pwm_max * current_power) / 100);
            }
        }
        delay(1);
    }
}

void loop() {
    if (wifi_status==WL_CONNECTED) {
        server.handleClient();
    }
    delay(10);
}