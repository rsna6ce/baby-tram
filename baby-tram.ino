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
const int pwm_ch_f = 1;
const int pwm_freq = 30;
const int pwm_bit = 8;
const int pwm_max = (1 << pwm_bit);

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
    digitalWrite(pin_f, LOW);
    // setup pwm
    ledcSetup(pwm_ch_f, pwm_freq, pwm_bit);
    ledcAttachPin(pin_f, pwm_ch_f);
    ledcWrite(pwm_ch_f, 0);

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
    server.on("/api",handleApi);
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
static const int table_col_led = 0;
static const int table_col_brake = 1;
static const int table_col_power = 2;

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
            String led = jsondoc_temp["table"][i][table_col_led];
            Serial.println(led);
            String brake = jsondoc_temp["table"][i][table_col_brake];
            Serial.println(brake);
            String power = jsondoc_temp["table"][i][table_col_power];
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
            String led = jsondoc_table["table"][i][table_col_led];
            Serial.println(led);
            String brake = jsondoc_table["table"][i][table_col_brake];
            Serial.println(brake);
            String power = jsondoc_table["table"][i][table_col_power];
            Serial.println(power);
        }
        */
        status_section_id = 0;
        status_section_count = jsondoc_table["table"].size();
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

void handleApi() {
    digitalWrite(pinLED, HIGH);
    String item_str = server.arg("item");
    String value_str = server.arg("value");
    int value = value_str.toInt();
    String res = "{\"result\":\"ERROR\"}";

    if (item_str == "run") {
        status_running = value;
        String status_running_str = status_running!=0 ? "RUN" : "STOP";
        res = "OK.";
        res = "{\"result\":\"OK\", \"item\":\"run\", \"value\":\"" + status_running_str + "\"}";
    } else if (item_str == "power"){
        int temp_value = status_power + value;
        temp_value = max(0, temp_value);
        temp_value = min(temp_value, 100);
        status_power = temp_value;
        res = "{\"result\":\"OK\", \"item\":\"power\", \"value\":" + String(status_power) + "}";
    }
    //Serial.println(res);
    server.send(200, "application/json", res);
    digitalWrite(pinLED, LOW);
}

void handleNotFound() {
  digitalWrite(pinLED, HIGH);
  server.send(404, "text/plain", "404 page not found.");
  digitalWrite(pinLED, LOW);
}

static int current_power = 0;
void loop2(void * params) {
    while (true) {
        int target_power = status_power * status_running;
        if (current_power == target_power) {
            // do nothing
        } else if (target_power==0){
            current_power = 0;
            ledcWrite(pwm_ch_f, current_power);
        } else if (current_power < target_power) {
            current_power++;
            Serial.print("current_power");
            Serial.println(current_power);
            ledcWrite(pwm_ch_f, (pwm_max * current_power) / 100);
        } else if (target_power < current_power) {
            current_power--;
            Serial.print("current_power");
            Serial.println(current_power);
            ledcWrite(pwm_ch_f, (pwm_max * current_power) / 100);
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