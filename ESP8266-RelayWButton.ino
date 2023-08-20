#define VERSION "0.0.5"

#define TELEGRAM

#ifdef TELEGRAM
#define SKETCH_VERSION VERSION " Tg"
#else
#define SKETCH_VERSION VERSION
#endif

#define WEB_PASSWORD "<Your Password to exec commands on esp8266>"
#define FIRMWARE_FOLDER "<URL to folder with firmwares>"
#define FIRMWARE_FILE "esp_relaywb_last.bin"

#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#include <ESP8266WiFiMulti.h>

#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <Regexp.h>

#define TB_SERVER  "<ThingsBoard IP>:8081"
int tb_interval = 60000;
char tb_token[21] = "";

#include <Preferences.h>
Preferences preferences;

bool do_restart = false;
bool do_update = false;
String fwfn = "";

bool tgavail = false;
char token[50];

int64_t userid = <Your Telegram User ID>;

#ifdef TELEGRAM
#include <AsyncTelegram2.h>

// Timezone definition
#include <time.h>
#define MYTZ "CET-1CEST,M3.5.0,M10.5.0/3"

BearSSL::WiFiClientSecure clientsec;
BearSSL::Session session;
BearSSL::X509List certificate(telegram_cert);

AsyncTelegram2 myBot(clientsec);
#endif

ESP8266WiFiMulti wifiMulti;

const int numssisds = 3;
const char* ssids[numssisds] = { "<Access Point 1>", "<Access Point 2>", "<Access Point 3>" };
const char* pass = "<WiFI Password>";
const int numpcs = 2;
// ip, name, ThingsBoard, Telegram
const char* pcs[numpcs][4] = {
  { "<IP of device1>", "<Name of device1>", "<ThingsBoard Tag1>", "<Telegram Tag1>"},
  { "<IP of device2>", "<Name of device2>", "<ThingsBoard Tag2>", "<Telegram Tag2>"},
};
int iListIndex = -2;

String ssid;
String device, deviceip;

ESP8266WebServer server(80);

const int led_pin = -1; //LED_BUILTIN;
const int relay_pin = 0;  // using GP0 esp8266
const int button_pin = 2;   // using GP2 esp8266

String top;

const String bot = "\
    </div>\
  </body>\
</html>";

String getFormRoot;
String getFormUpdate;

#define NEED_REPEAT_NUM 10
bool relay_closed = true;
bool reverse_logic = true;
int button_test_count = 0;

char tb_url[128] = "";
unsigned long tb_prevMs = 0;
unsigned long tb_curMs = 0;

void tb_send(char* buf){
  WiFiClient client;
  HTTPClient http;

  if (strlen(tb_url) == 0)
    sprintf(tb_url, "http://" TB_SERVER "/api/v1/%s/telemetry", tb_token);     
  http.begin(client, tb_url);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(buf);
  if (httpCode != 200) {
      Serial.println(tb_url);
      Serial.println(buf);
      Serial.print("HTTP Response code is: ");
      Serial.println(httpCode);
  }
  http.end();
}

byte rx_pin_state = 1;
byte rx_pin_check_count = 0;
unsigned int rx_count = 0;
unsigned long rx_prev_ms = 0;
unsigned int rx_mins_on = 0;

void handleThingsBoard(bool forced = false) {
  if ((forced || tb_interval) && strlen(tb_token)) {            
    tb_curMs = millis();
    if (forced || (tb_curMs - tb_prevMs >= tb_interval)) {
      tb_prevMs = tb_curMs;

      char buf[128];
      sprintf(buf, "{\"relay_closed\": %d, \"rx_pin_state\": %d}", relay_closed, rx_pin_state);
      tb_send(buf);
    }  
  }
}


void httpGet(String url){
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;

    http.begin(client, url);
    http.addHeader("Content-Type", "text/html");

    int httpCode = http.GET();

    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        Serial.println("Relay Command Sent");
      }
    } else {
      Serial.println("Unable to Send Relay Command");
    }
    http.end();
  }
}

bool pressed = false;

void test_button() {
  pinMode(button_pin, INPUT);
  if (!digitalRead(button_pin)) {
    if (!pressed) {
      button_test_count++;
      Serial.println(button_test_count);
    }
    delay(100);
    if (button_test_count > NEED_REPEAT_NUM) {
      pressed = true;
      button_test_count = 0;
      relay(!relay_closed);
      handleThingsBoard(true);
    }
  } else {
    pressed = false;
    button_test_count = 0;
  }
}

void relay(bool on) {
  String s = "Relay ";
  if (on)
    s += "CLOSED";
  else
    s += "opened";
  tgOwnerSend(s);
  relay_closed = on;
  preferences.putInt("relay_closed", relay_closed);
  if (on) {
    button_test_count = 0; 
    if (reverse_logic)
      digitalWrite(relay_pin, 0);
    else
      digitalWrite(relay_pin, 1);
  } else {
    if (reverse_logic)
      digitalWrite(relay_pin, 1);
    else
      digitalWrite(relay_pin, 0);
  }
  handleThingsBoard(true);
}

void led(int val){
  if (led_pin >= 0) {
    pinMode(led_pin, OUTPUT);
    digitalWrite(led_pin, val);
    pinMode(led_pin, INPUT);
  }
}

void handleRoot() {
  led(1);
  initgetFormRoot();
  server.send(200, "text/html", top + getFormRoot + bot);
  led(0);
}

void handleUpdForm() {
  led(1);
  initgetFormUpdate();
  server.send(200, "text/html", top + getFormUpdate + bot);
  led(0);
}

void update_started() {
  Serial.println("CALLBACK:  HTTP update process started");
}

void update_finished() {
  Serial.println("CALLBACK:  HTTP update process finished");
}

void update_progress(int cur, int total) {
  Serial.printf("CALLBACK:  HTTP update process at %d of %d bytes...\n", cur, total);
}

void update_error(int err) {
  Serial.printf("CALLBACK:  HTTP update fatal error code %d\n", err);
}

void (*restartFunc)(void) = 0;  //declare restart function @ address 0

void tgOwnerSend(String s) {
#ifdef TELEGRAM
  if (WiFi.status() == WL_CONNECTED && tgavail) {
    String message;
    message += "@";
    message += myBot.getBotName();
    message += " (";
    message += device;
    message += "):\n";
    message += s;
    Serial.println(message);
    myBot.sendTo(userid, s);
  }
#endif
}

void doRestart() {
  do_restart = false;
  if (WiFi.status() == WL_CONNECTED) {
    tgOwnerSend("Restarting...");
#ifdef TELEGRAM
    // to avoid restart looping
    while (WiFi.status() == WL_CONNECTED && !myBot.noNewMessage()) {
      Serial.print(".");
      delay(100);
    }
#endif
#endif
    Serial.println("Restart in 5 seconds...");
    delay(5000);
  }
  ESP.restart();
}

void doUpdate() {
  do_update = false;
#ifdef TELEGRAM
  if (WiFi.status() == WL_CONNECTED) {
    tgOwnerSend("Updating...");
    // Wait until bot synced with telegram to prevent cyclic reboot
    while (WiFi.status() == WL_CONNECTED && !myBot.noNewMessage()) {
      Serial.print(".");
      delay(100);
    }
    Serial.println("Update in 5 seconds...");
    delay(5000);
  }
#endif
  update(fwfn);
}

void updateOtherDevice(String devip, String firmware) {
  if (WiFi.status() == WL_CONNECTED) {

    WiFiClient client;
    HTTPClient http;

    String url = "http://" + devip + "/remote/" + "?pswupd=" + WEB_PASSWORD + "&firmware=" + firmware;
    http.begin(client, url);  //HTTP
    http.addHeader("Content-Type", "text/html");

    int httpCode = http.GET();

    if (httpCode > 0) {
      // file found at server
      if (httpCode == HTTP_CODE_OK) {
        const String& payload = http.getString();
        Serial.println(payload);
      }
    } else {
      Serial.printf("UPDATE %s failed, error: %s\n",
                    devip,
                    http.errorToString(httpCode).c_str());
    }

    http.end();
  }
}

void updateOthers(String firmware) {
  for (int i = 0; i < numpcs; i++) {
    if (i != iListIndex) {
      Serial.println(deviceip + " is sending update request to " + pcs[i][0]);
      updateOtherDevice(pcs[i][0], firmware);
    }
  }
};

void update(String firmware) {
  do_update = false;
  if (WiFi.status() == WL_CONNECTED) {

    Serial.print("Update with ");
    Serial.print(firmware);
    Serial.println("...");

    WiFiClient client;

    ESPhttpUpdate.onStart(update_started);
    ESPhttpUpdate.onEnd(update_finished);
    ESPhttpUpdate.onProgress(update_progress);
    ESPhttpUpdate.onError(update_error);

    tgOwnerSend(firmware);

    t_httpUpdate_return ret = ESPhttpUpdate.update(client, FIRMWARE_FOLDER + firmware);

    switch (ret) {
      case HTTP_UPDATE_FAILED:
        tgOwnerSend(ESPhttpUpdate.getLastErrorString().c_str());
        Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        do_restart = true;
        break;

      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        do_restart = true;
        break;

      case HTTP_UPDATE_OK:
        tgOwnerSend("HTTP_UPDATE_OK");
        Serial.println("HTTP_UPDATE_OK");
        break;
    }
  } else {
    do_restart = true;
  }
}

void handleRemote() {
  led(1);
  server.send(200, "text/plain", "- OK -");
  if (server.arg("pswupd").equals(WEB_PASSWORD)) {
    update(server.arg("firmware"));
  };
  led(0);
}

void handleUpdate() {
  led(1);
  char buffer[40];
  String message = top;

  message += "<table><td>";

  bool pswcorrect = server.arg("pswupd").equals(WEB_PASSWORD);
  if (pswcorrect) {
    if (server.arg("firmware").equals("restart")) {
      message += "<p>Restarting...</p>";
    } else {

      if (server.arg("alldev").equals("yes")) {
        message += "<p>UPDATE ALL DEVICES</p>";
        updateOthers(server.arg("firmware"));
      } else {
        message += "<p>UPDATE STARTED</p>";
      }

      message += server.arg("firmware");
      message += "</td></table>";
      message += bot;
      server.send(200, "text/html", message);
      update(server.arg("firmware"));
    }

  } else {
    message += "<p style=\"background-color:Tomato;\">FAIL</p>";
  }

  message += "</td></table>";
  message += bot;
  server.send(200, "text/html", message);
  led(0);

  if (pswcorrect && server.arg(1).equals("restart")) {
    do_restart = true;
  }
}

void handleRelay() {
  led(1);
  char buffer[40];
  String message = top;

  message += "<table><td>";
  if (server.arg("password").equals(WEB_PASSWORD)) {
    message += "<p>SUCCESS</p>";
    bool b;

    String s = server.arg("reverse_logic");
    if (s.length()) {
      b = s.equals("1");
      if (reverse_logic != b) {
        if (b)
          tgOwnerSend("Reversed Logic");
        else
          tgOwnerSend("Standard Logic");
      }
      reverse_logic = b;
      preferences.putInt("reverse_logic", reverse_logic);
    }

    s = server.arg("relay_closed");
    if (s.length())  
      relay(s.equals("1"));
    else
      relay(!relay_closed);

  } else {
    message += "<p style=\"background-color:Tomato;\">FAIL</p>";
  }

  message += "</td></table>";
  message += bot;
  server.send(200, "text/html", message);
  led(0);
}

void handleNotFound() {
  led(1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  led(0);
}

// WiFi connect timeout per AP. Increase when connecting takes longer.
const uint32_t connectTimeoutMs = 5000;

// called for each match
void match_callback(const char* match,          // matching string (not null-terminated)
                    const unsigned int length,  // length of matching string
                    const MatchState& ms)       // MatchState in use (to get captures)
{
  char cap[100];  // must be large enough to hold captures

  for (byte i = 0; i < ms.level; i++) {
    ms.GetCapture(cap, i);
    if (strcmp(cap, "../")) {
      //Serial.println(cap);
      String s(cap);
      if (s.endsWith(".bin")) {
        getFormUpdate += "<option value=\"" + s + "\">" + s + "</option>";
      }
    }

  }  // end of for each capture

}  // end of match_callback

void initgetFormRoot(void) {
  String butLbl, color, hid, rl = "";

  if (relay_closed) {
    butLbl = "Open Relay";
    color = "red";
    hid = "0";
  } else {
    butLbl = "Close Relay";
    color = "gray";
    hid = "1";
  }

  if (reverse_logic)
    rl = "checked";

  getFormRoot = "<form method=\"get\" enctype=\"application/x-www-form-urlencoded\" action=\"/relay/\">\
      <table>\
      <tr><td><label for=\"password\">Password: </label></td>\
      <td><input type=\"password\" id=\"password\" name=\"password\" value=\"\" required></td></tr>\
      <tr><td><label for=\"reverse_logic\">Reverse Logic: </label></td>\
      <td><input type=\"checkbox\" id=\"reverse_logic\" name=\"reverse_logic\" value=\"1\" " + rl +"></td></tr>\
      <tr><td><span class=\"dot"
                  + color + "\"></span>\
              <input type=\"hidden\" id=\"relay_closed\" name=\"relay_closed\" value=\"" + hid + "\"></td>\
      <td align=\"right\"><input type=\"submit\" value=\""
                  + butLbl + "\"></td></tr>\
      </table></form>";
  getFormRoot += "<a href=\"/updform/\">Update Firmware</a>";
  for(int i = 0; i < numpcs; i ++) 
    if (i != iListIndex) {
      getFormRoot += " <a href=\"http://";
      getFormRoot += pcs[i][0];
      getFormRoot += "/\">";
      getFormRoot += pcs[i][1];
      getFormRoot += "</a>";
    }

}

void initgetFormUpdate(void) {
  getFormUpdate = "<form method=\"get\" enctype=\"application/x-www-form-urlencoded\" action=\"/update/\">\
      <table>\
      <tr><td><label for=\"pswupd\">Password: </label></td>\
      <td><input type=\"password\" id=\"pswupd\" name=\"pswupd\" value=\"\" required></td></tr>\
      <tr><td><label for=\"firmware\">Firmware: </label></td> \
      <td align=\"right\"><select id=\"firmware\" name=\"firmware\">\
      <option value=\"restart\">Restart</option>";

  WiFiClient client;
  HTTPClient http;

  if (http.begin(client, FIRMWARE_FOLDER)) {  // HTTP
    int httpCode = http.GET();

    // httpCode will be negative on error
    if (httpCode > 0) {
      // file found at server
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String payload = http.getString();
        //Serial.println(payload);

        unsigned long count;

        int payload_len = payload.length() + 1;
        char c_payload[payload_len];
        payload.toCharArray(c_payload, payload_len);

        // match state object
        MatchState ms(c_payload);

        count = ms.GlobalMatch("href=['\"]?([^'\" >]+)", match_callback);
      }
    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
  } else {
    Serial.printf("[HTTP} Unable to connect\n");
  }

  getFormUpdate += "</select></td></tr>\
    <tr><td><label for=\"alldev\">All devices: </label></td>\
    <td><input type=\"checkbox\" id=\"alldev\" name=\"alldev\" value=\"yes\"></td></tr> \
    <tr><td></td><td align=\"right\"><input type=\"submit\" value=\"Restart/Update\"></td></tr>\
    </table>\
    </form>\
    <a href=\"/\">Main page</a>";
}

void handleRxPin(void){
  if (digitalRead(3) != rx_pin_state) {
    rx_count++;
    if (rx_count > 1000) {
      unsigned long ms = millis();
      char buf[128];
      sprintf(buf, "{\"rx_pin_state\": %d}", rx_pin_state);
      tb_send(buf);
      rx_pin_state = !rx_pin_state; 
      sprintf(buf, "{\"rx_pin_state\": %d, \"rx_pin_sec%u\": %d}", rx_pin_state, !rx_pin_state, (ms - rx_prev_ms) / 1000);
      tb_send(buf);
      Serial.println(buf);
      rx_prev_ms = ms;
      rx_count = 0;
      if (rx_pin_state) rx_mins_on = 0;
    }
  } else {
    rx_count = 0;
  }
}

unsigned long prev_try_connect = 0;
const long connect_interval = 10000;

void try_connect() {
  unsigned long current_ms = millis();

  if (prev_try_connect == 0 || current_ms - prev_try_connect >= connect_interval) {
    prev_try_connect = current_ms;

    Serial.println("Connecting...");
    if (wifiMulti.run(connectTimeoutMs) == WL_CONNECTED) {
      ssid = WiFi.SSID();
      deviceip = WiFi.localIP().toString();
      Serial.println(String("Connected: ") + ssid + " - " + deviceip);

      if (iListIndex != -2) return;
      device = "Unknown";
      for (int i = 0; i < numpcs; i++) {
        if (deviceip.equals(pcs[i][0])) {
          iListIndex = i;
          // ip, name, ThingsBoard, Telegram
          device = pcs[i][1];
          strcpy(tb_token, pcs[i][2]);
          tgavail = strlen(pcs[i][3]) > 0;
          if (tgavail) {
            String(pcs[i][3]).toCharArray(token, strlen(pcs[i][3]) + 1);
          }
          break;
        }        
      }
      if (iListIndex == -2) iListIndex = -1;

      top = "<head>\
          <title>"
            + device + "</title>\
          <style>\
            body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
            table {\
              border: 1px solid #4CAF50;\
              border-radius: 5px;\
              padding: 20px;\
              margin-top: 10px;\
              margin-bottom: 10px;\
              margin-right: 10px;\
              margin-left: 30px;\
              background-color: lightblue;\
            }\
            .dotgray {\
              height: 20px;\
              width: 20px;\
              background-color: #bbb;\
              border-radius: 50%;\
              border: 1px solid #4CAF50;\
              display: inline-block;\
            }\
            .dotred {\
              height: 20px;\
              width: 20px;\
              background-color: red;\
              border-radius: 50%;\
              border: 1px solid #4CAF50;\
              display: inline-block;\
            }\
          </style>\
        </head>\
        <body>\
          <h1>"
            + device + "</h1>" + ssid + "<br>\
            <small>" SKETCH_VERSION "<br>";
      top += "</small><div>";

      server.on("/", handleRoot);
      server.on("/updform/", handleUpdForm);
      server.on("/update/", handleUpdate);
      server.on("/relay/", handleRelay);
      server.on("/remote/", handleRemote);
      server.onNotFound(handleNotFound);
      server.begin();

#ifdef TELEGRAM
      if (tgavail) {
        // Sync time with NTP, to check properly Telegram certificate
        configTime(MYTZ, "time.google.com", "time.windows.com", "pool.ntp.org");
        //Set certficate, session and some other base client properies
        clientsec.setSession(&session);
        clientsec.setTrustAnchors(&certificate);
        clientsec.setBufferSizes(1024, 1024);
        // Set the Telegram bot properies
        myBot.setUpdateTime(2000);
        myBot.setTelegramToken(token);

        // Check if all things are ok
        Serial.print("\nTest Telegram connection... ");
        myBot.begin() ? Serial.println("OK") : Serial.println("NOK");

        String s;
        if (relay_closed)
          s = "CLOSED";
        else
          s = "opened";

        tgOwnerSend(SKETCH_VERSION " Online " + deviceip + " via " + ssid + ". Relay " + s);
      }
#endif
    }
  }
}

String process_tg_message(char* msg_txt)
{
  char* command = strtok(msg_txt, " ");
  String s = "";
  if (strcmp(command, "relay") == 0) {
    s = strtok(NULL, " ");
    if (s == "")
      relay(!relay_closed);
    else
      relay(s == "1");
    s = "";
  } else if (strcmp(command, "reverse") == 0) {
    s = strtok(NULL, " ");
    if (s == "")
      reverse_logic = !reverse_logic;
    else 
      reverse_logic = s == "1";
    preferences.putInt("reverse_logic", reverse_logic);
    s = "";
  } else if (strcmp(command, "restart") == 0) {
    //s = "Restarting...";
    do_restart = true;
  } else if (strcmp(command, "update") == 0) {
    //s = "";
    fwfn = strtok(NULL, " ");
    if (fwfn == "") 
      fwfn = FIRMWARE_FILE;
    do_update = true;
  } else if (strcmp(command, "updateall") == 0) {
    s = "Updating all...";
    fwfn = strtok(NULL, " ");
    if (fwfn == "")
      fwfn = FIRMWARE_FILE;
    updateOthers(fwfn);
    do_update = true;
  } else {
    s = "Unknown: ";
    s += command;
    s += ". Available: relay [0/1], reverse [0/1], restart, update [fw_file_name], updateall [fw_file_name]";
  }
  return s;
}

void setup(void) {
  WiFi.persistent(false);

  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY); //to use RX as INPUT PIN
  Serial.print("Version: " SKETCH_VERSION "\n");

  pinMode(relay_pin, OUTPUT);

  preferences.begin("button");
  
  int i = preferences.getInt("reverse_logic", -1);
  if (i >= 0) reverse_logic = i;

  i = preferences.getInt("relay_closed", -1);
  if (i >= 0) relay(i);

  // Set WiFi to station mode
  WiFi.mode(WIFI_STA);

  // Register multi WiFi networks
  for (int i = 0; i < numssisds; i++) {
    //Serial.println(ssids[i]);
    wifiMulti.addAP(ssids[i], pass);
  }

  try_connect();
  handleThingsBoard(true);

}

void loop(void) {
  if (do_restart)
    doRestart();

  if (do_update)
    doUpdate();

  test_button();

  if (WiFi.status() == WL_CONNECTED) {
    handleThingsBoard();
    handleRxPin();

    server.handleClient();

#ifdef TELEGRAM
    if (tgavail) {
      // local variable to store telegram message data
      TBMessage msg;

      // if there is an incoming message...
      if (myBot.getNewMessage(msg) && (msg.chatId == userid || msg.chatId == userid_printer)) {
      //if (myBot.getNewMessage(msg)) {
        int l = msg.text.length() + 1;
        char buf[l];
        msg.text.toCharArray(buf, l);
        String s = process_tg_message(buf);
        if (s != "")
          myBot.sendMessage(msg, s);
      }
    }
#endif

  } else {
    try_connect();
  } 
}
