#include "esp_system.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "wps_example.h"
#include "esp_wps.h"

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
    Serial.println("hello. baby  trum.");

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

    server.on("/", handleRoot);
    server.on("/api",handleApi);
    server.onNotFound(handleNotFound);
    server.begin();

    xTaskCreatePinnedToCore(loop2, "loop2", 4096, NULL, 1, NULL, 0);
}

static int status_running = 0;
static int status_power = 0;
void handleRoot() {
    digitalWrite(pinLED, HIGH);
    String status_running_str = status_running!=0 ? "RUN" : "STOP";
    String status_power_str = String(status_power);
    String index_html =
        "  <!DOCTYPE html>\n"
        "  <html lang='en'>\n"
        "  <head>\n"
        "    <meta charset='utf-8'>\n"
        "    <title>baby-trum</title>\n"
        "    <style>\n"
        "      .center{ width:480px; margin: 0 auto; }\n"
        "      progress{ width:125px; height:30px;}\n"
        "      button {width:60px; height:30px;\n"
        "      border:none; color:#FFF;\n"
        "      background:#0d6efd; border-radius:5px;}\n"
        "      p{height:2em; margin-block-start:0px; margin-block-end:0px;}\n"
        "    </style>\n"
        "  </head>\n"
        "  <body>\n"
        "    <div class='center'>\n"
        "      <h2>baby trum</h2>\n"
        "      <p><nobr>\n"
        "        <button type='button' onclick=set_value('run',1)> <b>RUN</b> </button>\n"
        "        <button type='button' onclick=set_value('run',0)> <b>STOP</b> </button>\n"
        "      </nobr></p>\n"
        "      <b><font size='5'><a id='run'> " + status_running_str + " </a></font></b>\n"
        "      <hr>\n"
        "      <p><nobr>\n"
        "        <button type='button' onclick=set_value('power',-5)> <b>－</b> </button>\n"
        "        <button type='button' onclick=set_value('power',5)>  <b>＋</b> </button>\n"
        "      </nobr></p>\n"
        "      <b><font size='5'><a id='power_value'> POW:" + status_power_str + "% </a></font></b>\n"
        "      <p>\n"
        "        <progress id='power_progress' value='" + status_power_str + "' min='0' max='100'>0</progress>\n"
        "      </p>\n"
        "  </div>\n"
        "  </body>\n"
        "  <script language='javascript' type='text/javascript'>\n"
        "  function set_value(item, value) {\n"
        "    var request = new XMLHttpRequest();\n"
        "    request.open('GET', '/api?item=' + item + '&value=' + value , true);\n"
        "    request.responseType = 'json';\n"
        "    request.onload = function () {\n"
        "            let data = this.response;\n"
        "            let result = data.result;\n"
        "            let item = data.item;\n"
        "            let value = data.value;\n"
        "            if (result!='OK') {\n"
        "                alert('ERROR');\n"
        "            } else if (item=='run') {\n"
        "                document.getElementById('run').textContent = value;\n"
        "            } else if (item=='power') {\n"
        "                document.getElementById('power_progress').value = value;\n"
        "                document.getElementById('power_value').textContent = 'POW:' + value + '%';\n"
        "            }\n"
        "        };\n"
        "    request.onerror = function () {\n"
        "      alert('ERROR');\n"
        "    };\n"
        "    request.send();\n"
        "  }\n"
        "  </script>\n";
    server.send(200, "text/HTML", index_html);
    digitalWrite(pinLED, LOW);
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
        delay(10);
    }
}

void loop() {
    if (wifi_status==WL_CONNECTED) {
        server.handleClient();
    }
    delay(10);
}